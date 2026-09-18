#!/usr/bin/env node
// psp-tt-route.mjs — SLOW, VERIFIED navigation: one press, screenshot, repeat.
//
// WHY: burst-pressing (`cross` repeatedly) skips screens and spills into the next battle's
// start-up, which loses fights and hides the route. This walks a plan with a configurable gap and
// a SCREENSHOT after every single step, so the route is provable rather than inferred.
//
// Screenshots go through scripts/psp-shot.py (PrintWindow) so they do NOT consume the debugger
// socket — the presser can therefore also be the capturer.
//
// Usage:
//   node scripts/psp-tt-route.mjs --tag r1 --gap 2500 --plan "down,down,down,cross"
//   node scripts/psp-tt-route.mjs --tag r2 --plan "cross" --repeat 8

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const TAG = val('--tag', 'route');
const GAP = Number(val('--gap', 2500));
const PLAN = String(val('--plan', 'cross')).split(',').map(s => s.trim()).filter(Boolean);
const REPEAT = Number(val('--repeat', 1));
const SHOT_DIR = val('--shot-dir', (process.env.LOCALAPPDATA || '') + '\\Temp\\psp-probe');

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
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const hex = (n) => '0x' + n.toString(16).toUpperCase().padStart(8, '0');

async function rd(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
// HP of the four tag-team slots, as a liveness/state aid printed alongside each step
async function hpLine() {
  const B = 0x0973CEE0, ST = 0x1A90, out = [];
  for (const i of [0, 1, 2, 3]) out.push(`s${i}=${await rd(B + i * ST + 0x14)}`);
  return out.join(' ');
}

async function shot(name) {
  const { execFileSync } = await import('node:child_process');
  const out = `${SHOT_DIR}\\${name}.png`;
  const py = `${process.cwd().replace(/\\/g, '/')}/scripts/psp-shot.py`;
  try {
    const r = execFileSync('python', ['-c',
      `import sys,importlib.util as u;s=u.spec_from_file_location("ps",r"${py}");m=u.module_from_spec(s);s.loader.exec_module(m);sys.argv=["psp-shot.py",r"${out}"];raise SystemExit(m.main())`
    ], { encoding: 'utf8', timeout: 60000 });
    return `${name}.png`;
  } catch (e) { return `FAILED:${e.message.split('\n')[0]}`; }
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-route', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  let n = 0;
  const start = await shot(`${TAG}-000-start`);
  console.log(`# step 000  (start)      HP ${await hpLine()}  shot ${start}`);

  for (let r = 0; r < REPEAT; r++) {
    for (const button of PLAN) {
      n++;
      await req('input.buttons.press', { button });
      await sleep(GAP);
      const s = await shot(`${TAG}-${String(n).padStart(3, '0')}-${button}`);
      console.log(`# step ${String(n).padStart(3, '0')}  press ${button.padEnd(8)}  HP ${await hpLine()}  shot ${s}`);
    }
  }
  console.log('# done. Inspect the PNGs in order — each corresponds to exactly one press.');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
