#!/usr/bin/env node
/**
 * psp-ar-text.mjs -- name a menu screen from the text the game is actually DISPLAYING.
 *
 * Why this route: the message tables hold symbolic ids (MSG_AR_CITY_00), not readable
 * labels, and the display text lives in .AMT/.MGB containers. But the game must DECODE that
 * text to draw it, so the readable form exists in RAM. Reading RAM avoids the container
 * format entirely.
 *
 * Method: anchor the active-menu struct (unique signature), read menuId + screen id, dump
 * RAM, extract ASCII and UTF-16LE strings, and keep only those NOT present in the ELF
 * (a string in the ELF is a static constant, not something the screen produced).
 *
 * Usage: node psp-ar-text.mjs
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
const ELF = "C:/Users/Devin Prater/AppData/Local/Temp/dbz-ar-extract/EBOOT.dec";

import { readFileSync } from "node:fs";

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar text", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// locate the struct
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
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
if (!struct) { console.log("not on a menu (no struct)"); db.s?.close(); process.exit(0); }
const menuId = ram.readUInt32LE(struct.base - RAM_BASE + 0x04);
const screen = ram.readUInt32LE(struct.base - RAM_BASE + 0x70);
console.log(`menuId=${menuId}  screen(+0x70)=${screen}`);

// ELF membership set: any string found in the ELF is a static constant
let elf = null;
try { elf = readFileSync(ELF); } catch (e) { console.log("(ELF not readable: " + e.message + ")"); }

function inElf(s) {
  return elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false;
}

// extract printable ASCII runs
function asciiRuns(buf, min) {
  const out = [];
  let start = -1;
  for (let i = 0; i <= buf.length; i++) {
    const c = i < buf.length ? buf[i] : 0;
    const printable = c >= 0x20 && c < 0x7f;
    if (printable) { if (start < 0) start = i; }
    else { if (start >= 0 && i - start >= min) out.push({ off: start, s: buf.subarray(start, i).toString("latin1") }); start = -1; }
  }
  return out;
}
// extract UTF-16LE runs
function utf16Runs(buf, min) {
  const out = [];
  let start = -1;
  for (let i = 0; i + 1 < buf.length; i += 2) {
    const lo = buf[i], hi = buf[i + 1];
    const printable = hi === 0 && lo >= 0x20 && lo < 0x7f;
    if (printable) { if (start < 0) start = i; }
    else { if (start >= 0 && i - start >= min * 2) out.push({ off: start, s: buf.subarray(start, i).toString("utf16le") }); start = -1; }
  }
  return out;
}

const ascii = asciiRuns(ram, 5).filter((r) => !inElf(r.s));
const u16 = utf16Runs(ram, 4).filter((r) => !inElf(r.s));
console.log(`\nstrings NOT in the ELF (i.e. produced/decoded at runtime):`);
console.log(`  ASCII runs: ${ascii.length}   UTF-16LE runs: ${u16.length}`);
console.log(`\n--- ASCII (first 45) ---`);
for (const r of ascii.slice(0, 45)) console.log(`  0x${(RAM_BASE + r.off).toString(16).toUpperCase()}  ${JSON.stringify(r.s.slice(0, 70))}`);
console.log(`\n--- UTF-16LE (first 25) ---`);
for (const r of u16.slice(0, 25)) console.log(`  0x${(RAM_BASE + r.off).toString(16).toUpperCase()}  ${JSON.stringify(r.s.slice(0, 70))}`);
db.s?.close();
process.exit(0);
