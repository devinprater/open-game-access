#!/usr/bin/env node
/**
 * psp-ar-flag2.mjs -- focused search for the unlock flag in ROSTER ORDER.
 *
 * The cycle proved 15 selectable and 9 locked. In roster order (Goku..Future Trunks = the 24
 * names) the expected pattern is:
 *    first 15 available, last 9 locked  ->  111111111111111000000000
 * or the inverse if the table is stored locked-first:
 *    000000000111111111111111
 *
 * The previous scan matched the second shape in a bitmap region hundreds of times, which is
 * coincidence, not evidence. This one requires the EXACT roster ordering and reports the hit
 * count, so a coincidence is visible as a high count.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
import { readFileSync } from "node:fs";
const ELF = "C:/Users/Devin Prater/AppData/Local/Temp/dbz-ar-extract/EBOOT.dec";

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar flag2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// roster names, in the order the game lists them (from the verified table)
const NAMES = ["Goku","Teen Gohan","Gohan","Vegeta","Trunks","Krillin","Piccolo","Frieza",
  "Android #18","Cell","Kid Buu","Cooler","Broly","Gotenks","Gogeta","Vegito","Pikkon",
  "Janemba","Future Gohan","Majin Buu","Super Buu","Dabura","Bardock","Future Trunks"];
// the 15 proven selectable (by the cycle)
const SELECTABLE = new Set(["Goku","Teen Gohan","Gohan","Future Gohan","Vegeta","Trunks",
  "Future Trunks","Krillin","Piccolo","Frieza","Android #18","Cell","Majin Buu","Kid Buu","Broly"]);
const expected = NAMES.map((n) => (SELECTABLE.has(n) ? 1 : 0));
console.log("roster order:  " + NAMES.join(", "));
console.log("expected flag: " + expected.join("") + "   (1=selectable, 15 ones)");

function find(pat, label) {
  const hits = [];
  for (let i = 0; i + pat.length <= ram.length; i++) {
    let ok = true;
    for (let k = 0; k < pat.length; k++) if (ram[i + k] !== pat[k]) { ok = false; break; }
    if (ok) hits.push(RAM_BASE + i);
  }
  console.log(`\n${label}: ${hits.length} hit(s)`);
  for (const h of hits.slice(0, 20)) console.log(`   0x${h.toString(16).toUpperCase()}`);
  return hits;
}

find(expected, "exact roster-order flag (u8)");
find(expected.map((x) => (x ? 0 : 1)), "inverted roster-order flag (u8)");
// stride 4 (u32 per slot, 0/1)
const pat32 = [];
for (const v of expected) pat32.push(v, 0, 0, 0);
find(pat32, "roster-order flag, u32 stride (u8 view)");
// also look for the 24 name pointers and check nearby for a parallel flag
console.log("\n=== is there anything flag-like just AFTER the roster name pointers? ===");
const NAME_LO = 0x08A25550, NAME_HI = 0x08A25800;
let table = null;
for (let i = 0; i + 4 * 24 <= ram.length; i += 4) {
  let run = 0;
  for (let k = 0; k < 24; k++) { const v = ram.readUInt32LE(i + k * 4); if (v >= NAME_LO && v <= NAME_HI) run++; else break; }
  if (run >= 24 && !table) table = RAM_BASE + i;
}
console.log(`24-run of name pointers at: ${table ? "0x" + table.toString(16).toUpperCase() : "none"}`);
process.exit(0);
