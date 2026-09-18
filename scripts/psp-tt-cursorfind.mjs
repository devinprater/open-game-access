#!/usr/bin/env node
// psp-tt-cursorfind.mjs — find the CURRENT-LINE CURSOR (the last blocker for a live reader).
//
// THE PROBLEM
//   Tag Team keeps each scene's story text resident in RAM as UTF-16LE (verified: field 2365 lines,
//   menu 2037), and the corpus is CONSTANT within a scene. So the line actually on screen is chosen
//   by a cursor/index somewhere ELSE. Without it, a reader can dump text but cannot narrate.
//
// THE EXPERIMENT
//   Snapshot ALL user RAM, advance exactly ONE dialogue line, snapshot again, and report every u32 in
//   a plausible id/index range that changed. That is the direct measurement of "what advances with
//   the text", rather than guessing.
//
//   Guards learned the hard way in this project:
//     * verify the phase BEFORE trusting anything (an advance in a menu proves nothing)
//     * the corpus itself is static -> exclude addresses inside the text regions (they do not move)
//     * a value that advances with your PRESSES may just be counting presses -> we press ONCE and
//       report the change, and we also run an IDLE control (same elapsed time, no press) so that
//       pure animation/counters can be subtracted
//
// USAGE
//   node scripts/psp-tt-cursorfind.mjs                  # idle control + one press
//   node scripts/psp-tt-cursorfind.mjs --press cross
//   node scripts/psp-tt-cursorfind.mjs --wait 6000

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const PRESS = val('--press', 'cross');
const WAIT = Number(val('--wait', 5000));
const ID_MIN = Number(val('--id-min', 100));
const ID_MAX = Number(val('--id-max', 100000));

const RAM_LO = 0x08800000, RAM_HI = 0x0A000000, CHUNK = 0x100000;
// Text regions: values INSIDE these do not change within a scene, so they can only be noise here.
const TEXT_REGIONS = [[0x08FB0000, 0x08FE0000], [0x08C70000, 0x08C90000]];

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 25000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function fullRAM() {
  const parts = [];
  for (let a = RAM_LO; a < RAM_HI; a += CHUNK) {
    const r = await req('memory.read', { address: hex(a), size: CHUNK });
    parts.push(Buffer.from(r.base64 || '', 'base64'));
  }
  return Buffer.concat(parts);
}
function inTextRegion(addr) {
  for (const [a, b] of TEXT_REGIONS) if (addr >= a && addr < b) return true;
  return false;
}
// Phase check: fighter structs. 0/0 => menu/cutscene; 0x3F800000 => menu sentinel.
async function phase() {
  const B = 0x0973CEE0, ST = 0x1A90;
  const out = [];
  for (const i of [0, 1, 2, 3]) {
    const r = await req('memory.read_u32', { address: hex(B + i * ST + 0x18) });   // +0x18 max HP
    const v = r.value !== undefined ? r.value : r.data;
    out.push(typeof v === 'number' ? v >>> 0 : parseInt(String(v), 16) >>> 0);
  }
  return out;
}
function decode(b) { return b.toString('utf16le').replace(/[^\x20-\x7E]/g, '.'); }

// Collect u32 changes in the id range, excluding the static text regions.
function diffIds(A, B) {
  const hits = [];
  for (let o = 0; o + 4 <= A.length; o += 4) {
    const a = A.readUInt32LE(o), b = B.readUInt32LE(o);
    if (a === b) continue;
    const addr = RAM_LO + o;
    if (inTextRegion(addr)) continue;                       // static corpus -> not the cursor
    // ⚠️ Require BOTH endpoints in range. Accepting "either" let animating FLOAT bit-patterns through:
    // e.g. `160 -> 1061159513` where 1061159513 == 0x3F400000 == float 0.75. A genuine id cursor
    // moves id -> id (761 -> 762), so both ends must look like ids. Also reject values whose bytes
    // form a plausible float in a sane range, which is what animating data looks like.
    const aIn = a >= ID_MIN && a <= ID_MAX, bIn = b >= ID_MIN && b <= ID_MAX;
    if (!aIn || !bIn) continue;
    const looksFloat = (v) => {
      const buf = Buffer.alloc(4); buf.writeUInt32LE(v >>> 0, 0);
      const f = buf.readFloatLE(0);
      return Number.isFinite(f) && Math.abs(f) > 0.0001 && Math.abs(f) < 1e6;
    };
    if (looksFloat(a) || looksFloat(b)) continue;      // animating transform data, not an id
    hits.push({ addr, a, b });
  }
  return hits;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-cursorfind', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  const ph = await phase();
  console.log('# phase check (+0x18 max HP per slot): ' + ph.join(', '));
  if (ph.some((v) => v === 0x3F800000)) console.log('#   -> MENU sentinel (float 1.0)');
  else if (ph.every((v) => v === 0)) console.log('#   -> all 0/0: boot/title/cutscene (or not yet populated)');
  else console.log('#   -> battle/field-like values present');

  console.log(`\n# IDLE CONTROL: two snapshots ${WAIT}ms apart, NO press`);
  const A0 = await fullRAM();
  await sleep(WAIT);
  const A1 = await fullRAM();
  const idleHits = diffIds(A0, A1);
  const idleSet = new Set(idleHits.map((h) => h.addr));
  console.log(`#   idle: ${idleHits.length} id-range value(s) changed with NO input`);
  for (const h of idleHits.slice(0, 12)) console.log(`     ${hex(h.addr)}  ${h.a} -> ${h.b}`);

  console.log(`\n# ONE PRESS of "${PRESS}", then re-snapshot`);
  const B0 = await fullRAM();
  try { await req('input.buttons.press', { button: PRESS, frames: 5 }); } catch (e) { console.log('#   press error: ' + e.message); }
  await sleep(1200);
  const B1 = await fullRAM();
  const allHits = diffIds(B0, B1);
  console.log(`#   press: ${allHits.length} id-range value(s) changed`);

  // Subtract the idle control: what changed ONLY because of the press.
  const pressOnly = allHits.filter((h) => !idleSet.has(h.addr));
  console.log(`\n=== CHANGED WITH THE PRESS ONLY (idle noise subtracted): ${pressOnly.length} ===`);
  const shown = pressOnly.slice(0, 40);
  for (const h of shown) {
    // Show the surrounding memory as both text and numbers: a cursor often sits beside a pointer.
    let ctx = '';
    try {
      const r = await req('memory.read', { address: hex((h.addr - 32) >>> 0), size: 96 });
      ctx = decode(Buffer.from(r.base64 || '', 'base64'));
    } catch {}
    console.log(`  ${hex(h.addr)}  ${h.a} -> ${h.b}`);
    if (ctx.replace(/\./g, '').trim().length > 4) console.log(`      text nearby: "${ctx.slice(0, 88)}"`);
  }

  if (pressOnly.length > 0) {
    console.log('\n# CANDIDATES: the cursor is among these. Re-run and confirm the SAME address');
    console.log('# advances again (a counter or animation will not reproduce predictably).');
  } else {
    console.log('\n# No id-range change attributable to the press.');
    console.log('#   -> either this state does not advance text, or the cursor is outside the id range');
    console.log('#      (try --id-min 0 --id-max 5000000).');
  }
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
