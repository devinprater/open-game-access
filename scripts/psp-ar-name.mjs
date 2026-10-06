#!/usr/bin/env node
/**
 * psp-ar-name.mjs -- name the CURRENT screen from the decoded text pool next to the menu
 * struct. This is the screen-naming mechanism.
 *
 * Mechanism (measured): the game decodes the current screen's label/description text into a
 * UTF-16LE pool in the page just below the menu struct. On the main menu that pool held the
 * seven item descriptions IN ITEM ORDER:
 *   "An original story that takes place after "Trunks Another Story.""   (= Another Road)
 *   "In this mode, you fight CPU opponents one after the other ..."       (= Arcade)
 *   ... through "Edit various settings and Save/Load."                    (= Options)
 *
 * So: locate the struct (unique signature), read menuId, read the pool, and print it. If the
 * pool changes when the screen changes, the screen is nameable.
 *
 * Usage: node psp-ar-name.mjs [--page 0x08BF0000]
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
import { readFileSync } from "node:fs";
const ELF = "C:/Users/Devin Prater/AppData/Local/Temp/dbz-ar-extract/EBOOT.dec";
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar name", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
let elf = null;
try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

async function state() {
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
    struct = { base: RAM_BASE + i };
    break;
  }
  return { ram, struct };
}

function pool(ram, page, limit = 14) {
  const start = page - RAM_BASE;
  const out = [];
  for (let i = start; i + 8 < start + 0x10000 && i + 8 < ram.length; i += 2) {
    let j = i;
    while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
    if (j - i >= 16) {
      const s = ram.subarray(i, j).toString("utf16le");
      if (!inElf(s)) out.push({ addr: RAM_BASE + i, s });
      i = j;
    }
  }
  return out.slice(0, limit);
}

const PAGE = 0x08BF0000;
let st = await state();
if (!st.struct) { console.log("not on a menu"); db.s?.close(); process.exit(0); }
let menuId = st.ram.readUInt32LE(st.struct.base - RAM_BASE + 4);
let screen = st.ram.readUInt32LE(st.struct.base - RAM_BASE + 0x70);
console.log(`=== menuId=${menuId} screen=${screen} ===`);
for (const s of pool(st.ram, PAGE)) console.log(`  ${JSON.stringify(s.s)}`);

db.s?.close();
process.exit(0);
