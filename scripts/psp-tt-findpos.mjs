#!/usr/bin/env node
// psp-tt-findpos.mjs — find the LIVE position field by diffing a fighter struct over time.
//
// WHY: +0x0B0 held a plausible float (x,y,z) triple but it never changed during live battle
// while HP dropped normally. So it is a spawn anchor / fixed origin, NOT the live position.
// The reliable way to find a live position is: sample the struct twice during motion and keep
// the fields whose FLOAT values changed.
//
// Usage:
//   node scripts/psp-tt-findpos.mjs [--slot 2] [--ms 2000] [--span 0x400]

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const SLOT = Number(val('--slot', 2));
const WAIT = Number(val('--ms', 2000));
const SPAN = Number(val('--span', 0x400));

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
const hex = (n) => '0x' + n.toString(16).toUpperCase().padStart(8, '0');
async function readU32(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
const toF = (u) => { const b = new ArrayBuffer(4); const d = new DataView(b); d.setUint32(0, u >>> 0, true); return d.getFloat32(0, true); };

async function sample(base, span) {
  const out = [];
  for (let off = 0; off < span; off += 4) out.push(await readU32(base + off));
  return out;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'findpos', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  const base = BASE + SLOT * STRIDE;
  console.log(`# slot ${SLOT} struct 0x${base.toString(16).toUpperCase()}, span 0x${SPAN.toString(16)}`);
  const hp0 = await readU32(base + 0x14);
  console.log(`# HP at start: ${hp0}  (if this is 0 the slot is empty)`);

  const a = await sample(base, SPAN);
  console.log(`# sampling again after ${WAIT} ms ...`);
  await new Promise(r => setTimeout(r, WAIT));
  const b = await sample(base, SPAN);
  const hp1 = await readU32(base + 0x14);
  console.log(`# HP at end:   ${hp1}   (changed by ${hp1 - hp0} -> the game IS live if this is non-zero)`);

  // float fields that changed by a meaningful amount
  const rows = [];
  for (let i = 0; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    const fa = toF(a[i]), fb = toF(b[i]);
    const plausible = (v) => v === v && Math.abs(v) < 100000;
    if (plausible(fa) && plausible(fb)) {
      const d = Math.abs(fb - fa);
      // a live position typically changes by a few units per sample; ignore tiny jitter and huge garbage
      if (d > 0.01 && d < 20000) rows.push({ off: i * 4, fa, fb, d, ua: a[i], ub: b[i] });
    }
  }
  rows.sort((x, y) => y.d - x.d);
  console.log(`# changed float fields: ${rows.length}`);
  for (const r of rows.slice(0, 40)) {
    console.log(`   +0x${r.off.toString(16).toUpperCase().padStart(3, '0')}  ${r.fa.toFixed(3)} -> ${r.fb.toFixed(3)}   (d=${r.d.toFixed(3)})`);
  }

  // also report u32 fields that changed, in case position is fixed-point
  const urows = [];
  for (let i = 0; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    urows.push({ off: i * 4, a: a[i], b: b[i] });
  }
  console.log(`# changed u32 fields: ${urows.length} (first 25)`);
  for (const r of urows.slice(0, 25)) {
    console.log(`   +0x${r.off.toString(16).toUpperCase().padStart(3, '0')}  ${r.a} -> ${r.b}`);
  }

  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
