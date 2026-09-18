#!/usr/bin/env node
// psp-tt-curline.mjs — find the pointer/cursor that says WHICH line of text is current.
//
// WHY: the whole scene script is resident in RAM as UTF-16LE (verified: the opening narration, the
// mission tutorial, and the ordered objective list). But a blob holds MANY lines, so a reader still
// needs to know which one is on screen NOW. That is a cursor: a pointer into the blob, or an index.
//
// METHOD: scan all user RAM for u32 values that POINT INTO a known text region. For each hit,
// decode the UTF-16 text at the target and show it. If the game keeps a "current line" pointer, one
// of these hits will decode to the line currently displayed -- which is exactly what to speak.
//
// This is the Steins;Gate pattern reused: there the fix was a ring buffer + a write head
// (0x089797E8). Here we look for the analogous cursor.
//
// Usage:
//   node scripts/psp-tt-curline.mjs
//   node scripts/psp-tt-curline.mjs --regions 0x08FB0000-0x08FE0000,0x08C70000-0x08C90000

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const LO = 0x08800000, HI = 0x0A000000, CHUNK = 0x100000;

// Text regions discovered by psp-ram-textsearch.mjs
const DEFAULT_REGIONS = '0x08FB0000-0x08FE0000,0x08C70000-0x08C90000';
const regions = (val('--regions', DEFAULT_REGIONS)).split(',').map((s) => {
  const [a, b] = s.split('-');
  return [parseInt(a, 16) >>> 0, parseInt(b, 16) >>> 0];
});

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

function inRegions(v) {
  for (const [a, b] of regions) if (v >= a && v < b) return true;
  return false;
}

// A real pointer must be 2-byte aligned AND point at genuinely printable text. Packed colour
// constants (0xFFC7C7C7-style ABGR) land inside the regions by coincidence and must be rejected.
function printableRatio(buf) {
  const s = buf.toString('utf16le');
  if (!s.length) return 0;
  let ok = 0;
  for (const ch of s) { const c = ch.charCodeAt(0); if (c >= 0x20 && c <= 0x7E) ok++; }
  return ok / s.length;
}
async function plausiblePointer(to) {
  if (to % 2 !== 0) return null;                 // UTF-16 text is 2-byte aligned
  if (to > 0x08FFFF00) return null;              // needs room for a text run
  let buf;
  try { const r = await req('memory.read', { address: hex(to), size: 120 }); buf = Buffer.from(r.base64 || '', 'base64'); }
  catch { return null; }
  const ratio = printableRatio(buf);
  const u = buf.readUInt32LE(0);
  if (u === 0xA0A0A0A0 || u === 0xFFFFFFFF || u === 0) return null;   // padding / fill
  return ratio >= 0.75 ? ratio : null;           // must look like real text
}
function decodeUtf16(buf) {
  return buf.toString('utf16le')
    .replace(/[^\x20-\x7E]/g, '.')
    .replace(/\.{3,}/g, ' ... ');
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-curline', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  console.log('# text regions watched: ' + regions.map(([a, b]) => hex(a) + '-' + hex(b)).join(', '));
  console.log('# scanning all user RAM for u32 pointers into those regions...');

  const raw = [], hits = [];
  const t0 = Date.now();
  for (let a = LO; a < HI; a += CHUNK) {
    let buf;
    try { const r = await req('memory.read', { address: hex(a), size: CHUNK }); buf = Buffer.from(r.base64 || '', 'base64'); }
    catch (e) { console.log(`  read failed ${hex(a)}: ${e.message}`); continue; }
    for (let o = 0; o + 4 <= buf.length; o += 4) {
      const v = buf.readUInt32LE(o);
      if (inRegions(v)) raw.push({ at: a + o, to: v });
    }
  }
  console.log(`# scanned in ${((Date.now() - t0) / 1000).toFixed(1)}s -> ${raw.length} raw in-region value(s)`);
  // Validate each target actually holds text (this is what rejects colour constants).
  const seenT = new Set();
  for (const h of raw) {
    if (seenT.has(h.to)) continue;
    seenT.add(h.to);
    const ratio = await plausiblePointer(h.to);
    if (ratio !== null) hits.push({ ...h, ratio });
  }
  console.log(`# ${hits.length} of them point at genuinely printable text (>=75% printable)`);

  // Dedupe by target, and decode the text each one points at.
  const seen = new Set();
  let shown = 0;
  console.log('\n=== POINTERS INTO TEXT (address -> what it points at) ===');
  for (const h of hits) {
    const key = h.to;
    if (seen.has(key)) continue;
    seen.add(key);
    let txt = '(read failed)';
    try {
      const r = await req('memory.read', { address: hex(h.to), size: 240 });
      txt = decodeUtf16(Buffer.from(r.base64 || '', 'base64'));
    } catch {}
    console.log(`  ${hex(h.at)} -> ${hex(h.to)}`);
    console.log(`      "${txt.slice(0, 150)}"`);
    if (++shown >= 30) { console.log('  ... (truncated)'); break; }
  }

  // Also: index-like neighbours. A cursor may be stored as a LINE INDEX with a base pointer.
  console.log('\n=== small integers adjacent to each pointer (possible line index) ===');
  for (const h of hits.slice(0, 30)) {
    try {
      const r = await req('memory.read', { address: hex((h.at - 8) >>> 0), size: 24 });
      const b = Buffer.from(r.base64 || '', 'base64');
      const vals = [];
      for (let o = 0; o + 4 <= b.length; o += 4) vals.push(b.readUInt32LE(o));
      console.log(`  ${hex((h.at - 8) >>> 0)}: ${vals.map((v) => (v < 100000 ? String(v) : inRegions(v) ? 'P:' + hex(v) : '.' + v.toString(16))).join('  ')}`);
    } catch {}
  }

  console.log('\n# A cursor is the pointer (or index) that changes as dialogue advances.');
  console.log('# NEXT: run this again after one line of dialogue advances; the changed pointer is the cursor.');
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
