// ⭐ THE RING-WRAP TEST.
// head reads exactly 512 = the capacity read from SYSTEM.CFG. If the message log is a ring
// buffer that has FILLED, then `k = (head - (index-1) - 1)` no longer identifies the newest
// record — the write pointer has wrapped and new lines overwrite somewhere else in the
// buffer. Watching only k=0 would then report "content unchanged" while the story is
// actually advancing.
//
// Decisive test: hash the WHOLE log buffer, press, hash again, and report WHICH records
// changed. If records change away from k=0, the ring wrapped and the reader's index math
// needs to account for the write pointer rather than the saturated head.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC;
const CAP = 512;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 20000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "ringwrap", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

const head0 = await u32(HEAD), lb = await u32(LB);
console.log(`head=${head0}  logbase=0x${lb.toString(16).toUpperCase()}  capacity=${CAP}`);
console.log(`buffer spans 0x${lb.toString(16).toUpperCase()} .. 0x${(lb + CAP * ST).toString(16).toUpperCase()}\n`);

// read the whole log in one go
async function wholeBuffer() { return await req("memory.read", { address: lb, size: CAP * ST, replacements: false }).then(r => Buffer.from(r.base64, "base64")); }

console.log("reading the full log buffer ...");
let prev = await wholeBuffer();
console.log(`  ${prev.length} bytes read (expected ${CAP * ST})\n`);

for (let round = 1; round <= 8; round++) {
  await req("input.buttons.press", { button: "circle", duration: 25 });
  await sleep(2400);
  const now = await wholeBuffer();
  const h = await u32(HEAD);
  const changed = [];
  for (let k = 0; k < CAP; k++) {
    const off = k * ST;
    if (!prev.subarray(off, off + ST).equals(now.subarray(off, off + ST))) changed.push(k);
  }
  console.log(`round ${round}: head=${h}  records changed: ${changed.length ? changed.join(", ") : "(none)"}`);
  if (changed.length) {
    for (const k of changed.slice(0, 3)) {
      const txt = now.subarray(k * ST + 0x48, k * ST + 0x48 + 0x60).toString("latin1").split("\x00")[0]
        .replace(/\x81[\x67\x68]/g, "").replace(/\x81\x43/g, ", ").replace(/\x81[\x65\x66]/g, "'")
        .replace(/\x81\xf4/g, "").replace(/\s+/g, " ").trim();
      console.log(`      k=${k}  ${JSON.stringify(txt.slice(0, 70))}`);
    }
    prev = now;
  }
}
s.close();
