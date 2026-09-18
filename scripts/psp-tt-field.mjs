#!/usr/bin/env node
// psp-tt-field.mjs — drive the ANALOG STICK and read the result, in ONE debugger connection.
//
// WHY: story mode's field phase ("Find Gohan") is flown with the analog stick, and PPSSPP allows
// only ONE debugger client at a time — so the stick driver and the memory reader must share a
// socket. This is the tool that proves stick input reaches the game.
//
// CORRECT PPSSPP EVENT NAMES (this was the bug that made analog look unsupported):
//   request  : input.analog.send   {x, y, stick?}         x,y in -1.0..1.0
//              input.buttons.send  {buttons:{name:bool}} continuous hold (state)
//              input.buttons.press {button, duration?}   momentary
//   broadcast: input.analog        (an event you LISTEN to, NOT a request)
//              input.buttons
// Sending the broadcast name as a request fails with "unknown event" — which is exactly what
// made me wrongly conclude analog input was impossible.
//
// Usage:
//   node scripts/psp-tt-field.mjs --probe            # is the stick accepted? read pos before/after
//   node scripts/psp-tt-field.mjs --drive 1 0 --ms 1500
//   node scripts/psp-tt-field.mjs --circle           # 8 directions, read position at each
//   node scripts/psp-tt-field.mjs --watch-pos        # poll the position field

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

// fighter slots (tag team: 4 slots, stride 0x1A90) and the float position triple at +0x0B0
const BASE = 0x0973CEE0, STRIDE = 0x1A90;
const POS = 0xB0;
const SLOTS = [0, 1, 2, 3].map(i => ({ name: 'slot' + i, base: BASE + i * STRIDE }));

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
async function readU32(a) {
  const r = await req('memory.read_u32', { address: hex(a) });
  const v = r.value !== undefined ? r.value : r.data;
  if (v === undefined || v === null) return null;
  return typeof v === 'number' ? v >>> 0 : (parseInt(String(v), 16) >>> 0);
}
async function readF32(a) {
  const u = await readU32(a);
  if (u === null) return null;
  const b = new ArrayBuffer(4); const dv = new DataView(b);
  dv.setUint32(0, u >>> 0, true);
  return dv.getFloat32(0, true);
}
async function pos(slot) {
  return {
    x: await readF32(slot.base + POS + 0),
    y: await readF32(slot.base + POS + 4),
    z: await readF32(slot.base + POS + 8),
  };
}
async function hp(slot) {
  return { cur: await readU32(slot.base + 0x14), max: await readU32(slot.base + 0x18) };
}
const fp = (p) => p && p.x !== null ? `(${p.x.toFixed(2)}, ${p.y.toFixed(2)}, ${p.z.toFixed(2)})` : '(null)';

// which slots are live fighters? maxHP != 0 is the robust test (do NOT hardcode 0/1 or 2/3)
async function activeSlots() {
  const out = [];
  for (const s of SLOTS) {
    const h = await hp(s);
    if (h.max && h.max > 0) out.push({ slot: s, hp: h });
  }
  return out;
}

async function stick(x, y, ms, stickName = 'left') {
  // hold: send the position repeatedly, since a single send may be consumed as one frame's input
  const until = Date.now() + ms;
  while (Date.now() < until) {
    await req('input.analog.send', { x, y, stick: stickName });
    await new Promise(r => setTimeout(r, Math.min(120, ms)));
  }
  await req('input.analog.send', { x: 0, y: 0, stick: stickName });  // always recentre
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-field', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# was paused -> cpu.resume'); } } catch {}

  console.log('# game:', JSON.stringify((await req('game.status')).game || {}));

  const act = await activeSlots();
  console.log('# active fighter slots (maxHP>0): ' + act.map(a => `${a.slot.name} HP ${a.hp.cur}/${a.hp.max}`).join('  '));
  if (!act.length) console.log('# WARNING: no slot has maxHP>0 — not in a battle/field with a fighter.');

  if (has('--watch-pos')) {
    console.log('# watching position (Ctrl-C to stop)');
    let prev = '';
    for (;;) {
      const parts = [];
      for (const a of act) parts.push(`${a.slot.name} ${fp(await pos(a.slot))}`);
      const line = parts.join('   ');
      if (line !== prev) { console.log(`[${new Date().toISOString().slice(11, 19)}] ${line}`); prev = line; }
      else process.stdout.write('.');
      await new Promise(r => setTimeout(r, Number(val('--ms', 400))));
    }
  }

  if (has('--circle')) {
    const dirs = [[0, -1, 'up'], [1, -1, 'up-right'], [1, 0, 'right'], [1, 1, 'down-right'],
                  [0, 1, 'down'], [-1, 1, 'down-left'], [-1, 0, 'left'], [-1, -1, 'up-left']];
    for (const [x, y, nm] of dirs) {
      const before = {};
      for (const a of act) before[a.slot.name] = await pos(a.slot);
      await stick(x, y, Number(val('--ms', 900)));
      const after = {};
      for (const a of act) after[a.slot.name] = await pos(a.slot);
      console.log(`# ${nm.padEnd(11)} (x=${x} y=${y})`);
      for (const a of act) {
        const b = before[a.slot.name], f = after[a.slot.name];
        const d = (b && f) ? Math.hypot(f.x - b.x, f.y - b.y, f.z - b.z) : NaN;
        console.log(`    ${a.slot.name}  ${fp(b)} -> ${fp(f)}   moved ${isNaN(d) ? '?' : d.toFixed(2)}`);
      }
    }
    sock.close();
    return;
  }

  if (has('--drive')) {
    const i = argv.indexOf('--drive');
    const x = Number(argv[i + 1]), y = Number(argv[i + 2]);
    const before = {}; for (const a of act) before[a.slot.name] = await pos(a.slot);
    console.log(`# holding stick (x=${x}, y=${y}) for ${val('--ms', 1500)} ms`);
    await stick(x, y, Number(val('--ms', 1500)));
    for (const a of act) {
      const f = await pos(a.slot);
      const b = before[a.slot.name];
      const d = Math.hypot(f.x - b.x, f.y - b.y, f.z - b.z);
      console.log(`    ${a.slot.name}  ${fp(b)} -> ${fp(f)}   moved ${d.toFixed(2)}`);
    }
    sock.close();
    return;
  }

  // default: probe acceptance + report current position
  console.log('# probing stick acceptance...');
  for (const [x, y, nm] of [[1, 0, 'right'], [0, -1, 'up'], [0, 0, 'centre']]) {
    try { await req('input.analog.send', { x, y, stick: 'left' }); console.log(`  accepted  x=${x} y=${y} (${nm})`); }
    catch (e) { console.log(`  REJECTED  x=${x} y=${y} (${nm}): ${e.message}`); }
  }
  for (const a of act) console.log(`  ${a.slot.name} position ${fp(await pos(a.slot))}`);
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
