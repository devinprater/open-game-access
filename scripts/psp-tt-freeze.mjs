#!/usr/bin/env node
// psp-tt-freeze.mjs — press a button, FREEZE the emulator, screenshot, then unfreeze.
//
// WHY: this game (and PPSSPP generally) keeps running an attract/demo loop. A menu raised by
// `start` scrolls away within a second or two, so a normal press-then-screenshot misses it.
// `cpu.stepping` stops the CPU, so the frame stays exactly as it was when the menu was up --
// which makes the menu reliably capturable and readable.
//
// Sequence per step:
//   1. resume (make sure the game is live)
//   2. press the button
//   3. wait `--settle` ms
//   4. cpu.stepping = true     -> freeze
//   5. screenshot              -> captures the frozen frame
//   6. cpu.resume              -> unfreeze
//
// Usage:
//   node scripts/psp-tt-freeze.mjs --tag fz --settle 500 --plan "start,cross,cross"
//   node scripts/psp-tt-freeze.mjs --tag fz --settle 300 --plan "cross" --repeat 6

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const TAG = val('--tag', 'fz');
const SETTLE = Number(val('--settle', 500));
const PLAN = String(val('--plan', 'cross')).split(',').map(s => s.trim()).filter(Boolean);
const REPEAT = Number(val('--repeat', 1));
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
function req(event, extra = {}, timeout = 10000) {
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
// phase signature from the fighter struct: 0/0 = unpopulated, 30000 = battle/field,
// 1065353216 (float 1.0) = menu, 0 = empty slot
async function phase() {
  const B = 0x0973CEE0, ST = 0x1A90;
  const cur = await rd(B + 2 * ST + 0x14), max = await rd(B + 2 * ST + 0x18);
  if (max === 0 && cur === 0) return 'UNPOPULATED(boot/title/cutscene)';
  if (cur === 1065353216 || max === 1065353216) return 'MENU/select(float1.0)';
  if (max > 0) return `BATTLE/FIELD(${cur}/${max})`;
  return `?(${cur}/${max})`;
}

async function shot(name) {
  const { execFileSync } = await import('node:child_process');
  const py = `${process.cwd().replace(/\\/g, '/')}/scripts/psp-shot.py`;
  const out = `${SHOT_DIR}\\${name}.png`;
  try { execFileSync('python', ['-c',
    `import sys,importlib.util as u;s=u.spec_from_file_location("ps",r"${py}");m=u.module_from_spec(s);s.loader.exec_module(m);sys.argv=["psp-shot.py",r"${out}"];raise SystemExit(m.main())`
  ], { encoding: 'utf8', timeout: 60000 }); return name; } catch { return 'FAILED'; }
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect')), { once: true });
  });
  await req('version', { name: 'tt-freeze', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) await req('cpu.resume'); } catch {}

  let n = 0;
  console.log(`# phase at start: ${await phase()}   shot ${await shot(`${TAG}-000`)}`);

  for (let r = 0; r < REPEAT; r++) {
    for (const button of PLAN) {
      n++;
      try { await req('cpu.resume'); } catch {}
      await req('input.buttons.press', { button });
      await sleep(SETTLE);
      // FREEZE so the menu cannot scroll away before the capture.
      // ⚠️ cpu.stepping is an ACTION that does NOT send a response -- awaiting it throws a
      // timeout even though it worked. Fire-and-forget it, then poll cpu.status to confirm.
      sock.send(JSON.stringify({ event: 'cpu.stepping', ticket: String(ticket++), stepping: true }));
      await sleep(250);
      let frozen = false;
      try { const st = await req('cpu.status'); frozen = !!st.stepping; } catch {}
      const s = await shot(`${TAG}-${String(n).padStart(3, '0')}-${button}`);
      // unfreeze; cpu.resume is likewise an action with no response
      sock.send(JSON.stringify({ event: 'cpu.resume', ticket: String(ticket++) }));
      await sleep(300);
      console.log(`# step ${String(n).padStart(3, '0')} press ${button.padEnd(9)} frozen=${frozen}  shot ${s}`);
    }
  }
  console.log('# done.');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
