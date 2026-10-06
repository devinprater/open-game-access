#!/usr/bin/env node
/**
 * psp-ar-roster.mjs -- locate the CHARACTER RECORDS and any unlock state.
 *
 * Facts established: a contiguous 24-name roster block sits at 0x08A2558A ("Goku",
 * "Teen Gohan", ... "Future Trunks"), followed immediately by the game's unlock messages
 * ("%s has become available!"). So the roster exists; the question is what records carry the
 * names and whether a per-character unlock flag is readable.
 *
 * Method: for each roster name, find every word in RAM that could reference it -- an absolute
 * pointer to the string, or a small offset that resolves near the block. A record table would
 * show the SAME reference scheme repeated 24 times, which is the signature to look for.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;

const NAMES = ["Goku", "Teen Gohan", "Gohan", "Vegeta", "Trunks", "Krillin", "Piccolo",
  "Frieza", "Android #18", "Cell", "Kid Buu", "Cooler", "Broly", "Gotenks", "Gogeta",
  "Vegito", "Pikkon", "Janemba", "Future Gohan", "Majin Buu", "Super Buu", "Dabura",
  "Bardock", "Future Trunks"];

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar roster", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();

// verify the block and record each name's address
console.log("=== roster block ===");
const addrs = [];
for (const n of NAMES) {
  const b = Buffer.from(n + " ", "utf16le");
  const i = ram.indexOf(b);
  const hit = i >= 0 ? RAM_BASE + i : null;
  if (hit) addrs.push({ n, addr: hit });
  console.log(`  ${n.padEnd(14)} ${hit ? "0x" + hit.toString(16).toUpperCase() : "NOT FOUND"}`);
}
const lo = Math.min(...addrs.map((a) => a.addr)), hi = Math.max(...addrs.map((a) => a.addr));
console.log(`block range 0x${lo.toString(16).toUpperCase()} .. 0x${hi.toString(16).toUpperCase()}  (${addrs.length} names)`);

// look for a run of 24 words that are absolute pointers into the block, or offsets resolving into it
console.log("\n=== absolute pointers into the roster block ===");
const ptrHits = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v >= lo - 8 && v <= hi + 40) ptrHits.push({ at: RAM_BASE + i, v });
}
console.log(`  ${ptrHits.length} words point into/near the block`);
for (const h of ptrHits.slice(0, 40)) console.log(`    holder 0x${h.at.toString(16).toUpperCase()} -> 0x${h.v.toString(16).toUpperCase()}`);

// how far apart are the names? (tells whether a table can be indexed by name stride)
console.log("\n=== name spacing ===");
for (let i = 1; i < addrs.length; i++) process.stdout.write(`${(addrs[i].addr - addrs[i - 1].addr).toString(16)} `);
console.log("");
process.exit(0);
