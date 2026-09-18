#!/usr/bin/env node
// psp-tt-livewatch.mjs — passively watch for the transition into a LIVE field/battle.
//
// WHY: fighter structs are EMPTY (0/0) in menus and cutscenes, so searches for character ids or
// position cannot run there. Reporting from such a state would be the "data cannot exist here"
// mistake. This watcher simply WAITS for the state to become live, then reports and screenshots.
//
// It is read-only (no writes, no input) so the user can drive PPSSPP by hand at the same time --
// reads do not consume the single debugger client's input, and we never press buttons here.
//
// Usage:
//   node scripts/psp-tt-livewatch.mjs --minutes 10
//   node scripts/psp-tt-livewatch.mjs --minutes 10 --shot-every 60

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const num = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? Number(argv[i + 1]) : d; };

const MINUTES = num('--minutes', 10);
const SHOT_EVERY = num('--shot-every', 0);   // seconds; 0 = only on state change
const POLL_MS = num('--poll-ms', 15000);

const SLOTS = [0, 1, 2, 3].map((i) => 0x0973CEE0 + i * 0x1A90);
const OFF_HP = 0x14, OFF_MAX = 0x18, OFF_POS = 0x0B0;

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 12000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function u32(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  return typeof v === 'number' ? v >>> 0 : parseInt(String(v), 16) >>> 0;
}
async function f32(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  const raw = typeof v === 'number' ? v >>> 0 : parseInt(String(v), 16) >>> 0;
  const b = Buffer.alloc(4); b.writeUInt32LE(raw, 0); return b.readFloatLE(0);
}

// 0/0 -> empty slot (menu/cutscene/boot); float 1.0 (0x3F800000) -> menu sentinel; else battle/field
async function snapshot() {
  const slots = [];
  for (const base of SLOTS) {
    const cur = await u32(base + OFF_HP);
    const max = await u32(base + OFF_MAX);
    slots.push({ cur, max });
  }
  const populated = slots.some((s) => s.max > 0 && s.max <= 100000 && s.cur <= s.max && s.cur > 0);
  const sentinel = slots.some((s) => s.cur === 0x3F800000);
  return { slots, populated, sentinel };
}

function describe(s) {
  return s.slots.map((x, i) => `s${i}=${x.cur}/${x.max}`).join('  ');
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-livewatch', version: '1' });
  try {
    const st = await req('cpu.status');
    if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# unfroze (cpu.resume)'); }
  } catch {}

  const deadline = Date.now() + MINUTES * 60000;
  let lastDesc = null, lastShot = 0, liveSeen = false, samples = 0;

  console.log(`# passive watch for ${MINUTES} min, polling every ${POLL_MS / 1000}s`);
  console.log('# read-only: no input, no writes. Drive PPSSPP by hand; this only observes.');
  console.log('# waiting for the field/battle to become LIVE (slots populated + values moving)...');

  while (Date.now() < deadline) {
    let s;
    try { s = await snapshot(); } catch (e) { console.log(`  ! read failed: ${e.message}`); await sleep(POLL_MS); continue; }
    samples++;
    const d = describe(s);

    if (d !== lastDesc) {
      const kind = s.populated ? 'POPULATED' : (s.sentinel ? 'MENU (float 1.0 sentinel)' : 'EMPTY (0/0: boot/title/cutscene)');
      console.log(`[${new Date().toLocaleTimeString()}] ${kind}`);
      console.log(`        ${d}`);
      lastDesc = d;
    }

    if (s.populated && !liveSeen) {
      liveSeen = true;
      console.log('\n# ===== FIELD/BATTLE IS LIVE =====');
      console.log(`# ${d}`);
      console.log('# reading position now...');
      for (let i = 0; i < 4; i++) {
        const p = [await f32(SLOTS[i] + OFF_POS), await f32(SLOTS[i] + OFF_POS + 4), await f32(SLOTS[i] + OFF_POS + 8)];
        if (s.slots[i].max > 0) console.log(`  slot${i} pos (${p.map((v) => v.toFixed(2)).join(', ')})`);
      }
      console.log('\n# READY. While you stay in the field I can search for character ids and position.');
    }

    if (SHOT_EVERY > 0 && Date.now() - lastShot > SHOT_EVERY * 1000) {
      lastShot = Date.now();
      console.log(`# (t=${new Date().toLocaleTimeString()}) ${d}`);
    }
    await sleep(POLL_MS);
  }
  console.log(`\n# watch ended after ${samples} samples. live-seen=${liveSeen}`);
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
