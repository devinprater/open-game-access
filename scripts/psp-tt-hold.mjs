#!/usr/bin/env node
// psp-tt-hold.mjs — HOLD a button for a real duration, then screenshot.
//
// WHY: `input.buttons.press` is MOMENTARY (default 1 frame). For anything requiring a sustained
// hold (charging, flying, menu repeats) a momentary press is silently ineffective — which looks
// exactly like "the button does nothing".
//
// `input.buttons.press` accepts an optional `duration` in FRAMES (from PPSSPP's InputSubscriber:
// "duration: optional integer indicating frames to press for, defaults to 1"). This script uses
// that, and also offers `input.buttons.send`-style explicit down/up for long holds.
//
// Usage:
//   node scripts/psp-tt-hold.mjs --button cross --frames 90 --tag h1
//   node scripts/psp-tt-hold.mjs --button ltrigger --frames 60 --also cross
//   node scripts/psp-tt-hold.mjs --button circle --seconds 2

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const has = (f) => argv.includes(f);

const BUTTON = val('--button', 'cross');
const FRAMES = Number(val('--frames', 60));
const SECONDS = val('--seconds', null);
const ALSO = val('--also', null);
const PRESSES = Number(val('--presses', 0));
const TAG = val('--tag', 'hold');
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
function req(event, extra = {}, timeout = 20000) {
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
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function shot(name) {
  const { execFileSync } = await import('node:child_process');
  const py = `${process.cwd().replace(/\\/g, '/')}/scripts/psp-shot.py`;
  const out = `${SHOT_DIR}\\${name}.png`;
  try { execFileSync('python', ['-c',
    `import sys,importlib.util as u;s=u.spec_from_file_location("ps",r"${py}");m=u.module_from_spec(s);s.loader.exec_module(m);sys.argv=["psp-shot.py",r"${out}"];raise SystemExit(m.main())`
  ], { encoding: 'utf8', timeout: 60000 }); return name; } catch { return 'FAILED'; }
}

async function hpLine() {
  const B = 0x0973CEE0, ST = 0x1A90, out = [];
  for (const i of [0, 1, 2, 3]) out.push(`s${i}=${await rd(B + i * ST + 0x14)}`);
  return out.join(' ');
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-hold', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  console.log(`# before: HP ${await hpLine()}   shot ${await shot(TAG + '-before')}`);

  if (SECONDS !== null) {
    // explicit down/up hold using buttons.send (state), for durations that must be wall-clock
    const secs = Number(SECONDS);
    console.log(`# HOLDING ${BUTTON} via buttons.send for ${secs}s`);
    await req('input.buttons.send', { buttons: { [BUTTON]: true } });
    if (ALSO) await req('input.buttons.send', { buttons: { [ALSO]: true } });
    const t0 = Date.now();
    while ((Date.now() - t0) / 1000 < secs) await sleep(200);
    await req('input.buttons.send', { buttons: { [BUTTON]: false } });
    if (ALSO) await req('input.buttons.send', { buttons: { [ALSO]: false } });
  } else {
    // frames-based press (PPSSPP's native duration parameter)
    console.log(`# PRESS ${BUTTON} for ${FRAMES} frames` +
      (ALSO ? ` (with ${ALSO} also held)` : ''));
    if (ALSO) await req('input.buttons.send', { buttons: { [ALSO]: true } });
    await req('input.buttons.press', { button: BUTTON, duration: FRAMES });
    if (ALSO) await req('input.buttons.send', { buttons: { [ALSO]: false } });
  }

  for (let i = 0; i < PRESSES; i++) {
    await req('input.buttons.press', { button: BUTTON });
    await sleep(700);
  }

  await sleep(1200);
  console.log(`# after : HP ${await hpLine()}   shot ${await shot(TAG + '-after')}`);
  console.log('# compare the two PNGs');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
