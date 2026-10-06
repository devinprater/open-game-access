#!/usr/bin/env node
/**
 * psp-ar-screenid3.mjs -- prove the active-menu struct is a SCREEN DISCRIMINATOR.
 *
 * If entering a submenu changes the locator's menuId, then menuId names the screen and the
 * reader no longer needs to guess from a list length. This is the deliverable test.
 *
 * One connection, one run (the debugger allows only one client).
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar screenid3", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

async function locate() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  const ram = Buffer.concat(parts);
  const hits = [];
  for (let i = 0; i + 8 <= ram.length; i += 4) {
    const v = ram.readUInt32LE(i);
    if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
    if ((v - TABLE) % STRIDE !== 0) continue;
    const id = (v - TABLE) / STRIDE;
    if (ram.readUInt32LE(i + 4) !== id) continue;
    hits.push({ base: RAM_BASE + i, id });
  }
  return hits;
}

async function report(tag) {
  const hits = await locate();
  if (!hits.length) { console.log(`${tag.padEnd(24)} no struct (off-menu)`); return null; }
  const h = hits[0];
  const b = await db.read(h.base, 0x80);
  console.log(`${tag.padEnd(24)} struct@0x${h.base.toString(16).toUpperCase()}  menuId=${h.id}  screen(+0x70)=${b.readUInt32LE(0x70)}  sel(+0x74)=${b.readUInt32LE(0x74)}  hits=${hits.length}`);
  return h.id;
}

console.log("=== screen-discriminator test ===");
const id0 = await report("on current screen");
press("cross"); await sleep(2600);
const id1 = await report("after cross (enter)");
if (id1 === null) { console.log("=> entering left the menu system (expected for a game-mode entry)"); }
else if (id1 !== id0) { console.log(`=> menuId CHANGED ${id0} -> ${id1}: the struct IS a screen discriminator`); }
else { console.log(`=> menuId unchanged (${id0}): either same screen or a submenu sharing the id`); }
press("circle"); await sleep(2400);
await report("after circle (back)");
db.s?.close();
process.exit(0);
