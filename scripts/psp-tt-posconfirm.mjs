#!/usr/bin/env node
// psp-tt-posconfirm.mjs — CONFIRM the live position field by correlating memory deltas with a
// VISIBLE camera/scene change, in one connection.
//
// WHY: psp-tt-movetest found directional, accumulating float deltas at +0x4C4/+0x4D4 on the active
// slot, but a screenshot afterwards showed a LOSE screen -- so those deltas may have come from
// falling OUT of the field rather than from stick-driven movement. A delta that appears while the
// liveness control is failing must not be trusted.
//
// This script closes that gap by doing, for each direction:
//   1. screenshot BEFORE (psp-shot.py -- no socket needed)
//   2. read the candidate fields BEFORE
//   3. hold the stick
//   4. read the candidate fields AFTER
//   5. screenshot AFTER
// and printing the candidates repeatedly DURING the hold, so accumulation (real movement) is
// distinguishable from a settle/oscillation (animation).
//
// Usage: node scripts/psp-tt-posconfirm.mjs --slot 2 --x 1 --y 0 --hold 4000

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const SLOT = Number(val('--slot', 2));
const X = Number(val('--x', 1)), Y = Number(val('--y', 0));
const HOLD = Number(val('--hold', 4000));
const CAND = [0x4C4, 0x4D4, 0x504, 0x158, 0x0D4, 0x550];
const SHOT_DIR = (process.env.LOCALAPPDATA || '') + '\\Temp\\psp-probe';

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

async function shot(name) {
  const { execFileSync } = await import('node:child_process');
  const py = `${process.cwd().replace(/\\/g, '/')}/scripts/psp-shot.py`;
  const out = `${SHOT_DIR}\\${name}.png`;
  try { execFileSync('python', ['-c',
    `import sys,importlib.util as u;s=u.spec_from_file_location("ps",r"${py}");m=u.module_from_spec(s);s.loader.exec_module(m);sys.argv=["psp-shot.py",r"${out}"];raise SystemExit(m.main())`
  ], { encoding: 'utf8', timeout: 60000 }); return name; } catch { return 'FAILED'; }
}

async function cands(base) {
  const o = {};
  for (const c of CAND) o[c] = toF(await rd(base + c));
  return o;
}
function show(o) { return CAND.map(c => `+0x${c.toString(16).toUpperCase()}=${o[c].toFixed(2)}`).join('  '); }

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect')), { once: true });
  });
  await req('version', { name: 'posconfirm', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) await req('cpu.resume'); } catch {}

  const base = BASE + SLOT * STRIDE;
  const hpBefore = await rd(base + 0x14);
  console.log(`# slot ${SLOT} base 0x${base.toString(16).toUpperCase()}  HP=${hpBefore}`);

  await shot(`pos-${SLOT}-before`);
  const a = await cands(base);
  console.log(`# BEFORE      ${show(a)}`);

  // hold, sampling throughout so accumulation is visible
  const samples = [];
  const t0 = Date.now();
  while (Date.now() - t0 < HOLD) {
    await req('input.analog.send', { x: X, y: Y, stick: 'left' });
    samples.push({ t: ((Date.now() - t0) / 1000).toFixed(1), v: await cands(base) });
    await sleep(400);
  }
  await req('input.analog.send', { x: 0, y: 0, stick: 'left' });

  console.log('# DURING hold (every ~0.4 s):');
  for (const s of samples) console.log(`   t=${String(s.t).padStart(4)}s  ${show(s.v)}`);

  const b = await cands(base);
  const hpAfter = await rd(base + 0x14);
  await shot(`pos-${SLOT}-after`);
  console.log(`# AFTER       ${show(b)}`);
  console.log(`# LIVENESS: HP ${hpBefore} -> ${hpAfter}  ${hpBefore !== hpAfter ? '(changed)' : '(UNCHANGED -- be suspicious of the deltas)'}`);

  console.log('# interpretation:');
  for (const c of CAND) {
    const vals = samples.map(s => s.v[c]);
    const span = Math.max(...vals) - Math.min(...vals);
    const net = b[c] - a[c];
    const monotonic = vals.every((v, i) => i === 0 || Math.abs(v - vals[i - 1]) < 0.001 || (vals[vals.length - 1] - vals[0]) * (v - vals[i - 1]) >= 0);
    console.log(`   +0x${c.toString(16).toUpperCase()}  net=${net.toFixed(3)}  range=${span.toFixed(3)}  ${span > 2 ? 'SUSPECT: large travel' : 'small/oscillating'}  ${monotonic ? 'accumulating' : 'non-monotonic'}`);
  }
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
