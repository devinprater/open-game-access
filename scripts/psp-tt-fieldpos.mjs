#!/usr/bin/env node
// psp-tt-fieldpos.mjs — find the player POSITION in the field (flight) mode.
//
// WHY: in the story FIELD the battle fighter struct (0x0973CEE0 + N*0x1A90) reads 0/0 -- the field
// uses different state. To fly to an objective we must first know WHERE the player is.
//
// METHOD (validated pattern from this project): move, then diff only same-state samples.
//   1. bulk-read all user RAM
//   2. hold the analog stick for a moment (`input.analog.send`)
//   3. bulk-read again
//   4. report float values that changed, and group candidates into (x,y,z) triples
//
// A position shows up as a float TRIPLE where the ground-plane components change and the vertical
// stays roughly constant while walking/flying level.
//
// Usage:
//   node scripts/psp-tt-fieldpos.mjs                 # auto-move north
//   node scripts/psp-tt-fieldpos.mjs --dir left --hold 1800

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const DIR = val('--dir', 'up');
const HOLD = Number(val('--hold', 1800));
// ⚠️ CONTRACT (measured): input.analog.send takes NORMALIZED floats in [-1.0, 1.0].
// Sending 0..255 is REJECTED ("Parameter 'x' must be between -1.0 and 1.0") and nothing moves.
// `input.analog` (without .send) is BROADCAST-ONLY and errors "Bad message: unknown event".
// The stick state must be re-sent continuously (~20Hz) while held.
const AXES = { up: [0, -1], down: [0, 1], left: [-1, 0], right: [1, 0] };

const LO = 0x08800000, HI = 0x0A000000, CHUNK = 0x100000;

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 25000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function fullRAM() {
  const parts = [];
  for (let a = LO; a < HI; a += CHUNK) {
    const r = await req('memory.read', { address: hex(a), size: CHUNK });
    parts.push(Buffer.from(r.base64 || '', 'base64'));
  }
  return Buffer.concat(parts);
}

// A float is "interesting" if finite, in a sane game range, and not a denormal/huge value.
function f32(b, o) { return b.readFloatLE(o); }
function sane(v) {
  return Number.isFinite(v) && Math.abs(v) > 0.0001 && Math.abs(v) < 100000;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-fieldpos', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  console.log('# pass 1: reading RAM...');
  const A = await fullRAM();

  const [ax, ay] = AXES[DIR] || AXES.up;
  console.log(`# holding analog ${DIR} for ${HOLD}ms (analog.send x=${ax} y=${ay}, normalized)`);
  // Re-send continuously: the stick state is polled per frame, so a single send is not enough.
  const t0h = Date.now();
  while (Date.now() - t0h < HOLD) {
    try { await req('input.analog.send', { stick: 'left', x: ax, y: ay }, 2500); } catch {}
    await sleep(50);
  }
  try { await req('input.analog.send', { stick: 'left', x: 0, y: 0 }, 2500); } catch {}

  console.log('# pass 2: reading RAM...');
  const B = await fullRAM();

  console.log('# diffing for changed FLOATS (position candidates)...');
  const changed = [];
  for (let o = 0; o + 4 <= A.length; o += 4) {
    if (A.readUInt32LE(o) === B.readUInt32LE(o)) continue;
    const a = f32(A, o), b = f32(B, o);
    if (!sane(a) || !sane(b)) continue;
    changed.push({ off: o, a, b });
  }
  console.log(`# ${changed.length} changed sane float(s)`);

  // Group into triples: three consecutive changed floats 4 bytes apart, same base alignment.
  const byOff = new Map(changed.map((c) => [c.off, c]));
  const triples = [];
  for (const c of changed) {
    if (byOff.has(c.off - 4)) continue;              // not the first of a run
    const run = [c];
    let o = c.off + 4;
    while (byOff.has(o) && run.length < 8) { run.push(byOff.get(o)); o += 4; }
    if (run.length >= 3) triples.push(run);
  }

  console.log(`\n=== POSITION CANDIDATES (runs of >=3 changed floats) — ${triples.length} ===`);
  for (const run of triples.slice(0, 20)) {
    const addr = LO + run[0].off;
    const d = run.map((r) => (r.b - r.a).toFixed(2));
    console.log(`  ${hex(addr)}  n=${run.length}`);
    console.log(`      before: ${run.map((r) => r.a.toFixed(2)).join(', ')}`);
    console.log(`      after : ${run.map((r) => r.b.toFixed(2)).join(', ')}`);
    console.log(`      delta : ${d.join(', ')}`);
    if (run.length >= 3) {
      // level flight: horizontal changes, vertical ~unchanged
      const vert = Math.abs(run[1].b - run[1].a);
      const horiz = Math.abs(run[0].b - run[0].a) + Math.abs(run[2].b - run[2].a);
      const verdict = (vert < horiz * 0.5 && horiz > 0.5) ? '  <== LEVEL-MOVE (pos x,y,z)' : '';
      console.log(`      vert=${vert.toFixed(2)} horiz=${horiz.toFixed(2)}${verdict}`);
    }
  }

  console.log('\n# NEXT: run again holding the OPPOSITE direction; the position address will move back.');
  console.log('#       Then the objective vector = normalize(objective_pos - player_pos).');
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
