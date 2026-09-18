#!/usr/bin/env node
// psp-tt-stagesearch.mjs — find the Dragon Walker stage counter in RAM.
//
// WHY: the Dragon Walker screen displays readable state:
//     Dragon Walker
//     [book] 761                 L 1 / 1 R
//     10/12   "Unknown Warrior from Another Planet"   [!]
// The stage counter "10/12" is the key to narrating progress. Small single numbers are hard to
// search (the index-0 lesson), but a PAIR -- 10 ADJACENT TO 12 -- is a strong signature.
//
// This script uses BULK reads (memory.read {address,size} -> base64) so the whole 24 MiB address
// space is scanned in about half a second, and reports the addresses of:
//   - cur (10) immediately followed by total (12)     <- the prime candidate
//   - the reverse order (12 then 10)
//   - the standalone values, for completeness
//
// Usage:
//   node scripts/psp-tt-stagesearch.mjs --cur 10 --total 12
//   node scripts/psp-tt-stagesearch.mjs --cur 10 --total 12 --also 761

const PORT = Number(process.env.PSP_DEBUG_PORT || 12345);
const URL = `ws://127.0.0.1:${PORT}/debugger`;
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };

const CUR = Number(val('--cur', 10));
const TOTAL = Number(val('--total', 12));
const ALSO = argv.indexOf('--also') >= 0 ? argv.slice(argv.indexOf('--also') + 1).map(Number) : [];

const RAM_LO = 0x08800000, RAM_HI = 0x0A000000;
const CHUNK = 0x100000;

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

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-stagesearch', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); console.log('# resumed'); } } catch {}

  console.log(`# searching RAM for the stage counter pair: ${CUR} then ${TOTAL}` + (ALSO.length ? ` (+ ${ALSO.join(', ')})` : ''));
  console.log('# NOTE: read the screen first and pass the numbers you actually SEE.');

  const pairs = [];        // cur followed by total
  const reversepairs = [];
  const solos = new Map(); // value -> addresses
  const t0 = Date.now();

  for (let a = RAM_LO; a < RAM_HI; a += CHUNK) {
    let buf = null;
    try {
      const r = await req('memory.read', { address: hex(a), size: CHUNK });
      buf = Buffer.from(r.base64 || '', 'base64');
    } catch (e) { console.log(`  read failed ${hex(a)}: ${e.message}`); continue; }

    for (let o = 0; o + 8 <= buf.length; o += 4) {
      const v1 = buf.readUInt32LE(o);
      const v2 = buf.readUInt32LE(o + 4);
      if (v1 === CUR && v2 === TOTAL) pairs.push(a + o);
      if (v1 === TOTAL && v2 === CUR) reversepairs.push(a + o);
      if (v1 === CUR || v1 === TOTAL || ALSO.includes(v1)) {
        const arr = solos.get(v1) || []; arr.push(a + o); solos.set(v1, arr);
      }
    }
    const pct = ((a + CHUNK - RAM_LO) / (RAM_HI - RAM_LO) * 100).toFixed(0);
    if (pct % 25 === 0) process.stdout.write('.');
  }
  console.log(`\n# scanned in ${((Date.now() - t0) / 1000).toFixed(1)}s`);

  console.log(`\n=== PRIME CANDIDATE: [${CUR}, ${TOTAL}] adjacent (cur then total) — ${pairs.length} hit(s) ===`);
  for (const a of pairs.slice(0, 40)) console.log(`   ${hex(a)}`);

  console.log(`\n=== reverse order [${TOTAL}, ${CUR}] — ${reversepairs.length} hit(s) ===`);
  for (const a of reversepairs.slice(0, 20)) console.log(`   ${hex(a)}`);

  for (const [v, arr] of [...solos.entries()].sort((a, b) => a[0] - b[0])) {
    console.log(`\n=== standalone ${v} — ${arr.length} hit(s) ===`);
    for (const a of arr.slice(0, 20)) console.log(`   ${hex(a)}`);
    if (arr.length > 20) console.log(`   ... and ${arr.length - 20} more`);
  }

  console.log('\n# NEXT: re-run after advancing one stage; the address whose value becomes 11 is the counter.');
  sock.close();
}
main().catch(e => { console.error('ERROR: ' + e.message); process.exit(1); });
