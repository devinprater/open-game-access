#!/usr/bin/env node
// psp-tt-identity.mjs — follow the +0x1A1 pointer and inspect the 0x08A75538 table.
//
// WHY: Codex's p-code slicing showed the game resolves character/UI names via
//   FUN_0883ee74( <literal base> + index*4 , -1 )
// with the key sources being tables at 0x8a75538 / 0x8a7553c / 0x8a80xxx / 0x8a72100, and a
// single struct field at +0x1A1 being dereferenced 358 times. A name is therefore NOT stored as a
// pointer anywhere -- it is looked up through these tables.
//
// This script, in ONE debugger connection:
//   1. reports the phase (fighter struct populated? -> we are in a battle)
//   2. reads the +0x1A1 field of each of the 4 fighter structs and follows it if it is a pointer
//   3. dumps a window around each table base so the entries can be inspected
//   4. reads the same in a second sample so CHANGING fields stand out
//
// Usage: node scripts/psp-tt-identity.mjs [--span 0x100]

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const SPAN = Number(val('--span', 0x100));

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const TABLES = [0x08A75538, 0x08A7553C, 0x08A72100, 0x08A80028, 0x08A80090, 0x08A80118, 0x08B41AF8];
const F1A1 = 0x1A1;

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 8000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
async function rd(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function window32(base, span) {
  const out = [];
  for (let o = 0; o < span; o += 4) out.push({ a: base + o, v: await rd(base + o) });
  return out;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-identity', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed (was frozen)'); } } catch {}

  // ---- phase ----
  const hp = [];
  for (const i of [0, 1, 2, 3]) hp.push({ i, cur: await rd(BASE + i * STRIDE + 0x14), max: await rd(BASE + i * STRIDE + 0x18) });
  const live = hp.filter(h => h.max && h.max > 0 && h.cur <= h.max);
  console.log('# phase: ' + hp.map(h => `s${h.i}=${h.cur}/${h.max}`).join('  '));
  console.log(live.length ? `# IN BATTLE (${live.length} populated slot(s))` : '# NOT in a battle -- the +0x1A1 field may be meaningless here');

  // ---- follow +0x1A1 ----
  console.log('\n=== +0x1A1 of each fighter struct ===');
  for (const i of [0, 1, 2, 3]) {
    const a = BASE + i * STRIDE + F1A1;
    const v = await rd(a);
    let note = '';
    if (v && v >= 0x08800000 && v < 0x0A000000) {
      const t0 = await rd(v);
      note = `  -> *0x${v.toString(16)} = ${t0} (0x${(t0 >>> 0).toString(16)})`;
    }
    console.log(`  s${i}  0x${a.toString(16).toUpperCase()}  0x${(v >>> 0).toString(16).toUpperCase().padStart(8, '0')}${note}`);
  }

  // ---- table windows, twice, marking changes ----
  console.log(`\n=== table windows (span 0x${SPAN.toString(16)}), sampled twice ===`);
  for (const tb of TABLES) {
    const a1 = await window32(tb, SPAN);
    await sleep(1500);
    const a2 = await window32(tb, SPAN);
    const changed = a1.filter((x, k) => x.v !== a2[k].v).length;
    console.log(`\n--- 0x${tb.toString(16).toUpperCase()}   changed values: ${changed}`);
    const s1 = a1.map(x => (x.v >>> 0).toString(16).padStart(8, '0')).join(' ');
    console.log('    ' + s1);
  }

  // ---- also show a broad window at the roster text table so we can see if anything indexes it ----
  console.log('\n=== roster text table head (0x08C85C82) as UTF-16 sanity check ===');
  const head = [];
  for (let o = 0; o < 0x60; o += 2) head.push(await rd(0x08C85C82 + o));
  console.log('    ' + head.map(v => (v & 0xffff).toString(16).padStart(4, '0')).join(' '));

  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
