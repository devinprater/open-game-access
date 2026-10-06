#!/usr/bin/env node
/**
 * psp-ar-table.mjs -- find the menu descriptor table in RAM BY ITS BYTES, and solve the
 * ELF->RAM delta from that. Avoids trusting any precomputed address (a documented trap).
 *
 * The table at ELF 0x1E7BA0 has a distinctive row 1:
 *   04 D2 0E 00 | 0C D2 0E 00 | 00 00 00 00 | 68 D3 0E 00 | 01 00 00 00
 * (two .text pointers, a null, a pointer, and a small count) repeated at stride 0x14.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar table", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 25000);
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

function findAll(needle, cap = 10) {
  const out = [];
  let i = ram.indexOf(needle, 0);
  while (i >= 0 && out.length < cap) { out.push(i); i = ram.indexOf(needle, i + 1); }
  return out;
}

// --- 1. the descriptor table, by bytes ---
const tableHead = Buffer.from("04D20E000CD20E0000000000 68D30E0001000000".replace(/ /g, ""), "hex");
const tHits = findAll(tableHead);
console.log(`descriptor-table row-1 pattern: ${tHits.length} hit(s)`);
for (const h of tHits) console.log(`  at RAM 0x${(RAM_BASE + h).toString(16).toUpperCase()}  (=> base = 0x${((RAM_BASE + h) - 0x14 - 0x1E7BA0).toString(16).toUpperCase()})`);

// --- 2. re-verify the base with a known .text string ---
for (const [s, elf] of [["[MENU] MESSAGE", null], ["%05ddmg", 0x001A3D30]]) {
  const hits = findAll(Buffer.from(s, "latin1"));
  console.log(`string ${JSON.stringify(s)}: ${hits.length} hit(s)` + (hits.length ? `  first RAM 0x${(RAM_BASE + hits[0]).toString(16).toUpperCase()}` : ""));
  if (elf != null && hits.length) {
    console.log(`   => base = 0x${((RAM_BASE + hits[0]) - elf).toString(16).toUpperCase()}`);
  }
}

// --- 3. is .data even resident?  read a fixed .data address both ways ---
const DESC_RAM = 0x08804000 + 0x1E7BA0;
const off = DESC_RAM - RAM_BASE;
console.log(`\nbytes at computed table addr 0x${DESC_RAM.toString(16).toUpperCase()}:`);
console.log("  " + ram.subarray(off - 0x10, off + 0x50).toString("hex").replace(/(..)/g, "$1 "));
