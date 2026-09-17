// ⛔ WHY PRESSES DON'T REGISTER — try every press form the debugger offers.
// The title says "Press START button" and injected START does nothing, while the game is
// clearly running (the background animates). Candidates:
//   1. duration too short to span a frame the game polls
//   2. the debugger press needs an explicit DOWN then a separate UP
//   3. frames advance only when the emulator is "stepping" vs running
// Test each form and watch the SCREEN (the title background animates, so instead watch for
// the title TEXT changing = leaving the title), plus the write head.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 12000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "pressforms", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("head =", await u32(HEAD), "\n");
const forms = [
  ["duration 20",    { button: "start", duration: 20 }],
  ["duration 200",   { button: "start", duration: 200 }],
  ["duration 1000",  { button: "start", duration: 1000 }],
  ["duration 5000",  { button: "start", duration: 5000 }],
  ["duration -1",    { button: "start", duration: -1 }],
  ["no duration",    { button: "start" }],
];
for (const [label, args] of forms) {
  try {
    const r = await req("input.buttons.press", args);
    await sleep(2500);
    console.log(`  ${label.padEnd(16)} accepted  head=${await u32(HEAD)}  reply=${JSON.stringify(r).slice(0, 90)}`);
  } catch (e) { console.log(`  ${label.padEnd(16)} FAILED: ${e.message}`); }
}
console.log("\n=== trying the CPU 'stepping' mode: some builds only accept input while stepping ===");
try { const st = await req("cpu.stepping", { stepping: true }); console.log("  stepping on:", JSON.stringify(st)); } catch (e) { console.log("  cpu.stepping:", e.message); }
try { const r = await req("input.buttons.press", { button: "start", duration: 200 }); console.log("  press while stepping:", JSON.stringify(r)); } catch (e) { console.log("  press failed:", e.message); }
try { await req("cpu.stepping", { stepping: false }); console.log("  stepping off"); } catch (e) {}
await sleep(2500);
console.log("  head now =", await u32(HEAD));
s.close();
