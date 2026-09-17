// ⭐ THE PHONE-STATE TEST — this is what "do choices work" reduces to.
// FUN_0008d7f0 warns "Phone not open" / "Phone not close" and reads/writes two state bytes:
//   cRam0001b1e7 @ RAM 0x0897B1E7
//   cRam0001b1e8 @ RAM 0x0897B1E8
// If pressing TRIANGLE (the documented "take out phone" button) flips one of these, then
// input IS landing and the phone — the game's choice mechanic — is drivable. If they never
// move under triangle while the CPU is running, the choice system is unreachable from this
// harness and that is the honest answer.
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "phone", version: "1" });
const u8 = async a => (await req("memory.read", { address: a, size: 1, replacements: false })).base64
  ? Buffer.from((await req("memory.read", { address: a, size: 1, replacements: false })).base64, "base64")[0] : -1;

const OPEN = 0x0897B1E7, CLOSE = 0x0897B1E8;
const HEAD = 0x089797E8;
const snap = async () => `open=${await u8(OPEN)} close=${await u8(CLOSE)} head=${Buffer.from((await req("memory.read",{address:HEAD,size:4,replacements:false})).base64,"base64").readUInt32LE(0)}`;

console.log("=== baseline, NO input, 6 samples ===");
for (let i = 0; i < 6; i++) { console.log("  " + await snap()); await sleep(500); }

for (const b of ["triangle", "square", "circle", "cross", "start"]) {
  console.log(`\n=== pressing ${b.toUpperCase()} 4x, sampling between ===`);
  for (let i = 0; i < 4; i++) {
    await req("input.buttons.press", { button: b, duration: 40 });
    await sleep(1100);
    console.log(`  after press ${i + 1}: ${await snap()}`);
  }
}
s.close();
