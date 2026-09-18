#!/usr/bin/env node
// psp-tt-movetest.mjs — drive the analog stick AND diff every struct field, in ONE connection.
//
// WHY: proving the stick moves the character requires three things at once, and no existing script
// did all three:
//   1. the stick driver and the memory reader must share the ONE available debugger client
//   2. the diff must cover the whole struct (the live position field is not yet known --
//      +0x0B0 turned out to be a spawn anchor)
//   3. a LIVENESS control must prove the game is actually interactive, otherwise a zero-change
//      result is indistinguishable from a paused/ended/finished state (this invalidated several
//      earlier conclusions)
//
// It samples the struct, HOLDS the stick, samples again, and reports fields that moved while
// printing the HP delta as the liveness control.
//
// Usage:
//   node scripts/psp-tt-movetest.mjs --slot 0 --x 1 --y 0 --hold 2500
//   node scripts/psp-tt-movetest.mjs --slot 0 --all      # try 4 directions in turn

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const SLOT = Number(val('--slot', 0));
const SPAN = Number(val('--span', 0xC00));
const HOLD = Number(val('--hold', 2500));

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
async function rd(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
const toF = (u) => { const b = new ArrayBuffer(4); const d = new DataView(b); d.setUint32(0, u >>> 0, true); return d.getFloat32(0, true); };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function sample(base, span) {
  const out = [];
  for (let off = 0; off < span; off += 4) out.push(await rd(base + off));
  return out;
}

// hold the stick by RE-SENDING the position (a single send may be consumed as one frame)
async function hold(x, y, ms) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    await req('input.analog.send', { x, y, stick: 'left' });
    await sleep(90);
  }
  await req('input.analog.send', { x: 0, y: 0, stick: 'left' });
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-movetest', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  const base = BASE + SLOT * STRIDE;
  console.log(`# slot ${SLOT} struct 0x${base.toString(16).toUpperCase()}, span 0x${SPAN.toString(16)}`);

  const dirs = has('--all')
    ? [[1, 0, 'right'], [-1, 0, 'left'], [0, -1, 'up'], [0, 1, 'down']]
    : [[Number(val('--x', 1)), Number(val('--y', 0)), 'custom']];

  for (const [x, y, nm] of dirs) {
    console.log(`\n# === holding stick ${nm} (x=${x} y=${y}) for ${HOLD} ms ===`);
    const hp0 = await rd(base + 0x14);
    const a = await sample(base, SPAN);
    await hold(x, y, HOLD);
    const b = await sample(base, SPAN);
    const hp1 = await rd(base + 0x14);
    console.log(`# LIVENESS: HP ${hp0} -> ${hp1}  ${hp0 !== hp1 ? '(game IS live)' : '(HP unchanged -- battle may be over or this slot is idle)'}`);

    const rows = [];
    for (let i = 0; i < a.length; i++) {
      if (a[i] === b[i]) continue;
      const fa = toF(a[i]), fb = toF(b[i]);
      const ok = (v) => v === v && Math.abs(v) < 1e6;
      if (ok(fa) && ok(fb)) {
        const d = Math.abs(fb - fa);
        if (d > 0.005) rows.push({ off: i * 4, fa, fb, d });
      }
    }
    rows.sort((p, q) => q.d - p.d);
    console.log(`# float fields that moved: ${rows.length}`);
    for (const r of rows.slice(0, 18)) {
      console.log(`   +0x${r.off.toString(16).toUpperCase().padStart(3, '0')}  ${r.fa.toFixed(3)} -> ${r.fb.toFixed(3)}  (d=${r.d.toFixed(3)})`);
    }
  }
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
