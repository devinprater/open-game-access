#!/usr/bin/env node
// psp-tt-postrack.mjs — track a POSITION continuously, to separate a real player position
// from a rotating basis / entity table.
//
// WHY: one move-and-diff pass reported ~20 triple-like rows all changing by the SAME magnitude.
// That is ambiguous: it could be (a) an entity table where every entity moved, or (b) a rotating
// basis/camera transform, which changes many rows at once and OSCILLATES.
//
// DISCRIMINATOR -- time series:
//   * a real POSITION changes smoothly and, while a direction is held, roughly MONOTONICALLY
//   * a rotating basis OSCILLATES (value goes up then down) and its components swap magnitude
//
// So: sample a window of RAM many times while a direction is held, then report each float that
// moved, with monotonicity and oscillation statistics, and rank the best position candidates.
//
// Usage:
//   node scripts/psp-tt-postrack.mjs --window 0x08B69000 --size 0x6000 --samples 24 --dir up
//   node scripts/psp-tt-postrack.mjs --window 0x08B69000 --size 0x6000 --samples 24 --dir up --release

const URL = `ws://127.0.0.1:${Number(process.env.PSP_DEBUG_PORT || 12345)}/debugger`;
const argv = process.argv.slice(2);
const num = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? Number(argv[i + 1]) : d; };
const has = (f) => argv.indexOf(f) >= 0;

const WINDOW = num('--window', 0x08B69000) >>> 0;
const SIZE = num('--size', 0x6000);
const SAMPLES = num('--samples', 24);
const INTERVAL = num('--interval', 400);
const DIR = (() => { const i = argv.indexOf('--dir'); return i >= 0 ? argv[i + 1] : 'up'; })();
const AXES = { up: [128, 0], down: [128, 255], left: [0, 128], right: [255, 128] };

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

async function readWin() {
  const r = await req('memory.read', { address: hex(WINDOW), size: SIZE });
  return Buffer.from(r.base64 || '', 'base64');
}

async function main() {
  await new Promise((res, rej) => {
    sock.addEventListener('open', res, { once: true });
    sock.addEventListener('error', () => rej(new Error('cannot connect to ' + URL)), { once: true });
  });
  await req('version', { name: 'tt-postrack', version: '1' });
  try { const st = await req('cpu.status'); if (st.stepping || st.paused) { await req('cpu.resume'); } } catch {}

  const [ax, ay] = AXES[DIR] || AXES.up;
  console.log(`# tracking window ${hex(WINDOW)} +${hex(SIZE)}, ${SAMPLES} samples @ ${INTERVAL}ms`);
  console.log(`# holding analog ${DIR} (x=${ax} y=${ay}) throughout`);

  // Hold the stick by re-sending the analog state every sample (idempotent state, not a press).
  const series = [];
  for (let i = 0; i < SAMPLES; i++) {
    try { await req('input.analog.send', { stick: 'left', x: ax, y: ay, trigger: false, hold: INTERVAL + 400 }, 5000); }
    catch { /* acts, does not reply -- expected */ }
    try { series.push(await readWin()); } catch (e) { console.log(`  ! sample ${i}: ${e.message}`); }
    await sleep(INTERVAL);
  }
  try { await req('input.analog.send', { stick: 'left', x: 128, y: 128, trigger: false, hold: 60 }, 4000); } catch {}
  if (has('--release')) console.log('# stick released');

  if (series.length < 6) { console.log('# not enough samples; aborting'); sock.close(); return; }

  const n = series[0].length;
  const cands = [];
  for (let o = 0; o + 4 <= n; o += 4) {
    const vals = series.map((b) => b.readFloatLE(o));
    if (vals.some((v) => !Number.isFinite(v) || Math.abs(v) > 100000)) continue;
    const first = vals[0], last = vals[vals.length - 1];
    const min = Math.min(...vals), max = Math.max(...vals);
    const range = max - min;
    const net = Math.abs(last - first);
    if (range < 1.0) continue;                       // did not really move
    // monotonicity: fraction of consecutive steps with the same sign as net
    const sgn = Math.sign(last - first) || 1;
    let agree = 0, steps = 0;
    for (let i = 1; i < vals.length; i++) {
      const d = vals[i] - vals[i - 1];
      if (Math.abs(d) < 1e-6) continue;
      steps++; if (Math.sign(d) === sgn) agree++;
    }
    const mono = steps ? agree / steps : 0;
    cands.push({ off: o, addr: WINDOW + o, first, last, range, net, mono, vals });
  }

  // A position: large net movement, high monotonicity (low oscillation).
  cands.sort((a, b) => (b.net * b.mono) - (a.net * a.mono));
  console.log(`\n=== ${cands.length} moving float(s); top candidates ranked by net*monotonicity ===`);
  for (const c of cands.slice(0, 14)) {
    console.log(`  ${hex(c.addr)}  ${c.first.toFixed(1)} -> ${c.last.toFixed(1)}  net=${c.net.toFixed(1)} range=${c.range.toFixed(1)} mono=${(c.mono * 100).toFixed(0)}%`);
    console.log(`      series: ${c.vals.slice(0, 10).map((v) => v.toFixed(0)).join(' ')} ...`);
  }

  console.log(`\n=== TOP BY MONOTONICITY (a held direction must move a position consistently) ===`);
  const byMono = [...cands].sort((a, b) => (b.mono - a.mono) || (b.net - a.net));
  for (const c of byMono.slice(0, 16)) {
    console.log(`  ${hex(c.addr)}  ${c.first.toFixed(1)} -> ${c.last.toFixed(1)}  net=${c.net.toFixed(1)} range=${c.range.toFixed(1)} mono=${(c.mono * 100).toFixed(0)}%`);
  }

  // Group top candidates into consecutive triples (a position is x,y,z adjacent).
  const top = cands.slice(0, 200).map((c) => c.off).sort((a, b) => a - b);
  const groups = [];
  for (const o of top) {
    if (groups.length && o - groups[groups.length - 1][groups[groups.length - 1].length - 1] <= 4) groups[groups.length - 1].push(o);
    else groups.push([o]);
  }
  console.log(`\n=== CONSECUTIVE RUNS (position triples) ===`);
  for (const g of groups.filter((g) => g.length >= 3).slice(0, 10)) {
    console.log(`  ${hex(WINDOW + g[0])} len=${g.length}`);
  }

  console.log('\n# A real POSITION shows high monotonicity (>=80%) and a consistent sign.');
  console.log('# An oscillating/rotating basis shows low monotonicity.');
  sock.close();
}
main().catch((e) => { console.error('ERROR: ' + e.message); process.exit(1); });
