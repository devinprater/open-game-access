// ⛔ CONTROL: does the story advance with ZERO input?
// The previous run pressed DOWN and the head went 65 -> 242. But if the game AUTO-ADVANCES
// dialogue on a timer, that progress had nothing to do with the button, and every
// "this button advances the story" conclusion in this session needs re-checking.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 10000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "control", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

const h0 = await u32(HEAD);
console.log(`head at start = ${h0}`);
console.log("watching for 60 s with ZERO input ...");
const marks = [];
for (let i = 0; i < 12; i++) {
  await sleep(5000);
  const h = await u32(HEAD);
  marks.push(h);
  console.log(`  t=${(i + 1) * 5}s  head=${h}${h !== h0 ? "   <== MOVED WITHOUT INPUT" : ""}`);
}
const moved = marks[marks.length - 1] !== h0;
console.log();
console.log(moved
  ? "  => THE GAME AUTO-ADVANCES. Progress cannot be attributed to a button press."
  : "  => NO auto-advance. The story only moves when a button is pressed.");
s.close();
