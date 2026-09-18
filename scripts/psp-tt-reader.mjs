#!/usr/bin/env node
// psp-tt-reader.mjs — THE DELIVERABLE: read Tag Team's story text out of RAM as it appears.
//
// WHY THIS EXISTS
//   Verified live with the user: Tag Team keeps its story text as plain UTF-16LE in RAM — dialogue,
//   the narration the game does NOT voice, mission/tutorial text, and the ordered objective list.
//   So a screen reader for this game needs NO OCR and NO core integration: it reads the text the
//   engine already decoded, which is the project's rule "model the game, not the screen".
//
// WHAT IT DOES
//   Scans the text regions and extracts printable UTF-16LE runs -- i.e. the game's story text.
//
// ⛔ MEASURED LIMITATION -- DO NOT MISREAD THE OUTPUT AS A LIVE NARRATION
//   The text regions are a STATIC CORPUS: two scans 25 s apart returned 2365 lines with 0
//   differences. The game keeps its WHOLE script resident and unchanged, and selects the line to
//   display via a cursor/pointer somewhere else. So:
//     * --once gives you the game's TEXT (useful for content: objectives, mission lists)
//     * watch mode's [NEW]/[gone] does NOT track the displayed line -- nothing changes to diff
//   A true reader needs the CURRENT-LINE CURSOR, which is NOT found yet (see the doc).
//
// USAGE
//   node scripts/psp-tt-reader.mjs                  # watch and narrate
//   node scripts/psp-tt-reader.mjs --once           # single dump
//   node scripts/psp-tt-reader.mjs --interval 900 --min-len 12

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const num = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? Number(argv[i + 1]) : d; };
const has = (f) => argv.indexOf(f) >= 0;

const ONCE = has('--once');
const INTERVAL = num('--interval', 900);
const MINLEN = num('--min-len', 12);

// Regions discovered by psp-ram-textsearch.mjs. Deliberately generous -- cheap to read.
const REGIONS = [
  [0x08FB0000, 0x08FE0000],   // dialogue / scene script
  [0x08C70000, 0x08C90000],   // UI, tutorial, and the ordered mission objective list
];

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 15000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Extract the readable lines (and each line's address) from a UTF-16LE region.
function extractLines(buf, base, minLen) {
  const out = [];
  const text = buf.toString('utf16le');
  let start = null;
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    const printable = (c >= 0x20 && c <= 0x7E);
    if (printable) { if (start === null) start = i; continue; }
    if (start !== null) {
      const s = text.slice(start, i);
      if (s.trim().length >= minLen) out.push({ addr: base + start * 2, text: s.trim() });
      start = null;
    }
  }
  if (start !== null) {
    const s = text.slice(start);
    if (s.trim().length >= minLen) out.push({ addr: base + start * 2, text: s.trim() });
  }
  return out;
}

async function scan() {
  const lines = [];
  for (const [a, b] of REGIONS) {
    try {
      const r = await req('memory.read', { address: hex(a), size: b - a });
      const buf = Buffer.from(r.base64 || '', 'base64');
      lines.push(...extractLines(buf, a, MINLEN));
    } catch (e) { console.error(`# read failed ${hex(a)}: ${e.message}`); }
  }
  return lines;
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-reader', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  const first = await scan();
  console.log(`# Tag Team text reader -- ${first.length} line(s) in the text regions`);
  console.log(`# regions: ${REGIONS.map(([a, b]) => hex(a) + '-' + hex(b)).join(', ')}`);
  if (ONCE) {
    for (const l of first) console.log(`${hex(l.addr)}  ${l.text}`);
    sock.close(); return;
  }

  console.log('⛔ NOTE: the text regions are a STATIC corpus (measured: 0 differences between two');
  console.log('   scans 25s apart). This mode can only report corpus changes, NOT the line on screen.');
  console.log('   A true reader needs the current-line cursor (not yet found).\n');
  let prev = new Map(first.map((l) => [l.addr + '|' + l.text, l]));
  for (const l of first) console.log(`[seen] ${l.text}`);

  // Loop; each poll emits only text that was not present before -- i.e. newly displayed.
  for (;;) {
    await sleep(INTERVAL);
    let now;
    try { now = await scan(); } catch (e) { console.error('# scan failed: ' + e.message); continue; }
    const cur = new Map(now.map((l) => [l.addr + '|' + l.text, l]));
    for (const [k, l] of cur) {
      if (!prev.has(k)) console.log(`[NEW ] ${l.text}`);
    }
    for (const [k, l] of prev) {
      if (!cur.has(k)) console.log(`[gone] ${l.text.slice(0, 60)}`);
    }
    prev = cur;
  }
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
