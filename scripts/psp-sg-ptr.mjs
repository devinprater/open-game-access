// Is `head` CAPPED at capacity, or is there a separate total-count / write pointer?
// head reads exactly 512 = the capacity read from SYSTEM.CFG. If the log is a ring that has
// wrapped and head is capped, then `k = head - index` can no longer locate the newest record
// — the reader would go deaf after 512 lines, which is a real defect worth knowing about.
// Look for a companion counter near head that has grown past 512.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14;
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 20000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "ptr", version: "1" });
const base = HEAD - 0x100;
const buf = Buffer.from((await req("memory.read", { address: base, size: 0x300, replacements: false })).base64, "base64");
console.log(`scanning 0x${base.toString(16).toUpperCase()} .. 0x${(base + 0x300).toString(16).toUpperCase()}`);
console.log(`head is at 0x${HEAD.toString(16).toUpperCase()} (offset ${HEAD - base})\n`);
console.log("=== 4-byte values that look like counters (1..100000) ===");
for (let o = 0; o < 0x300; o += 4) {
  const v = buf.readUInt32LE(o);
  if (v >= 1 && v <= 100000) {
    const a = base + o;
    const mark = a === HEAD ? "   <== HEAD (reads 512)" : (v > 512 ? "   <== GREW PAST CAPACITY" : "");
    console.log(`  0x${a.toString(16).toUpperCase()} = ${String(v).padStart(7)}${mark}`);
  }
}
s.close();
