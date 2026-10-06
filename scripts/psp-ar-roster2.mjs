#!/usr/bin/env node
/**
 * psp-ar-roster2.mjs -- dump the roster pointer table at 0x08A243F0 and its neighbours, to
 * find the table's true size and any PARALLEL unlock array.
 *
 * Established: 0x08A243F0 is a contiguous array of 4-byte pointers into the name block
 * (0x08A2558A onward). A game with unlockable characters usually keeps a parallel array of
 * flags or ids for the same slot count, so both sides of the table are dumped.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
const TABLE = 0x08A243F0;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class D {
  constructor() { this.q = new Map(); this.t = 1; }
  connect() {
    return new Promise((res, rej) => {
      const s = new WebSocket(URL, "debugger.ppsspp.org");
      this.s = s;
      s.addEventListener("message", (e) => {
        let m; try { m = JSON.parse(String(e.data)); } catch { return; }
        if (m.ticket != null && this.q.has(String(m.ticket))) {
          const it = this.q.get(String(m.ticket)); this.q.delete(String(m.ticket));
          clearTimeout(it.timer);
          m.event === "error" ? it.rej(new Error(m.message || "request failed")) : it.res(m);
        }
      });
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar roster2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 30000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new D();
await db.connect();
// read a window well before and after the table
const FROM = TABLE - 0x100, SIZE = 0x1200;
const buf = await db.read(FROM, SIZE);

// resolve a pointer into the name block by reading the string it points to
async function nameAt(ptr) {
  if (ptr < 0x08A25500 || ptr > 0x08A25800) return null;
  const b = await db.read(ptr, 24);
  const s = b.toString("utf16le");
  return s.split("\u0000")[0].trim();
}

console.log(`=== window 0x${FROM.toString(16).toUpperCase()} .. 0x${(FROM + SIZE).toString(16).toUpperCase()} ===`);
let inTable = 0;
for (let i = 0; i < SIZE; i += 4) {
  const addr = FROM + i;
  const v = buf.readUInt32LE(i);
  let note = "";
  if (v >= 0x08A25500 && v <= 0x08A25800) {
    const n = await nameAt(v);
    if (n) { note = `  name="${n}"`; inTable++; }
  }
  if (addr === TABLE) note += "   <== TABLE START";
  if (note || (v > 0 && v < 0x100)) console.log(`  0x${addr.toString(16).toUpperCase()}  ${String(v).padStart(10)}  0x${v.toString(16).padStart(8, "0")}${note}`);
}
console.log(`\nname pointers seen: ${inTable}`);
db.s?.close();
process.exit(0);
