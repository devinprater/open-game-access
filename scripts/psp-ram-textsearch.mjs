#!/usr/bin/env node
// psp-ram-textsearch.mjs — search PSP RAM for a TEXT string, in UTF-16LE and ASCII.
//
// WHY: the accessibility question for a dialogue reader is not "which id is showing" but
// "can I get the TEXT". PACKFILE keeps text as UTF-16LE; if the game copies a line into RAM to
// render it, the same encoding will appear in RAM and a reader can read it directly.
//
// Reads the whole 24 MiB user RAM area via bulk reads (memory.read {address,size} -> base64).
//
// Usage:
//   node scripts/psp-ram-textsearch.mjs "long time ago"
//   node scripts/psp-ram-textsearch.mjs "Piccolo" --enc utf16
const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const needle = argv.find((a) => !a.startsWith('--'));
const encArg = argv.indexOf('--enc') >= 0 ? argv[argv.indexOf('--enc') + 1] : 'both';
if (!needle) { console.error('usage: node psp-ram-textsearch.mjs "<text>" [--enc utf16|ascii|both]'); process.exit(1); }

const LO = 0x08800000, HI = 0x0A000000, CHUNK = 0x100000;
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

await new Promise((res, rej) => {
  sock.addEventListener('open', res, { once: true });
  sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
});
await req('version', { name: 'ram-textsearch', version: '1' });
try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

const wantUtf16 = encArg === 'both' || encArg === 'utf16';
const wantAscii = encArg === 'both' || encArg === 'ascii';
const u16 = wantUtf16 ? Buffer.from(needle, 'utf16le') : null;
const asc = wantAscii ? Buffer.from(needle, 'latin1') : null;
const lower = needle.toLowerCase();

console.log(`# searching RAM for "${needle}"  (utf16=${wantUtf16} ascii=${wantAscii})`);
const hits = [];
const t0 = Date.now();

for (let a = LO; a < HI; a += CHUNK) {
  let buf;
  try { const r = await req('memory.read', { address: hex(a), size: CHUNK }); buf = Buffer.from(r.base64 || '', 'base64'); }
  catch (e) { console.log(`  read failed ${hex(a)}: ${e.message}`); continue; }
  if (u16) { let i = 0; while ((i = buf.indexOf(u16, i)) !== -1) { hits.push({ addr: a + i, enc: 'utf16', off: i }); i += 2; } }
  if (asc) { let i = 0; while ((i = buf.indexOf(asc, i)) !== -1) { hits.push({ addr: a + i, enc: 'ascii', off: i }); i += 1; } }
}
console.log(`# scanned in ${((Date.now() - t0) / 1000).toFixed(1)}s -> ${hits.length} hit(s)`);

// For each hit, decode the surrounding text so it is readable here.
for (const h of hits.slice(0, 25)) {
  const start = (h.addr - 64) >>> 0;
  let ctx = '';
  try {
    const r = await req('memory.read', { address: hex(start), size: 320 });
    const b = Buffer.from(r.base64 || '', 'base64');
    if (h.enc === 'utf16') {
      ctx = b.toString('utf16le').replace(/[^\x20-\x7E\u2018\u2019\u201C\u201D\u2026!?.,'":;()\-/]/g, '.');
    } else {
      ctx = b.toString('latin1').replace(/[^\x20-\x7E]/g, '.');
    }
  } catch { ctx = '(read failed)'; }
  console.log(`\n--- ${hex(h.addr)} [${h.enc}]`);
  console.log('    ' + ctx);
}

console.log('\n# If the text is present in RAM, a dialogue reader can read it directly.');
console.log('# If it is NOT present, text is rendered from PACKFILE and the reader must resolve ids.');
sock.close();
