// press buttons DIRECTLY over the debugger while watching the write head.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "presswatch", version: "1" });
const head = async () => Buffer.from((await req("memory.read", { address: HEAD, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("start head =", await head());
// try each button, pressing twice with a long settle
for (const b of ["cross", "circle", "start", "select", "square", "triangle", "up", "down", "right", "left"]) {
  const before = await head();
  for (let i = 0; i < 2; i++) { await req("input.buttons.press", { button: b, duration: 20 }); await sleep(1400); }
  await sleep(1200);
  const after = await head();
  console.log(`  ${b.padEnd(9)} ${before} -> ${after}${after > before ? "   <== ADVANCED" : ""}`);
  if (after > before) { console.log("\n*** ADVANCES WITH:", b, "***"); break; }
}
s.close();
