#!/usr/bin/env node
// psp-tt-charbid2.mjs — GATED character-id search: only reports when a battle is PROVEN live.
//
// WHY: the previous charbid run returned "1 hit" while the liveness control said NOT in a battle,
// making the result inconclusive (and easy to misread as a negative). This version REFUSES to
// report a result it cannot stand behind.
//
// Protocol per attempt:
//   1. check liveness: fighter HP must CHANGE between two samples (combat/regen) AND be populated
//   2. if not live -> advance (single press, then re-check) up to N times
//   3. only with liveness PROVEN, scan the 4 fighter structs and the whole RAM for the roster ids
//   4. report the offset within the struct, or say clearly that the id is not in the struct
//
// Roster id map (verified): roster index i -> message id 843+i
//   843 'Goku'   844 'Kid Gohan'   848 'Piccolo'   854 'Vegeta'   876 'Frieza'   883 'Cell'
//
// Usage:
//   node scripts/psp-tt-charbid2.mjs                 # auto-advance until live, then scan
//   node scripts/psp-tt-charbid2.mjs --no-advance    # just scan what is on screen now

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const NO_ADVANCE = has('--no-advance');
const MAX_ADVANCE = 25;

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const RAM_LO = 0x08800000, RAM_HI = 0x0A000000;
const CHUNK = 0x100000;

// roster ids to hunt. Include a spread so that ANY two-fighter pairing is likely to match one.
const IDS = [843, 844, 845, 846, 848, 849, 850, 851, 854, 876, 883, 891];
const NAMES = { 843: 'Goku', 844: 'Kid Gohan', 845: 'Teen Gohan', 846: 'Gohan', 848: 'Piccolo',
                849: 'Krillin', 850: 'Yamcha', 851: 'Tien', 854: 'Vegeta', 876: 'Frieza',
                883: 'Cell', 891: 'Super Saiyan 3' };

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 20000) {
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
async function bulk(a, size) {
  const r = await req('memory.read', { address: hex(a), size }, 25000);
  return Buffer.from(r.base64 || '', 'base64');
}
async function hpAll() {
  const out = [];
  for (const i of [0, 1, 2, 3]) out.push({ i, cur: await readU32(BASE + i * STRIDE + 0x14), max: await readU32(BASE + i * STRIDE + 0x18) });
  return out;
}
const hpLine = (h) => h.map(x => `s${x.i}=${x.cur}/${x.max}`).join(' ');
const populated = (h) => h.some(x => x.max > 0 && x.cur !== null && x.cur <= x.max);

// LIVENESS = HP changes between two samples (combat or regen moves it)
async function isLive(gap = 1400) {
  const a = await hpAll(); await sleep(gap); const b = await hpAll();
  return a.some((x, k) => x.cur !== b[k].cur);
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-charbid2', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  let live = await isLive();
  if (!live && !NO_ADVANCE) {
    // ⚠️ On the TITLE screen `cross` is INERT (verified frame-by-frame: 8 presses changed nothing).
    // The title menu requires `start`; menus and cutscenes then take `cross`. So alternate:
    // start (raises the title menu) -> cross (accepts) -> cross... and keep re-feeding start in
    // case we fall back to the title.
    console.log('# not live; advancing with start/cross alternation until a battle starts...');
    const ADVANCE_BUTTONS = ['start', 'cross', 'cross', 'cross', 'start', 'cross', 'cross', 'cross'];
    for (let i = 1; i <= MAX_ADVANCE && !live; i++) {
      const btn = ADVANCE_BUTTONS[(i - 1) % ADVANCE_BUTTONS.length];
      await req('input.buttons.press', { button: btn });
      await sleep(1500);
      live = await isLive();
      if (i % 5 === 0) console.log(`  ...advance ${i} (last ${btn})  hp ${hpLine(await hpAll())}`);
    }
  }

  const h = await hpAll();
  console.log(`# GATE: populated=${populated(h)}  live=${live}`);
  console.log(`# HP: ${hpLine(h)}`);
  if (!populated(h) || !live) {
    console.log('');
    console.log('# REFUSING TO REPORT A RESULT: no battle is proven live.');
    console.log('# (Reporting here would be the too-narrow-probe mistake: a null result from a');
    console.log('#  state where the data cannot exist is not evidence of absence.)');
    sock.close();
    return;
  }

  console.log('\n=== PASS 1: roster ids inside the 4 fighter structs ===');
  let hits = 0;
  const addrById = {};
  for (const i of [0, 1, 2, 3]) {
    const base = BASE + i * STRIDE;
    const buf = await bulk(base, 0x1A90);
    for (let off = 0; off + 4 <= buf.length; off += 4) {
      const v = buf.readUInt32LE(off);
      if (IDS.includes(v)) {
        console.log(`  slot ${i}  +0x${off.toString(16).toUpperCase()}  = ${v} ${NAMES[v] ? '(' + NAMES[v] + ')' : ''}   [${hex(base + off)}]`);
        hits++;
        (addrById[v] = addrById[v] || []).push(base + off);
      }
    }
  }
  console.log(`  struct hits: ${hits}`);

  console.log('\n=== PASS 2: whole-RAM scan for the same ids ===');
  const found = new Map();
  const t0 = Date.now();
  for (let a = RAM_LO; a < RAM_HI; a += CHUNK) {
    let buf = null;
    try { buf = await bulk(a, CHUNK); } catch (e) { console.log(`  read failed ${hex(a)}: ${e.message}`); continue; }
    for (let o = 0; o + 4 <= buf.length; o += 4) {
      const v = buf.readUInt32LE(o);
      if (IDS.includes(v)) found.set(a + o, v);
    }
  }
  console.log(`  scanned in ${((Date.now() - t0) / 1000).toFixed(1)}s`);

  const byVal = {};
  for (const [a, v] of found) (byVal[v] = byVal[v] || []).push(a);
  for (const v of Object.keys(byVal).map(Number).sort((x, y) => x - y)) {
    console.log(`\n  ${v} ${NAMES[v] ? '(' + NAMES[v] + ')' : ''}  -> ${byVal[v].length} location(s)`);
    for (const a of byVal[v].slice(0, 12)) {
      let where = '';
      for (const i of [0, 1, 2, 3]) {
        const base = BASE + i * STRIDE;
        if (a >= base && a < base + 0x1A90) where = `  ** fighter slot ${i} +0x${(a - base).toString(16)} **`;
      }
      console.log(`     ${hex(a)}${where}`);
    }
  }
  if (!found.size) {
    console.log('\n  NO roster ids found anywhere in RAM.');
    console.log('  => the id is NOT stored as a plain 843+i value; it is transformed (see the doc).');
  }
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
