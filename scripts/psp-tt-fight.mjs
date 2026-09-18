#!/usr/bin/env node
// psp-tt-fight.mjs — attack AND read HP in ONE debugger connection.
//
// WHY: PPSSPP's debugger allows only ONE client at a time, so a separate "presser" and
// "reader" process cannot coexist. This script does both over a single socket, which is the
// only way to prove live that HP responds to input.
//
// It prints an HP snapshot, presses the attack button, prints again, and repeats — so any
// change is attributable to a specific press. That is the live confirmation the dumped
// addresses were missing.
//
// Usage:
//   node scripts/psp-tt-fight.mjs                 # 12 rounds of cross
//   node scripts/psp-tt-fight.mjs --rounds 20 --button cross --gap 700

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;

const argv = process.argv.slice(2);
const val = (flag, dflt) => { const i = argv.indexOf(flag); return i >= 0 ? argv[i + 1] : dflt; };
const ROUNDS = Number(val('--rounds', 12));
const BUTTON = val('--button', 'cross');
const GAP = Number(val('--gap', 700));
// --idle: press NO buttons; the CPU attacks on its own, so whichever slot's HP FALLS is the
// PLAYER's fighter. This is the reliable way to tell the sides apart, because pressing buttons
// yourself cannot distinguish "I moved" from "the enemy attacked".
const IDLE = argv.includes('--idle');

const HP_CUR = 0x14, HP_MAX = 0x18, G2_CUR = 0x20, G2_MAX = 0x24;
const BASE_1P = 0x0973CEE0, STRIDE = 0x1A90;
// TAG TEAM: FOUR slots. Slots can legitimately be 0 (empty partners), and which pair is
// "active" is NOT fixed -- do not hardcode. Report all four.
const FIGHTERS = [0, 1, 2, 3].map(i => ({ name: 's' + i, base: BASE_1P + STRIDE * i }));

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
async function readU32(addr) {
  const r = await req('memory.read_u32', { address: hex(addr) });
  const raw = r.value !== undefined ? r.value : r.data;
  if (raw === undefined || raw === null) return null;
  return typeof raw === 'number' ? raw >>> 0 : (parseInt(String(raw), 16) >>> 0);
}
async function snapshot() {
  const out = [];
  for (const f of FIGHTERS) {
    out.push({
      name: f.name,
      cur: await readU32(f.base + HP_CUR),
      max: await readU32(f.base + HP_MAX),
      g2c: await readU32(f.base + G2_CUR),
    });
  }
  return out;
}
const line = (s) => s.map(x => `${x.name} ${String(x.cur).padStart(6)}/${x.max}`).join('   ');

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-fight', version: '1' });

  // never start from a frozen emulator
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# was paused/stepping -> cpu.resume'); } } catch {}

  const first = await snapshot();
  console.log(`# start            ${line(first)}`);
  let last = line(first);

  for (let i = 1; i <= ROUNDS; i++) {
    if (!IDLE) await req('input.buttons.press', { button: BUTTON });
    await new Promise(r => setTimeout(r, GAP));
    const s = await snapshot();
    const l = line(s);
    const mark = l !== last ? '  <== CHANGED' : '';
    console.log(`# ${IDLE ? 'idle ' : BUTTON} ${String(i).padStart(2)}       ${l}${mark}`);
    last = l;
  }

  const end = await snapshot();
  console.log('# end              ' + line(end));
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
