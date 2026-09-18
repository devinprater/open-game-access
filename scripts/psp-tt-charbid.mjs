#!/usr/bin/env node
// psp-tt-charbid.mjs — find the RAM field holding a fighter's ROSTER ID (message id).
//
// WHY: the identity mapping is now solved and verified:
//     roster index i  ->  message id = 843 + i   (843='Goku', 876='Frieza', 883='Cell', ...)
//     message id N    ->  the Nth string in PACKFILE.BIN's message table (UTF-16LE)
// The ONE remaining unknown is which RAM field holds a fighter's roster id.
//
// This is now a SEARCH FOR A DISTINCTIVE VALUE, not a fuzzy diff:
//   843 and 876 are large, unusual numbers (unlike roster index 0, which was unsearchable).
// This is the same technique that cracked HP (searching for the printed 'Max Damage 7460').
//
// It scans RAM for those values, and reports:
//   - whether the value sits inside a fighter struct (base 0x0973CEE0 + N*0x1A90)
//   - what the value looks like nearby (is it a lone id or part of a roster array?)
//
// Requires the debugger (ONE client). Read-only.
//
// Usage:
//   node scripts/psp-tt-charbid.mjs                # search for 843 and 876
//   node scripts/psp-tt-charbid.mjs --values 883 854
//   node scripts/psp-tt-charbid.mjs --scan-fighter  # only scan the 4 fighter structs

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const has = (f) => argv.includes(f);

const ROSTER_BASE_ID = 843;              // id of roster index 0 ('Goku')
const NAMES = { 843: 'Goku', 844: 'Kid Gohan', 845: 'Teen Gohan', 846: 'Gohan',
                848: 'Piccolo', 849: 'Krillin', 850: 'Yamcha', 851: 'Tien',
                854: 'Vegeta', 876: 'Frieza', 883: 'Cell', 891: 'Super Saiyan 3' };

const VALUES = (() => {
  const i = argv.indexOf('--values');
  if (i >= 0) return argv.slice(i + 1).map(Number);
  return [843, 876];
})();

// RAM window to scan when not restricted to the structs. The PSP user RAM used by this game
// spans roughly 0x08800000..0x0A000000 (the debugger's dump is 24 MiB from 0x08800000).
const RAM_LO = 0x08800000, RAM_HI = 0x0A000000;
const SCAN_STEP = 4;
const READ_CHUNK = 0x4000;               // bytes per read request

const BASE = 0x0973CEE0, STRIDE = 0x1A90;

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 15000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function readU32(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
const nameOf = (v) => NAMES[v] ? `${v} (${NAMES[v]})` : String(v);

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-charbid', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  // context: which phase are we in?
  const hp = [];
  for (const i of [0, 1, 2, 3]) hp.push(await readU32(BASE + i * STRIDE + 0x14));
  console.log('# fighter HP: ' + hp.map((v, i) => `s${i}=${v}`).join('  '));
  const inBattle = hp.some(v => v !== null && v > 0 && v <= 30000);
  console.log(inBattle ? '# IN BATTLE (structs populated)' : '# NOT in a battle -- results may be meaningless');

  console.log(`\n# searching for roster ids: ${VALUES.map(nameOf).join(', ')}`);

  // ---- pass 1: the four fighter structs (0x400 bytes each) ----
  console.log('\n=== PASS 1: inside the 4 fighter structs ===');
  let structHits = 0;
  for (const i of [0, 1, 2, 3]) {
    const base = BASE + i * STRIDE;
    for (let off = 0; off < 0x400; off += 4) {
      const v = await readU32(base + off);
      if (v !== null && VALUES.includes(v)) {
        console.log(`  slot ${i} +0x${off.toString(16).toUpperCase()}  = ${nameOf(v)}   [addr ${hex(base + off)}]`);
        structHits++;
      }
    }
  }
  console.log(`  struct hits: ${structHits}`);

  if (has('--scan-fighter')) { sock.close(); return; }

  // ---- pass 2: the wider RAM window, using BULK reads ----
  // memory.read {address, size} -> {base64}. Per-word reads would need millions of requests;
  // a bulk read makes the whole 24 MiB scan practical.
  console.log('\n=== PASS 2: scanning RAM with bulk reads ===');
  const CHUNK = 0x100000;                 // 1 MiB per request
  const found = new Map();                // addr -> value
  const t0 = Date.now();
  for (let a = RAM_LO; a < RAM_HI; a += CHUNK) {
    let buf = null;
    try {
      const r = await req('memory.read', { address: hex(a), size: CHUNK }, 20000);
      buf = Buffer.from(r.base64 || '', 'base64');
    } catch (e) {
      console.log(`  read failed at ${hex(a)}: ${e.message}`);
      continue;
    }
    for (let o = 0; o + 4 <= buf.length; o += 4) {
      const v = buf.readUInt32LE(o);
      if (VALUES.includes(v)) found.set(a + o, v);
    }
    const pct = ((a + CHUNK - RAM_LO) / (RAM_HI - RAM_LO) * 100).toFixed(0);
    console.log(`  ...${pct}%  hits: ${found.size}  (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
  }

  console.log(`\n=== RAM SEARCH RESULTS: ${found.size} hit(s) ===`);
  const byVal = {};
  for (const [a, v] of found) (byVal[v] = byVal[v] || []).push(a);
  for (const v of Object.keys(byVal).map(Number)) {
    console.log(`\n  ${nameOf(v)}  -> ${byVal[v].length} location(s)`);
    for (const a of byVal[v].slice(0, 25)) {
      // note if inside a fighter struct
      let where = '';
      for (const i of [0, 1, 2, 3]) {
        const base = BASE + i * STRIDE;
        if (a >= base && a < base + 0x1A90) where = `  ** inside fighter slot ${i} struct (+0x${(a - base).toString(16)}) **`;
      }
      console.log(`     ${hex(a)}${where}`);
    }
    if (byVal[v].length > 25) console.log(`     ... and ${byVal[v].length - 25} more`);
  }

  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
