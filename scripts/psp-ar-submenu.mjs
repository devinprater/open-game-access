#!/usr/bin/env node
/**
 * psp-ar-submenu.mjs -- map a SUBMENU: walk every item, record its (menuId, screen, index)
 * and the decoded text-pool entry for that index.
 *
 * The main menu is menuId=1 with 7 items; Options is reached from index 6. Once inside, this
 * walks item by item, reading the struct + the text pool after each move, so an item list can
 * be built from the game's own words.
 *
 * ONE debugger connection for the whole run (the debugger allows only one client, and
 * repeated connect/disconnect leaves sockets in TIME_WAIT that break the next attempt).
 *
 * Read-only except injected presses.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";
import { readFileSync } from "node:fs";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
const POOL_PAGE = 0x08BF0000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar submenu", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
let elf = null; try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

async function snap() {
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
    struct = RAM_BASE + i; break;
  }
  let menuId = null, screen = null, sel = null;
  if (struct) {
    menuId = ram.readUInt32LE(struct - RAM_BASE + 4);
    screen = ram.readUInt32LE(struct - RAM_BASE + 0x70);
    sel = ram.readUInt32LE(struct - RAM_BASE + 0x74);
  }
  // text pool in the page below the struct (fall back to the default page)
  const page = struct ? (struct & ~0xFFFF) : POOL_PAGE;
  const start = page - RAM_BASE;
  const strs = [];
  for (let i = start; i + 8 < start + 0x10000 && i + 8 < ram.length; i += 2) {
    let j = i;
    while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
    if (j - i >= 16) {
      const s = ram.subarray(i, j).toString("utf16le");
      if (!inElf(s)) strs.push(s);
      i = j;
    }
  }
  return { struct, menuId, screen, sel, strs, page };
}

function show(tag, st) {
  if (!st.struct) { console.log(`${tag.padEnd(20)} no struct (off the menu system)`); return; }
  console.log(`${tag.padEnd(20)} menuId=${st.menuId} screen=${st.screen} sel=${st.sel}  pool=${st.strs.length} strings @0x${st.page.toString(16).toUpperCase()}`);
  for (const s of st.strs.slice(0, 10)) console.log(`      ${JSON.stringify(s)}`);
}

console.log("=== task 2: map the Options submenu ===");
let st = await snap();
show("start", st);
if (!st.struct) { console.log("Not on a menu -- navigate to the main menu first."); db.s?.close(); process.exit(0); }

// go to the bottom item (Options), then enter it
for (let i = 0; i < 10; i++) { press("down"); await sleep(500); }
st = await snap();
console.log(`\nafter walking to the last item: menuId=${st.menuId} sel=${st.sel}`);
press("cross"); await sleep(2800);
st = await snap();
show("\nafter cross (Options?)", st);
if (!st.struct) { console.log("no menu here -- the item entered a game mode"); db.s?.close(); process.exit(0); }

console.log("\n--- walking items: down x6, then up x2 ---");
for (const b of ["down", "down", "down", "down", "down", "down", "up", "up"]) {
  press(b); await sleep(700);
  st = await snap();
  console.log(`  after ${b.padEnd(4)} menuId=${st.menuId} screen=${st.screen} sel=${st.sel}`);
}
console.log("\n--- final text pool (candidate item labels/descriptions) ---");
for (const s of st.strs.slice(0, 30)) console.log(`  ${JSON.stringify(s)}`);
db.s?.close();
process.exit(0);
