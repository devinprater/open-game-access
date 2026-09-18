#!/usr/bin/env node
// psp-tt-live.mjs — ADVANCE until the game is genuinely in a live battle, THEN measure.
//
// WHY THIS EXISTS: seven separate conclusions this session were invalid because the game was
// parked on a result screen / cutscene while I measured. Every script so far assumed "we are in
// a battle" without checking. This one refuses to measure until liveness is PROVEN by a control
// signal, which is the fix for the dominant failure mode on this project.
//
// LIVENESS DEFINITION (measured, not assumed): a live battle has fighter HP that CHANGES between
// two samples taken a short time apart, because regeneration and combat both move it. On a result
// screen, cutscene, or menu, HP is frozen.
//
// Modes:
//   --ensure            advance until live (default mode)
//   --idle [--secs N]   ensure live, then press NOTHING and report which slot's HP falls ->
//                       that slot is the PLAYER's fighter (the CPU attacks the player)
//   --sweep             ensure live, then for each slot: top every OTHER slot to max and hold the
//                       candidate at 0, and report the HP outcome (used to find the enemy win slot)
//   --topup SLOT        ensure live, then keep every slot EXCEPT SLOT at full HP
//
// Usage:
//   node scripts/psp-tt-live.mjs --ensure
//   node scripts/psp-tt-live.mjs --idle --secs 25
//   node scripts/psp-tt-live.mjs --sweep --secs 18

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const SLOTS = [0, 1, 2, 3].map(i => ({ i, name: 's' + i, base: BASE + i * STRIDE }));
const HP_CUR = 0x14, HP_MAX = 0x18;

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
async function hpAll() {
  const out = [];
  for (const s of SLOTS) out.push({ ...s, cur: await rd(s.base + HP_CUR), max: await rd(s.base + HP_MAX) });
  return out;
}
const fmt = (h) => h.map(x => `${x.name} ${String(x.cur).padStart(6)}/${x.max}`).join('  ');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

// liveness: does any slot's HP change across a short interval?
async function isLive(gapMs = 1200) {
  const a = await hpAll();
  await sleep(gapMs);
  const b = await hpAll();
  return a.some((x, i) => x.cur !== b[i].cur);
}

async function ensureLive() {
  console.log('# checking liveness (HP must change while the game runs)...');
  for (let attempt = 1; attempt <= 40; attempt++) {
    if (await isLive(1200)) {
      console.log(`# LIVE after ${attempt - 1} advance press(es)`);
      return true;
    }
    await req('input.buttons.press', { button: 'cross' });
    await sleep(1200);
  }
  console.log('# could NOT establish a live battle after 40 presses');
  return false;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-live', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  const ok = await ensureLive();
  console.log('# state: ' + fmt(await hpAll()));
  if (!ok) { sock.close(); return; }

  if (has('--idle')) {
    const secs = Number(val('--secs', 25));
    console.log(`# IDLE for ${secs}s - NO input. Whichever slot falls is the PLAYER's fighter.`);
    const start = await hpAll();
    let prev = fmt(start);
    const t0 = Date.now();
    while ((Date.now() - t0) / 1000 < secs) {
      await sleep(1500);
      const cur = await hpAll();
      const l = fmt(cur);
      if (l !== prev) { console.log(`  [${((Date.now() - t0) / 1000).toFixed(1)}s] ${l}`); prev = l; }
    }
    const end = await hpAll();
    console.log('# start: ' + fmt(start));
    console.log('# end  : ' + fmt(end));
    const drops = end.map((e, i) => ({ name: e.name, d: (start[i].cur || 0) - (e.cur || 0) }));
    drops.sort((a, b) => b.d - a.d);
    console.log('# drops: ' + drops.map(d => `${d.name} -${d.d}`).join(', '));
    console.log('# => PLAYER fighter is most likely: ' + (drops[0].d > 0 ? drops[0].name : '(no drop: battle may not be live)'));
    sock.close();
    return;
  }

  if (has('--sweep') || has('--topup')) {
    const secs = Number(val('--secs', 18));
    let candidates = [0, 1, 2, 3];
    if (has('--topup')) candidates = [Number(val('--topup', 2))];
    const results = [];
    for (const c of candidates) {
      console.log(`\n# === candidate ${c}: keep all others at max, hold s${c} at 0 for ${secs}s ===`);
      const t0 = Date.now();
      while ((Date.now() - t0) / 1000 < secs) {
        for (const s of SLOTS) {
          const max = await rd(s.base + HP_MAX);
          if (s.i === c) await wr(s.base + HP_CUR, 0);
          else if (max) await wr(s.base + HP_CUR, max);
        }
        await sleep(180);
      }
      const after = await hpAll();
      console.log(`# after candidate ${c}: ` + fmt(after));
      results.push({ c, after: after.map(x => x.cur) });
      // if every slot except a "dead" player is frozen, the fight likely ended
      const frozen = !(await isLive(1200));
      console.log(`# battle still live? ${!frozen}  (frozen => likely ENDED, check the screen)`);
      if (frozen) { console.log(`# STOPPING: candidate ${c} appears to have ended the battle.`); break; }
    }
    console.log('\n# summary:');
    for (const r of results) console.log(`  candidate ${r.c}: [${r.after.join(', ')}]`);
    sock.close();
    return;
  }

  // default: just report and exit
  console.log('# liveness confirmed; use --idle or --sweep to act.');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
