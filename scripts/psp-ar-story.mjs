#!/usr/bin/env node
/**
 * psp-ar-story.mjs -- read the Another Road story-mode state live.
 *
 * From the decompile (story2.txt):
 *   FUN_000323bc "AR MAIN"      story mode entry
 *   DAT_001b11d5  (vaddr 0x1B11D5)  CHAPTER INDEX, byte; clamped to cRam00099dac - 1
 *   DAT_001b11d8  (vaddr 0x1B11D8)  chapter table, u32 * index -> chapter id
 *   cRam00099dac  (vaddr 0x99DAC)   chapter COUNT
 *   per-chapter records at 0x99DAE, stride 0x4a (74 bytes)
 *   AR state struct: FUN_000213c4() returns 0x6b90; flags at +0x6e4 and +0x6f0
 *   FUN_000325e8 advances DAT_001b11d5 by 1 on a pad flag (0x40), decrements on another (0x10)
 *
 * RAM = 0x08804000 + ELF vaddr (verified 5 ways in DECOMP_INDEX.md).
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const BASE = 0x08804000;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar story", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 20000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new Debugger();
await db.connect();

function show(ram, addr, len, label) {
  console.log(`\n--- ${label} @0x${addr.toString(16).toUpperCase()} ---`);
  for (let i = 0; i < len; i += 16) {
    const row = [];
    const asc = [];
    for (let j = 0; j < 16 && i + j < len; j++) {
      const v = ram[i + j];
      row.push(v.toString(16).padStart(2, "0"));
      asc.push(v >= 0x20 && v < 0x7f ? String.fromCharCode(v) : ".");
    }
    const a = addr + i;
    console.log(`  0x${a.toString(16).toUpperCase()}  ${row.join(" ").padEnd(48)}  ${asc.join("")}`);
  }
}

// chapter index + count + table (static .data)
const addrIdx = BASE + 0x1B11D5;          // chapter index
const addrTbl = BASE + 0x1B11D8;          // chapter table
const addrCnt = BASE + 0x99DAC;           // chapter count
const addrRec = BASE + 0x99DAE;           // per-chapter records, stride 0x4a

const ramIdx = await db.read(addrIdx - 8, 0x20);
show(ramIdx, addrIdx - 8, 0x20, "chapter index area (DAT_001b11d5 at +8)");

const ramTbl = await db.read(addrTbl, 0x40);
show(ramTbl, addrTbl, 0x40, "chapter table (DAT_001b11d8) as u32");
console.log("   as u32:", Array.from({ length: 8 }, (_, i) => "0x" + ramTbl.readUInt32LE(i * 4).toString(16).toUpperCase()).join(", "));

const ramCnt = await db.read(addrCnt - 0x10, 0x30);
show(ramCnt, addrCnt - 0x10, 0x30, "chapter count area (cRam00099dac at +0x10)");

// per-chapter records: stride 0x4a. Show the first 3 records.
for (let c = 0; c < 3; c++) {
  const a = addrRec + c * 0x4a;
  const r = await db.read(a, 0x4a);
  show(r, a, 0x4a, `chapter record ${c}`);
}

const chapIdx = (await db.read(addrIdx, 1))[0];
const chapCnt = (await db.read(addrCnt, 1))[0];
console.log(`\n=== SUMMARY ===`);
console.log(`chapter index (DAT_001b11d5) = ${chapIdx}`);
console.log(`chapter count (cRam00099dac) = ${chapCnt}`);
db.s?.close();
process.exit(0);
