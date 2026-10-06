#!/usr/bin/env node
/**
 * psp-ar-csanchor.mjs -- dump the region around the proven character cursor (0x08ABC2E8) and
 * search all RAM for the display-order TABLE, so the reader can re-locate the cursor every
 * boot (addresses here are per-boot; signatures are stable).
 *
 * Proven so far: the word at 0x08ABC2E8 holds the NAME-TABLE id of the highlighted character
 * and follows the display order: 2,18,3,4,23,5,6,7,8,9,19,10,11,12 ...
 * The 16 selectable ids in display order are 0,1,2,18,3,4,23,5,6,7,8,9,19,10,11,12
 * (Cooler=11 sits between Kid Buu and Broly -- the earlier "15 characters" run missed it
 *  because one OCR step came back empty).
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const CURSOR = 0x08ABC2E8;
const DISPLAY = [0, 1, 2, 18, 3, 4, 23, 5, 6, 7, 8, 9, 19, 10, 11, 12];

class Debugger {
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar csanchor", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

const db = new Debugger();
await db.connect();
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();
console.log(`ram ${ram.length}`);

console.log(`\n=== window around the cursor 0x${CURSOR.toString(16).toUpperCase()} ===`);
for (let a = CURSOR - 0x80; a < CURSOR + 0x60; a += 4) {
  const v = ram.readUInt32LE(a - RAM_BASE);
  const mark = a === CURSOR ? "  <== CURSOR (current char id)" : "";
  console.log(`  0x${a.toString(16).toUpperCase()}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}${mark}`);
}

// search RAM for the display-order TABLE (16 u32s in display order)
console.log("\n=== display-order table in RAM (u32) ===");
const pat = Buffer.alloc(DISPLAY.length * 4);
DISPLAY.forEach((v, i) => pat.writeUInt32LE(v, i * 4));
let hits = [];
let i = ram.indexOf(pat);
while (i !== -1) { hits.push(RAM_BASE + i); i = ram.indexOf(pat, i + 1); }
console.log(`  ${hits.length} hit(s)`);
for (const h of hits.slice(0, 10)) console.log(`    0x${h.toString(16).toUpperCase()}`);

// u8 table too
console.log("\n=== display-order table in RAM (u8) ===");
const pat8 = Buffer.from(DISPLAY);
hits = []; i = ram.indexOf(pat8);
while (i !== -1) { hits.push(RAM_BASE + i); i = ram.indexOf(pat8, i + 1); }
console.log(`  ${hits.length} hit(s)`);
for (const h of hits.slice(0, 10)) console.log(`    0x${h.toString(16).toUpperCase()}`);

// what points AT the cursor address? (a struct holding it)
console.log("\n=== words pointing at the cursor address ===");
const cur = Buffer.alloc(4); cur.writeUInt32LE(CURSOR);
hits = []; i = ram.indexOf(cur);
while (i !== -1) { if (RAM_BASE + i !== CURSOR) hits.push(RAM_BASE + i); i = ram.indexOf(cur, i + 1); }
console.log(`  ${hits.length} hit(s)`);
for (const h of hits.slice(0, 10)) console.log(`    0x${h.toString(16).toUpperCase()}  (holds the cursor ADDRESS)`);
process.exit(0);
