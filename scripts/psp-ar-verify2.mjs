#!/usr/bin/env node
/**
 * psp-ar-verify2.mjs -- verify the ACTIVE-MENU STRUCT route end to end.
 *
 * Locator (unique, proven): the word W such that W points into the descriptor table at
 * 0x089EBBA0 on a row boundary AND the next word equals the row id. Exactly one match was
 * found while a menu was on screen (vs 3,921 for the pattern-matching route).
 *
 * The struct is piRam00034660 from the decompile, with:
 *   +0x000 = table + menuId*0x14     +0x004 = menuId
 *   +0x070 = screen id               +0x074 = SELECTION
 *
 * This checks that +0x74 really tracks the selection: locate, then press down/down/up and
 * report the field each time. A field that moves on every press (including a no-op) is a
 * counter; one that moves only on real selection changes is the selection.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar verify2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}
async function locate() {
  const ram = await dumpAll();
  for (let i = 0; i + 8 <= ram.length; i += 4) {
    const v = ram.readUInt32LE(i);
    if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
    if ((v - TABLE) % STRIDE !== 0) continue;
    const id = (v - TABLE) / STRIDE;
    if (ram.readUInt32LE(i + 4) !== id) continue;
    return { base: RAM_BASE + i, id, ram };
  }
  return null;
}

let loc = await locate();
if (!loc) { console.log("no active-menu struct found -- not on a menu screen"); db.s?.close(); process.exit(0); }
console.log(`locator: struct @0x${loc.base.toString(16).toUpperCase()}  menuId=${loc.id}`);

const readFields = async () => {
  const b = await db.read(loc.base, 0x80);
  return { id: b.readUInt32LE(0x04), screen: b.readUInt32LE(0x70), sel: b.readUInt32LE(0x74) };
};

console.log("\nsequence: (start) down down down up up   [+ a no-press control]");
let st = await readFields();
console.log(`  start                id=${st.id} screen=${st.screen} sel=${st.sel}`);
await sleep(1500);
st = await readFields();
console.log(`  NO PRESS (control)   id=${st.id} screen=${st.screen} sel=${st.sel}`);
for (const b of ["down", "down", "down", "up", "up"]) {
  press(b); await sleep(750);
  st = await readFields();
  console.log(`  after ${b.padEnd(5)}          id=${st.id} screen=${st.screen} sel=${st.sel}`);
}

// re-locate: the struct should still be resolvable and consistent
const again = await locate();
console.log(`\nre-locate after input: ${again ? `0x${again.base.toString(16).toUpperCase()} id=${again.id}` : "none"}  (same base: ${again && again.base === loc.base})`);
db.s?.close();
process.exit(0);
