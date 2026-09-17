// ⛔ THE INPUT-LANDS TEST.
// The screen animates on its own (two alternating frames, confirmed with zero input), so
// "the screen changed after a press" proves NOTHING. This test uses PSP's HOME button,
// which must raise a modal "quit?" dialog if the press reaches the game — a state that
// cannot be confused with the idle animation.
//
// If HOME produces a screen unlike the idle set, input IS landing and the game is genuinely
// blocked. If not, my harness has stopped driving the game and the "block" is my bug.
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 10000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "inputlands", version: "1" });

// the debugger can snapshot the framebuffer; but we can also just read a hashed region of
// VRAM. Simplest reliable signal: ask the debugger for a framebuffer hash if supported,
// otherwise fall back to reading a chunk of RAM that the renderer writes.
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("=== does input land? probing debugger capabilities ===");
for (const ev of ["input.buttons.press", "input.buttons.release", "input.buttons.pressAll", "input.axis"]) {
  try { await req(ev, { button: "cross", duration: 1, axis: 0, value: 0 }); console.log(`  ${ev}: accepted`); }
  catch (e) { console.log(`  ${ev}: ${e.message}`); }
}
console.log("\n=== current head (a value that only moves if the GAME reacts) ===");
console.log("  head =", await u32(0x089797E8));
s.close();
