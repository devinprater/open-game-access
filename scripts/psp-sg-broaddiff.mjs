// ⛔ CORRECTED INPUT-LANDS TEST.
// My earlier version diffed only the message-log buffer (0x9557C80 .. 0x9577C80, 128 KB).
// That was a bad test: pressing TRIANGLE to open the phone does not write to the message
// log, so "0 bytes changed" proved nothing about whether input is landing.
//
// This diffs a BROAD region of the emulated RAM, comparing:
//   (a) IDLE  — two reads 4s apart with NO input
//   (b) PRESS — two reads 4s apart with a burst of presses in between
// If presses cause materially more change than idling, input IS landing and the game is
// simply not advancing the log (i.e. it IS waiting on a choice/phone state).
// If presses cause the SAME churn as idling, the injection is not reaching the game.
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 60000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "broaddiff", version: "1" });
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");

// A broad slab of PSP user RAM, in chunks so no single request is enormous.
const START = 0x08800000, CHUNK = 0x100000, NCHUNKS = 6;   // 6 MB
async function snap() {
  const parts = [];
  for (let i = 0; i < NCHUNKS; i++) parts.push(await rd(START + i * CHUNK, CHUNK));
  return Buffer.concat(parts);
}
function diffCount(a, b) { let c = 0; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) c++; return c; }

console.log(`region 0x${START.toString(16).toUpperCase()} .. 0x${(START + NCHUNKS * CHUNK).toString(16).toUpperCase()} (${NCHUNKS} MB)`);
console.log("reading IDLE baseline ...");
const i0 = await snap();
await sleep(4000);
const i1 = await snap();
const idle = diffCount(i0, i1);
console.log(`  IDLE  (no input, 4s): ${idle} bytes differ`);

console.log("reading PRESS baseline ...");
const p0 = await snap();
console.log("  pressing cross/circle/triangle/square/start 3x each ...");
for (const b of ["cross", "circle", "triangle", "square", "start"]) {
  for (let k = 0; k < 3; k++) { await req("input.buttons.press", { button: b, duration: 45 }); await sleep(320); }
}
await sleep(4000);
const p1 = await snap();
const pressed = diffCount(p0, p1);
console.log(`  PRESS (with input, 4s): ${pressed} bytes differ`);

console.log();
if (pressed > idle * 1.5) {
  console.log("  => INPUT IS LANDING — presses cause materially more change than idling.");
  console.log("     The game is not advancing the log because it is WAITING on something.");
} else {
  console.log("  => INPUT IS NOT LANDING — presses change no more than idling.");
  console.log("     Any 'block' is a harness fault, not game behaviour.");
}
s.close();
