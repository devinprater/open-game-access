// ⭐ Is the write head SATURATED at the ring capacity, or is the story actually stopped?
// If the log is a ring buffer of capacity 512, once head reaches 512 it can no longer grow:
// new lines OVERWRITE in place at k=0. In that case "head stopped increasing" is a FALSE
// stall detector — the story may still be advancing.
// Test: press circle and watch the CONTENT of the newest record, not the head value.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "saturate", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");
const dec = b => { const c = b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)).toString("latin1");
  return c.replace(/\x81[\x67\x68]/g, "").replace(/\x81\x43/g, ", ").replace(/\x81[\x65\x66]/g, "'")
          .replace(/\x81\xf4/g, "").replace(/\s+/g, " ").trim(); };

const head = await u32(HEAD);
const lb = await u32(LB);
console.log(`head=${head}   logbase=0x${lb.toString(16).toUpperCase()}`);
console.log("capacity read from config = 512\n");

// fingerprint the newest record (k = 0) and a few near the head
const stamp = async () => {
  const parts = [];
  for (const k of [0, 1, 2]) {
    const a = lb + k * ST;
    parts.push(dec(await rd(a + 0x48, 0x60)).slice(0, 48));
  }
  return parts;
};
console.log("=== watching the NEWEST records while pressing circle ===");
let prev = await stamp();
console.log(`  initial   k0=${JSON.stringify(prev[0])}`);
for (let round = 1; round <= 10; round++) {
  await req("input.buttons.press", { button: "circle", duration: 25 });
  await sleep(2200);
  const now = await stamp();
  const h = await u32(HEAD);
  const changed = now[0] !== prev[0] || now[1] !== prev[1] || now[2] !== prev[2];
  console.log(`  round ${round}: head=${h}  ${changed ? "CONTENT CHANGED" : "content same"}`);
  if (changed) {
    console.log(`      k0 was ${JSON.stringify(prev[0])}`);
    console.log(`      k0 now ${JSON.stringify(now[0])}`);
    console.log(`      k1 now ${JSON.stringify(now[1])}`);
    console.log(`      k2 now ${JSON.stringify(now[2])}`);
    prev = now;
  }
}
s.close();
