#!/usr/bin/env node
// psp-tt-hp.mjs — LIVE reader for DBZ: Tenkaichi Tag Team battle HP.
//
// WHY THIS EXISTS: the fighter struct was found by diffing RAM dumps, which is indirect.
// This script reads the addresses straight out of the RUNNING game over PPSSPP's debugger,
// so the values can be watched changing frame by frame. That is the difference between
// "an address derived from dumps" and "an address confirmed live".
//
// VERIFIED MAP (see docs/reverse-engineering/dbz-tenkaichi-tag-team.md):
//   1P struct 0x0973CEE0   2P struct 0x0973E970   stride 0x1A90 (6800)
//   +0x14 HP current   +0x18 HP max   +0x20 gauge   +0x24 gauge max
// Ground truth: losing the battle printed "Health 0%" and HP read exactly 0/30000.
//
// Usage:
//   node scripts/psp-tt-hp.mjs                 # print HP snapshot
//   node scripts/psp-tt-hp.mjs --watch         # poll and print only on change
//   node scripts/psp-tt-hp.mjs --watch --ms 250
//
// PPSSPP's debugger allows ONE client at a time — do not run a presser concurrently.

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;

const argv = process.argv.slice(2);
const watch = argv.includes('--watch');
const msIdx = argv.indexOf('--ms');
const MS = msIdx >= 0 ? Number(argv[msIdx + 1]) : 250;
const asJson = argv.includes('--json');

// threshold for announcing a change, in HP points — avoids speaking every frame of a slow drain
const thIdx = argv.indexOf('--threshold');
const THRESHOLD = thIdx >= 0 ? Number(argv[thIdx + 1]) : 1000;

// fighter table: struct base, then field offsets.
// TAG TEAM: there are FOUR fighter slots, stride 0x1A90. In battle, slots 0/1 are the
// tag partners (commonly 0/empty) and slots 2/3 hold the active fighters -- verified live
// in story mode: slot2 28690/30000 (95.6%), slot3 12840/30000 (42.8%).
// An earlier version of this script read only slots 0/1 and therefore reported 0 HP while
// the HUD showed a full health bar. Read all four and label them.
const HP_CUR = 0x14, HP_MAX = 0x18, G2_CUR = 0x20, G2_MAX = 0x24;
const BASE_1P = 0x0973CEE0;
const STRIDE = 0x1A90;
const FIGHTERS = [
  { name: '0', base: BASE_1P },
  { name: '1', base: BASE_1P + STRIDE },
  { name: '2', base: BASE_1P + STRIDE * 2 },
  { name: '3', base: BASE_1P + STRIDE * 3 },
];

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1;
const pending = new Map();

sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket);
    pending.delete(m.ticket);
    if (m.event === 'error') reject(new Error(m.message));
    else resolve(m);
  }
});

function req(event, extra = {}, timeout = 8000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++);
    pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}

const hex = (n) => '0x' + n.toString(16).toUpperCase().padStart(8, '0');

// read a u32 at an absolute address — the debugger reports the value as a hex STRING
async function readU32(addr) {
  const r = await req('memory.read_u32', { address: hex(addr) });
  const raw = r.value !== undefined ? r.value : r.data;
  if (raw === undefined || raw === null) return null;
  return typeof raw === 'number' ? raw >>> 0 : (parseInt(String(raw), 16) >>> 0);
}

async function snapshot() {
  const out = [];
  for (const f of FIGHTERS) {
    const cur = await readU32(f.base + HP_CUR);
    const max = await readU32(f.base + HP_MAX);
    const g2c = await readU32(f.base + G2_CUR);
    const g2m = await readU32(f.base + G2_MAX);
    out.push({ name: f.name, cur, max, g2c, g2m });
  }
  return out;
}

function fmt(snap) {
  return snap.map(s => {
    const pct = (s.max && s.cur !== null) ? ((s.cur / s.max) * 100).toFixed(1) + '%' : 'n/a';
    return `${s.name} HP ${String(s.cur).padStart(6)}/${s.max} (${pct.padStart(6)})  gauge ${String(s.g2c).padStart(6)}/${s.g2m}`;
  }).join('\n');
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to PPSSPP debugger on ' + URL + ' (is PPSSPP running with the debugger enabled, and no other client attached?)')), { once: true });
  });

  // identity check — make sure we are reading the RIGHT game
  let game = null;
  try {
    await req('version', { name: 'tt-hp', version: '1' });
    const g = await req('game.status');
    game = g.game || g;
  } catch (e) { /* older builds may not answer */ }

  if (game && game.id) {
    console.log(`# game ${game.id} "${game.title}"`);
    if (game.id !== 'ULUS10537') {
      console.log(`# WARNING: expected ULUS10537 (DBZ: Tenkaichi Tag Team). Addresses will be wrong for ${game.id}.`);
    }
  }

  if (!watch) {
    if (asJson) { console.log(JSON.stringify(await snapshot())); }
    else { console.log(fmt(await snapshot())); }
    sock.close();
    return;
  }

  console.log(`# watching HP every ${MS} ms (Ctrl-C to stop)`);
  console.log(`# announcing a change of >= ${THRESHOLD} HP`);
  let prevSnap = null;
  let ticks = 0;
  for (;;) {
    try {
      const snap = await snapshot();
      ticks++;
      if (ticks % 40 === 0) process.stdout.write('.');  // heartbeat so a static screen is distinguishable from a hang

      // announce only meaningful changes, so a slow drain does not flood the channel
      const notable = !prevSnap || snap.some((s, i) => {
        const p = prevSnap[i];
        return p && (Math.abs((s.cur ?? 0) - (p.cur ?? 0)) >= THRESHOLD);
      });
      if (notable) {
        const t = new Date().toISOString().slice(11, 19);
        if (asJson) console.log(JSON.stringify({ t, fighters: snap }));
        else console.log(`[${t}] ${fmt(snap).replace(/\n/g, '\n            ')}`);
        prevSnap = snap;
      }
    } catch (e) {
      console.log(`# read error: ${e.message}`);
    }
    await new Promise(r => setTimeout(r, MS));
  }
}

main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
