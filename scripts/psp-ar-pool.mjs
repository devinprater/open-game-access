#!/usr/bin/env node
/**
 * psp-ar-pool.mjs -- is the DECODED UI text in one pool, so a screen can be named from it?
 *
 * psp-ar-text.mjs found readable UTF-16LE UI strings at ~0x08A24B00 ("Memory Stick Duo",
 * "Cancel,", "Return to Main Menu.", "Keep game data?"), which are the game's own displayed
 * text decoded at runtime. This maps the extent of that pool and reports the strings by
 * region, so the label for a menu screen can be found next to the screen's own id.
 *
 * Strings found in the ELF are static constants; they are filtered out.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar pool", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// struct state
let struct = null;
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
  if ((v - TABLE) % STRIDE !== 0) continue;
  const id = (v - TABLE) / STRIDE;
  if (ram.readUInt32LE(i + 4) !== id) continue;
  struct = { base: RAM_BASE + i, id };
  break;
}
if (struct) {
  console.log(`menuId=${ram.readUInt32LE(struct.base - RAM_BASE + 4)}  screen(+0x70)=${ram.readUInt32LE(struct.base - RAM_BASE + 0x70)}  sel=${ram.readUInt32LE(struct.base - RAM_BASE + 0x74)}`);
} else console.log("(no menu struct -- not on a menu)");

let elf = null;
try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

// UTF-16LE strings, >=6 chars, not in the ELF, grouped by 0x10000 page
const runs = [];
for (let i = 0; i + 12 <= ram.length; i += 2) {
  let j = i;
  while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
  if (j - i >= 12) {
    const s = ram.subarray(i, j).toString("utf16le");
    if (!inElf(s)) runs.push({ off: i, s });
    i = j;
  }
}
console.log(`\nUTF-16LE strings >=6 chars, not in the ELF: ${runs.length}`);
const pages = new Map();
for (const r of runs) {
  const p = (RAM_BASE + r.off) & ~0xFFFF;
  if (!pages.has(p)) pages.set(p, []);
  pages.get(p).push(r);
}
console.log("\nby 64 KB page:");
for (const [p, list] of [...pages.entries()].sort((a, b) => b[1].length - a[1].length).slice(0, 8)) {
  console.log(`  0x${p.toString(16).toUpperCase()}  ${list.length} strings`);
}
// print the densest page's strings (the UI pool)
const densest = [...pages.entries()].sort((a, b) => b[1].length - a[1].length)[0];
if (densest) {
  console.log(`\n--- densest page 0x${densest[0].toString(16).toUpperCase()} (${densest[1].length} strings) ---`);
  for (const r of densest[1].slice(0, 60)) console.log(`  0x${(RAM_BASE + r.off).toString(16).toUpperCase()}  ${JSON.stringify(r.s)}`);
}
db.s?.close();
process.exit(0);
