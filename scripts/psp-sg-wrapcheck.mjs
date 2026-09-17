// Does the message log WRAP once head reaches capacity?
// If it does, two consequences: (1) the newest record's position is the WRITE POINTER, not
// head; (2) the "head increased" trigger CANNOT fire after 512 lines, so a reader would go
// deaf exactly when the story is longest.
// Test: print the records around the wrap boundary (k = CAP-8 .. CAP-1 and k = 0..8) and
// judge by CONTENT whether k=CAP-1 is followed by k=0 (wrapped) or whether k=CAP-1 is a
// very old line (not wrapped).
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC, CAP = 512;
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 20000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "wrapcheck", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const lb = await u32(LB);
const buf = Buffer.from((await req("memory.read", { address: lb, size: CAP * ST, replacements: false })).base64, "base64");
const dec = b => { const c = b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)).toString("latin1");
  return c.replace(/\x81[\x67\x68]/g, "").replace(/\x81\x43/g, ", ").replace(/\x81[\x65\x66]/g, "'")
          .replace(/\x81\xf4/g, "").replace(/\s+/g, " ").trim(); };
const rec = k => {
  const o = k * ST;
  const nm = dec(buf.subarray(o + 0x1c, o + 0x1c + 0x28));
  const tx = dec(buf.subarray(o + 0x48, o + 0x48 + 0x60));
  return { nm, tx, empty: !nm && !tx };
};
console.log(`head=${await u32(HEAD)}  capacity=${CAP}  logbase=0x${lb.toString(16).toUpperCase()}\n`);
console.log("=== records at the END of the ring (k = 504..511) ===");
for (let k = 504; k < CAP; k++) { const r = rec(k); console.log(`  k=${k}  ${r.empty ? "(empty)" : JSON.stringify((r.nm ? r.nm + ": " : "") + r.tx).slice(0, 88)}`); }
console.log("\n=== records at the START of the ring (k = 0..10) ===");
for (let k = 0; k <= 10; k++) { const r = rec(k); console.log(`  k=${k}  ${r.empty ? "(empty)" : JSON.stringify((r.nm ? r.nm + ": " : "") + r.tx).slice(0, 88)}`); }
// count how many records are non-empty, and where the empty gap is
const filled = [];
for (let k = 0; k < CAP; k++) if (!rec(k).empty) filled.push(k);
console.log(`\nnon-empty records: ${filled.length} of ${CAP}`);
console.log(`  first filled k=${filled[0]}   last filled k=${filled[filled.length-1]}`);
// is there a contiguous run, and where are the holes?
const holes = [];
for (let k = filled[0]; k <= filled[filled.length-1]; k++) if (!filled.includes(k)) holes.push(k);
console.log(`  holes inside the range: ${holes.length ? holes.slice(0,20).join(", ") : "(none)"}`);
s.close();
