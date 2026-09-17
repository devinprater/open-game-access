// ⛔ DOES INJECTED INPUT LAND AT ALL?
// The screen animates on its own and the write head is pinned, so neither can tell us.
// This diffs a large RAM region before and after a burst of presses. If input reaches the
// game and the game reacts, SOMETHING in RAM must change. If the diff is empty, the
// injection is not landing (window focus, wrong port, wrong target) and any "block" is a
// harness fault, not game behaviour.
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 30000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "inputdiff", version: "1" });
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");

// the message log + a broad slab covering UI/state globals
const A = 0x09557C80, Z = 0x20000;
console.log(`diffing 0x${A.toString(16).toUpperCase()} .. 0x${(A + Z).toString(16).toUpperCase()} (${Z} bytes)`);

async function diff(label) {
  const before = await rd(A, Z);
  for (const b of ["cross", "circle", "start", "down"]) { await req("input.buttons.press", { button: b, duration: 40 }); await sleep(700); }
  await sleep(1500);
  const after = await rd(A, Z);
  let bytes = 0, firstOff = -1;
  for (let i = 0; i < Z; i++) if (before[i] !== after[i]) { bytes++; if (firstOff < 0) firstOff = i; }
  console.log(`  ${label}: ${bytes} bytes changed` + (firstOff >= 0 ? `  (first at +0x${firstOff.toString(16)})` : ""));
  return bytes;
}
// baseline: NO input, to see how much churn the running game produces by itself
const b0 = await rd(A, Z); await sleep(3500); const b1 = await rd(A, Z);
let idle = 0; for (let i = 0; i < Z; i++) if (b0[i] !== b1[i]) idle++;
console.log(`  IDLE (no input, 3.5s): ${idle} bytes changed`);
// now with presses
const pressed = await diff("WITH presses");
console.log();
console.log(pressed <= idle ? "  => presses change NO more than idling: INPUT IS NOT LANDING" : "  => presses cause extra changes: input IS landing");
s.close();
