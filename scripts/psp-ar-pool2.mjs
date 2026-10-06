#!/usr/bin/env node
/**
 * psp-ar-pool2.mjs -- dump the RESIDENT menu text pool in full.
 *
 * Correction to an earlier inference: the pool is NOT per-screen. On the main menu and in
 * Options it held the SAME 174 strings, i.e. it is resident text for many screens
 * (the "a resident string pool is not a menu" trap). So a screen's items must be identified
 * among the pool, not assumed to be all of it.
 *
 * Writes every UTF-16LE string not present in the ELF, so the labels for a given submenu can
 * be found by name.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
import { readFileSync, writeFileSync } from "node:fs";
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar pool2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// struct state for context
let menuId = null, screen = null;
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
  if ((v - TABLE) % STRIDE !== 0) continue;
  const id = (v - TABLE) / STRIDE;
  if (ram.readUInt32LE(i + 4) !== id) continue;
  menuId = ram.readUInt32LE(i + 4); screen = ram.readUInt32LE(i + 0x70);
  break;
}
console.log(`context: menuId=${menuId} screen=${screen}`);

// ALL utf16 strings >=4 chars not in the ELF, across RAM
const all = [];
for (let i = 0; i + 8 <= ram.length; i += 2) {
  let j = i;
  while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
  if (j - i >= 8) {
    const s = ram.subarray(i, j).toString("utf16le");
    if (!inElf(s)) all.push({ addr: RAM_BASE + i, s });
    i = j;
  }
}
console.log(`total UTF-16LE strings not in the ELF: ${all.length}`);
const lines = all.map((a) => `0x${a.addr.toString(16).toUpperCase()}\t${a.s}`);
writeFileSync("C:/Users/Public/dbzar-ui-strings.txt", lines.join("\n") + "\n");
console.log("wrote C:/Users/Public/dbzar-ui-strings.txt");

// show the menu-relevant slice: the 0x8BF0000 page in order
const slice = all.filter((a) => a.addr >= 0x08BF0000 && a.addr < 0x08C00000);
console.log(`\n--- pool page 0x08BF0000 (${slice.length} strings, in address order) ---`);
slice.forEach((a, n) => console.log(`${String(n).padStart(3)}  0x${a.addr.toString(16).toUpperCase()}  ${JSON.stringify(a.s)}`));
process.exit(0);
