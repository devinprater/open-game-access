// After a clean PPSSPP restart, test whether injected input LANDS.
// Method: boot -> title -> the game. Watch the write head; a fresh boot starts at head 0,
// so any increase proves the input path works again.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "afterboot", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("fresh boot state:");
console.log("  head   =", await u32(HEAD));
console.log("  logbase=", "0x" + (await u32(LB)).toString(16).toUpperCase());
console.log("  cpu    =", JSON.stringify(await req("cpu.status")));
console.log();
console.log("=== pressing START (PSP menu / advance), then cross, watching head ===");
for (const b of ["start", "cross", "cross", "cross", "circle", "cross", "start", "cross",
                 "triangle", "cross", "cross", "square", "cross", "cross", "cross", "cross"]) {
  const before = await u32(HEAD);
  await req("input.buttons.press", { button: b, duration: 40 });
  await sleep(1800);
  const after = await u32(HEAD);
  console.log(`  ${b.padEnd(9)} head ${String(before).padStart(5)} -> ${String(after).padStart(5)}${after !== before ? "   <== MOVED" : ""}`);
}
console.log("\nfinal head =", await u32(HEAD));
s.close();
