#!/usr/bin/env node
// psp-tt-posbidi.mjs — BIDIRECTIONAL position test: fly north, then south.
//
// THE DISCRIMINATOR
//   Previous attempts gave ~57-67% monotonicity on many near-duplicate rows, which is ambiguous
//   between "entity positions" and "a rotating basis / interpolated copies". Monotonicity alone
//   cannot separate them.
//
//   A real POSITION passes this test cleanly:
//       fly NORTH  -> value moves one way   (A - S)
//       fly SOUTH  -> value moves back      (B - A), opposite sign
//       and B ends up near S (it returned)
//   A rotating basis does NOT: rotation is periodic, so it often keeps going the same way, or its
//   magnitude depends on the turn rather than the displacement.
//
//   So we score:  opposite = sign(A-S) == -sign(B-A)  AND  return = |B-S| small relative to range.
//
// Usage:
//   node scripts/psp-tt-posbidi.mjs --window 0x08B69000 --size 0x4000
//   node scripts/psp-tt-posbidi.mjs --window 0x08B69000 --size 0x4000 --hold 2000

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const num = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? Number(argv[i + 1]) : d; };

const FULL = argv.indexOf('--full') >= 0;      // scan ALL user RAM instead of one window
const WINDOW = num('--window', 0x08B69000) >>> 0;
const SIZE = num('--size', 0x4000);
const HOLD = num('--hold', 2000);
const RAM_LO = 0x08800000, RAM_HI = 0x0A000000;

const sock = new WebSocket(URL, 'debugger.ppsspp.org');
let ticket = 1; const pending = new Map();
sock.addEventListener('message', (ev) => {
  let m; try { m = JSON.parse(ev.data); } catch { return; }
  if (m.ticket && pending.has(m.ticket)) {
    const { resolve, reject } = pending.get(m.ticket); pending.delete(m.ticket);
    m.event === 'error' ? reject(new Error(m.message)) : resolve(m);
  }
});
function req(event, extra = {}, timeout = 6000) {
  return new Promise((resolve, reject) => {
    const t = String(ticket++); pending.set(t, { resolve, reject });
    setTimeout(() => { if (pending.delete(t)) reject(new Error('timeout on ' + event)); }, timeout);
    sock.send(JSON.stringify({ event, ticket: t, ...extra }));
  });
}
const hex = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// In --full mode we read the whole 24 MiB user RAM area via bulk reads (~0.5 s) and treat it as one
// contiguous buffer, so addresses are RAM_LO-relative rather than WINDOW-relative.
async function readWin() {
  if (!FULL) {
    const r = await req('memory.read', { address: hex(WINDOW), size: SIZE }, 12000);
    return Buffer.from(r.base64 || '', 'base64');
  }
  const parts = [];
  for (let a = RAM_LO; a < RAM_HI; a += 0x100000) {
    const r = await req('memory.read', { address: hex(a), size: 0x100000 }, 25000);
    parts.push(Buffer.from(r.base64 || '', 'base64'));
  }
  return Buffer.concat(parts);
}
const BASE = FULL ? RAM_LO : WINDOW;

// Hold a direction and READ THE WINDOW WHILE STILL HOLDING.
// ⚠️ Why: if the stick is released before sampling, every VELOCITY field reads 0 at both S and B and
// scores a perfect "returned to start", crowding out the true position. Sampled mid-hold:
//   * POSITION  -> S (rest) -> A (moved north) -> B (moved back) : B returns to ~S
//   * VELOCITY  -> S=0 -> A=-X -> B=+X                          : B does NOT return to S
// so `returned` near 0 selects the position and rejects velocity.
async function flyAndSample(y, ms) {
  const t0 = Date.now();
  let win = null;
  while (Date.now() - t0 < ms) {
    try { await req('input.analog.send', { stick: 'left', x: 0, y }, 2500); } catch {}
    if (Date.now() - t0 > ms * 0.6) win = await readWin();   // sample mid-hold, stick still held
    await sleep(50);
  }
  return win || await readWin();
}
async function release() {
  try { await req('input.analog.send', { stick: 'left', x: 0, y: 0 }, 2500); } catch {}
  await sleep(600);
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-posbidi', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  console.log(FULL
    ? `# bidirectional test on ALL user RAM (${hex(RAM_LO)}-${hex(RAM_HI)}), holding ${HOLD}ms each way`
    : `# bidirectional test on ${hex(WINDOW)} +${hex(SIZE)}, holding ${HOLD}ms each way`);
  console.log('# S = start, A = after flying NORTH (y=-1), B = after flying SOUTH (y=+1)');

  const S = await readWin();
  console.log('# flying NORTH (sampling mid-hold)...');
  const A = await flyAndSample(-1, HOLD);
  console.log('# flying SOUTH (sampling mid-hold)...');
  const B = await flyAndSample(+1, HOLD);
  await release();
  console.log('# three snapshots captured (A and B taken WHILE HOLDING)');

  const out = [];
  for (let o = 0; o + 4 <= S.length; o += 4) {
    const s = S.readFloatLE(o), a = A.readFloatLE(o), b = B.readFloatLE(o);
    if (![s, a, b].every((v) => Number.isFinite(v) && Math.abs(v) < 100000)) continue;
    const d1 = a - s, d2 = b - a;
    // ⚠️ MAGNITUDE FLOOR. A degenerate 1-unit reversal (S=1 -> A=0 -> B=1) scored a perfect 4.00 in
    // an earlier run, and near-zero DENORMAL floats being read as floats produce the same pattern.
    // Real flight moves the character a substantial distance, so require a meaningful displacement
    // and reject values living near zero.
    if (Math.abs(s) < 2 && Math.abs(a) < 2 && Math.abs(b) < 2) continue;      // denormal / near-zero noise
    if (Math.abs(d1) < 5.0 || Math.abs(d2) < 5.0) continue;                   // too small to be travel
    const opposite = Math.sign(d1) === -Math.sign(d2);
    const range = Math.max(s, a, b) - Math.min(s, a, b);
    const returned = Math.abs(b - s) / (range || 1);                 // 0 = perfect return
    // A position: moved out, came back, similar magnitude both ways.
    const magRatio = Math.abs(Math.abs(d1) - Math.abs(d2)) / Math.max(Math.abs(d1), Math.abs(d2));
    const score = (opposite ? 2 : 0) + (1 - Math.min(returned, 1)) + (1 - magRatio);
    out.push({ addr: BASE + o, s, a, b, d1, d2, opposite, returned, magRatio, score });
  }

  out.sort((x, y) => y.score - x.score);
  console.log(`\n=== ${out.length} moved float(s); ranked by bidirectional-position score ===`);
  console.log('# score 4.0 = moved out, came back, opposite signs, matched magnitude');
  for (const c of out.slice(0, 16)) {
    console.log(`  ${hex(c.addr)}  S=${c.s.toFixed(1)} A=${c.a.toFixed(1)} B=${c.b.toFixed(1)}`);
    console.log(`      d1=${c.d1.toFixed(1)} d2=${c.d2.toFixed(1)} opposite=${c.opposite} returned=${(c.returned * 100).toFixed(0)}% magDiff=${(c.magRatio * 100).toFixed(0)}%  SCORE=${c.score.toFixed(2)}`);
  }

  // Group the best scorers into consecutive triples: a position is x,y,z adjacent.
  const good = out.filter((c) => c.score >= 3.0).map((c) => ((c.addr - BASE) / 4) | 0).sort((a, b) => a - b);
  console.log(`\n=== CONSECUTIVE RUNS among score>=3.0 (a position is 3 adjacent floats) ===`);
  const groups = [];
  for (const w of good) {
    if (groups.length && w - groups[groups.length - 1][groups[groups.length - 1].length - 1] === 1) groups[groups.length - 1].push(w);
    else groups.push([w]);
  }
  const trips = groups.filter((g) => g.length >= 3);
  console.log(`# ${trips.length} group(s) of >=3`);
  for (const g of trips.slice(0, 12)) console.log(`  ${hex(BASE + g[0] * 4)} len=${g.length}`);

  console.log('\n# A confirmed position = a run of 3 floats all with SCORE >= 3.0.');
  console.log('# If none, the window does not hold the player position (try a wider scan).');
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
