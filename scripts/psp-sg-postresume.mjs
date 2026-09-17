// Now that the CPU is RESUMED, re-run the real test: does input land, and does the game
// advance / reach a choice?
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 8000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "postresume", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("cpu:", JSON.stringify(await req("cpu.status")));
console.log(`head=${await u32(HEAD)}  logbase=0x${(await u32(LB)).toString(16).toUpperCase()}\n`);

// step the title screen: START, then cross a few times
console.log("=== pressing through the title ===");
for (const b of ["start", "start", "cross", "cross", "cross", "cross", "cross", "cross", "cross", "cross"]) {
  const before = await u32(HEAD);
  try { await req("input.buttons.press", { button: b, duration: 30 }); } catch (e) { console.log(`  ${b}: press ${e.message}`); }
  await sleep(1500);
  const after = await u32(HEAD);
  const lb = await u32(LB);
  console.log(`  ${b.padEnd(7)} head ${String(before).padStart(4)} -> ${String(after).padStart(4)}  logbase=0x${lb.toString(16).toUpperCase()}${after > before ? "   <== ADVANCED" : ""}`);
  if (after > 100) { console.log("  *** DEEP IN THE GAME — dialogue is flowing ***"); break; }
}
s.close();
