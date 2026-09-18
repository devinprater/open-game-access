#!/usr/bin/env node
// psp-tt-advance.mjs — walk the story forward to the FIELD, winning battles by HP when needed.
//
// WHY: reaching story mode's open field ("Find Gohan") requires clearing the story battles first.
// Doing that by hand cost many cycles. This automates it in one connection:
//   - if a battle is live, zero the ENEMY team's HP (slots 2/3) and keep the PLAYER team (0/1)
//     full -> the battle ends with a win
//   - otherwise press cross to advance dialogue / menus
//   - screenshot periodically and record every distinct screen so nothing is skipped
//
// VERIFIED FACTS this relies on (see docs/reverse-engineering/dbz-tenkaichi-tag-team.md):
//   struct base 0x0973CEE0, stride 0x1A90; +0x14 curHP, +0x18 maxHP
//   slots 0/1 = PLAYER team, slots 2/3 = ENEMY team   (confirmed by producing a WIN)
//   liveness = HP changes between two samples
//
// Usage: node scripts/psp-tt-advance.mjs --iters 60 [--shot-every 4] [--shot-dir <dir>]

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const ITERS = Number(val('--iters', 60));
const SHOT_EVERY = Number(val('--shot-every', 4));
const SHOT_TAG = val('--shot-tag', 'adv');

// Screenshots do NOT need the debugger socket: scripts/psp-shot.py grabs the WINDOW via
// PrintWindow. So a socket-holding process CAN screenshot by spawning it. (Earlier the loop
// logged "#SHOT" markers without producing images -- an unverifiable run.)
const SHOT_DIR = val('--shot-dir', process.env.LOCALAPPDATA + '\\Temp\\psp-probe');
let shotSeq = 0;
async function shot() {
  try {
    const { execFileSync } = await import('node:child_process');
    shotSeq++;
    const out = `${SHOT_DIR}\\${SHOT_TAG}-${String(shotSeq).padStart(3, '0')}.png`;
    const r = execFileSync('python', ['-c',
      'import sys;sys.path.insert(0,r"' + process.cwd().replace(/\\/g, '/') + '/scripts");import importlib.util as u;' +
      's=u.spec_from_file_location("ps",r"' + process.cwd().replace(/\\/g, '/') + '/scripts/psp-shot.py");m=u.module_from_spec(s);s.loader.exec_module(m);' +
      'sys.argv=["psp-shot.py",r"' + out + '"];raise SystemExit(m.main())'
    ], { encoding: 'utf8', timeout: 60000 });
    console.log(`#SHOT ${out}  ${String(r).trim().split('\n')[0]}`);
  } catch (e) {
    console.log(`#SHOT-FAILED ${e.message.split('\n')[0]}`);
  }
}

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const HP_CUR = 0x14, HP_MAX = 0x18;
const PLAYER = [0, 1], ENEMY = [2, 3];

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
const wr = (a, v) => req('memory.write_u32', { address: hex(a), value: v });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const hpOf = async (i) => ({ cur: await rd(BASE + i * STRIDE + HP_CUR), max: await rd(BASE + i * STRIDE + HP_MAX) });

// a battle is live if any PLAYER or ENEMY slot's HP moves on its own
async function battleLive(gap = 1100) {
  const a = [], b = [];
  for (const i of [0, 1, 2, 3]) a.push((await hpOf(i)).cur);
  await sleep(gap);
  for (const i of [0, 1, 2, 3]) b.push((await hpOf(i)).cur);
  return a.some((x, k) => x !== b[k]);
}

async function winBattle(secs) {
  // hold enemy at 0, player at max, until the battle stops being live
  const t0 = Date.now();
  while ((Date.now() - t0) / 1000 < secs) {
    for (const i of PLAYER) { const m = (await hpOf(i)).max; if (m) await wr(BASE + i * STRIDE + HP_CUR, m); }
    for (const i of ENEMY)  { const m = (await hpOf(i)).max; if (m) await wr(BASE + i * STRIDE + HP_CUR, 0); }
    await sleep(200);
    if (!(await battleLive(700))) return true;   // battle ended
  }
  return false;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-advance', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  let battles = 0, advances = 0;
  for (let it = 1; it <= ITERS; it++) {
    if (await battleLive(1100)) {
      battles++;
      console.log(`# [${it}] battle live -> winning by HP (battle #${battles})`);
      const done = await winBattle(30);
      console.log(`# [${it}] battle ${done ? 'ENDED' : 'still going after 30s'}`);
    } else {
      // ⚠️ DO NOT mash cross blindly. On the RESULT screen a burst of crosses can be consumed by
      // the following dialogue and then spill into the NEXT battle's start-up, and on menus it
      // can select unintended entries. Press ONCE, then wait and re-check liveness: if a battle
      // has started, the loop will win it on the next iteration instead of pressing into it.
      advances++;
      await req('input.buttons.press', { button: 'cross' });
      await sleep(600);
      // if this press started a battle, stop pressing immediately
      if (await battleLive(900)) {
        battles++;
        console.log(`# [${it}] press started a battle -> winning by HP`);
        await winBattle(30);
      }
    }
    await sleep(900);
    if (it % SHOT_EVERY === 0) await shot();
  }
  const final = [];
  for (const i of [0, 1, 2, 3]) { const h = await hpOf(i); final.push(`s${i} ${h.cur}/${h.max}`); }
  console.log(`# done: ${battles} battle-win(s), ${advances} cross advance(s). final: ${final.join('  ')}`);
  console.log('# if HP are static and no battle is live, the game is likely on the FIELD or a menu.');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
