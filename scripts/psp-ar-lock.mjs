#!/usr/bin/env node
/**
 * psp-ar-lock.mjs -- the Customize screen shows an explicit LOCK icon, which is a direct
 * unlock indicator. Capture the screen's state and look for what could drive it.
 *
 * Also records the screen's full decoded text, so the character and the lock's wording are
 * known rather than assumed.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar lock", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
let elf = null; try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

// screen text: the 0x8BF0000 page (menu/system text)
function page(addr, label) {
  const start = addr - RAM_BASE;
  const out = [];
  for (let i = start; i + 12 < start + 0x10000 && i + 12 < ram.length; i += 2) {
    let j = i;
    while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
    if (j - i >= 12) { const s = ram.subarray(i, j).toString("utf16le"); if (!inElf(s)) out.push(s); i = j; }
  }
  console.log(`--- ${label} (${out.length} strings) ---`);
  for (const s of out.slice(0, 26)) console.log(`   ${JSON.stringify(s)}`);
}
page(0x08BF0000, "0x08BF0000");
console.log("");
page(0x08C00000, "0x08C00000");

// Does the roster table region hold anything keyed per character that could be a lock state?
// Print 0x40 before the table (0x08A243EC) and 0x80 after its 24th entry.
const T = 0x08A243EC;
console.log(`\n--- around the roster table 0x${T.toString(16).toUpperCase()} ---`);
for (let o = T - 0x40 - RAM_BASE; o < T + 0x18 * 4 - RAM_BASE; o += 4) {
  const v = ram.readUInt32LE(o);
  const isPtr = v >= 0x08A25000 && v <= 0x08A26000;
  console.log(`  0x${(RAM_BASE + o).toString(16).toUpperCase()}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}${isPtr ? "  <ptr>" : ""}`);
}
process.exit(0);
