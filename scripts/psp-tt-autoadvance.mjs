#!/usr/bin/env node
// psp-tt-autoadvance.mjs — press ONLY when the screen has stopped changing.
//
// WHY THIS EXISTS (the last missing instrument):
//   - mashing `cross` skips screens and spills into the next battle's start-up (lost fights)
//   - pressing nothing lets a cutscene time out back to the attract demo
//   - so the correct behaviour is: press ONCE when the frame has been STATIC for a while, i.e. the
//     game has finished animating and is waiting for input.
//
// Detection uses FRAME DIFFERENCE (the field/cutscene-appropriate control), not HP -- HP reads 0/0
// during cutscenes and is only valid in battle.
//
// Screenshots come from scripts/psp-shot.py (PrintWindow), which does NOT use the debugger socket,
// so this script can both drive input AND capture frames over the one available client.
//
// Usage:
//   node scripts/psp-tt-autoadvance.mjs --iters 60 --static-ms 2500 --diff-threshold 0.004
//   node scripts/psp-tt-autoadvance.mjs --tag aa --button cross
//
// Safety: refuses to advance when a battle is live (HP changing) -- it reports that instead, so it
// never presses through a fight.

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const ITERS = Number(val('--iters', 60));
const BUTTON = val('--button', 'cross');
const STATIC_MS = Number(val('--static-ms', 2500));      // how long the frame must be unchanged
const THRESHOLD = Number(val('--diff-threshold', 0.004)); // fraction of sampled px that must differ
const POLL_MS = Number(val('--poll-ms', 700));
const TAG = val('--tag', 'aa');
const SHOT_DIR = (process.env.LOCALAPPDATA || '') + '\\Temp\\psp-probe';

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
async function hpAll() {
  const out = [];
  for (const i of [0, 1, 2, 3]) {
    out.push({ i, cur: await readU32(BASE + i * STRIDE + 0x14), max: await readU32(BASE + i * STRIDE + 0x18) });
  }
  return out;
}
// a battle is live if populated HP is CHANGING
async function battleLive(gap = 1100) {
  const a = await hpAll(); await sleep(gap); const b = await hpAll();
  const populated = a.some(x => x.max > 0 && x.cur !== null && x.cur <= x.max);
  const changing = a.some((x, k) => x.cur !== b[k].cur);
  return { populated, changing, live: populated && changing, hp: b };
}

// frame difference WITHOUT a second debugger client: capture to a temp PNG and compare with the
// previous one. Uses only file IO + the Python capturer, so the socket stays ours.
async function capture(name) {
  const { execFileSync } = await import('node:child_process');
  const py = `${process.cwd().replace(/\\/g, '/')}/scripts/psp-shot.py`;
  const out = `${SHOT_DIR}\\${name}.png`;
  try {
    execFileSync('python', ['-c',
      `import sys,importlib.util as u;s=u.spec_from_file_location("ps",r"${py}");m=u.module_from_spec(s);s.loader.exec_module(m);sys.argv=["psp-shot.py",r"${out}"];raise SystemExit(m.main())`
    ], { encoding: 'utf8', timeout: 60000 });
    return out;
  } catch { return null; }
}

// Fraction of DIFFERING pixels between two frames.
//
// ⛔ BUG THIS FIXES: the first version sampled on a coarse fixed grid (every 6th pixel in x and y).
// When the changed region is small or sits between sample points, that grid misses it ENTIRELY and
// reports 0.0000 on frames that demonstrably differ (verified: ImageChops bbox was
// (677,350,1690,1066) while the grid said 0.0000). A diff that can miss a real change is not a
// liveness control at all.
//
// Fix: use getbbox() first (cheap, exact), then only if that is None is the frame truly identical.
// Otherwise measure the changed AREA as a fraction of the frame -- which is stable regardless of
// where on the screen the change happens.
async function diffFraction(a, b) {
  const { execFileSync } = await import('node:child_process');
  const code = `
from PIL import Image, ImageChops
import sys
a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
if a.size != b.size:
    print("1.0"); sys.exit()
d = ImageChops.difference(a, b)
bb = d.getbbox()                 # exact: None means the frames are IDENTICAL
if bb is None:
    print("0.0"); sys.exit()
# fraction of the frame covered by the changed region
x0, y0, x1, y1 = bb
W, H = d.size
print((x1 - x0) * (y1 - y0) / float(W * H))
`;
  try {
    const out = execFileSync('python', ['-c', code, a, b], { encoding: 'utf8', timeout: 60000 });
    return Number(String(out).trim());
  } catch { return 1; }
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-autoadvance', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  console.log(`# pressing '${BUTTON}' only after the frame is static for ${STATIC_MS} ms`);
  console.log(`# diff threshold ${THRESHOLD} (fraction of sampled pixels)`);

  let prev = await capture(`${TAG}-000`);
  let staticSince = Date.now();
  let presses = 0;

  for (let i = 1; i <= ITERS; i++) {
    await sleep(POLL_MS);

    // never press through a fight
    const b = await battleLive(900);
    if (b.live) {
      console.log(`# [${i}] BATTLE LIVE (populated+changing) -> NOT advancing. hp ${b.hp.map(x => x.cur).join('/')}`);
      staticSince = Date.now();     // reset so we do not immediately press when it ends
      continue;
    }

    const cur = await capture(`${TAG}-${String(i).padStart(3, '0')}`);
    if (!cur || !prev) { prev = cur || prev; staticSince = Date.now(); continue; }

    const d = await diffFraction(prev, cur);
    prev = cur;

    if (d > THRESHOLD) {
      // still animating
      staticSince = Date.now();
      process.stdout.write('.');
      continue;
    }

    // frame is stable -> has it been stable long enough?
    if (Date.now() - staticSince >= STATIC_MS) {
      await req('input.buttons.press', { button: BUTTON });
      presses++;
      console.log(`\n# [${i}] static ${((Date.now() - staticSince) / 1000).toFixed(1)}s (diff ${d.toFixed(4)}) -> press ${BUTTON}  (total ${presses})`);
      staticSince = Date.now() + 1500;    // grace period: let the press take effect
    } else {
      process.stdout.write('-');
    }
  }
  console.log(`\n# done: ${presses} press(es)`);
  console.log(`# frames: ${SHOT_DIR}\\${TAG}-*.png  (inspect them in order)`);
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
