#!/usr/bin/env node
/**
 * psp-ar-struct2.mjs -- with the active-menu struct FOUND by its (pointer->table, id)
 * signature, dump it and identify the SELECTION field.
 *
 * Locates the struct itself each run (no hardcoded address), then:
 *   - prints the whole struct area
 *   - presses down/down/up and reports which field tracks the selection
 *
 * This is the payload of Experiment A: an unambiguous locator plus a field that should hold
 * the selected item, replacing the (len,index) heap-pair guesswork.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar struct2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

console.log("=== locate active-menu struct ===");
let hits = await locate();
for (const h of hits) console.log(`  struct 0x${h.base.toString(16).toUpperCase()}  menuId=${h.id}`);
if (!hits.length) { console.log("  none found -- not on a menu?"); db.s?.close(); process.exit(0); }

const base = hits[0].base;
console.log(`\n=== struct dump @0x${base.toString(16).toUpperCase()} (using first hit) ===`);
const buf = await db.read(base, 0x120);
for (let i = 0; i < 0x120; i += 4) {
  const v = buf.readUInt32LE(i);
  const marks = [];
  if (i === 0x70) marks.push("<== +0x70 (decomp: screen id)");
  if (i === 0x74) marks.push("<== +0x74 (decomp: selection)");
  if (v > 0 && v < 0x40) marks.push("small");
  if (v >= 0x08800000 && v < 0x0A000000) marks.push("ptr");
  if (v !== 0 || marks.length) console.log(`  +0x${i.toString(16).padStart(3, "0")}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}  ${marks.join(" ")}`);
}

console.log("\n=== does any struct word track the selection? (down, down, up) ===");
const series = new Map();
const readStruct = async () => db.read(base, 0x120);
let prev = await readStruct();
series.set("start", prev);
for (const b of ["down", "down", "up"]) {
  press(b); await sleep(800);
  series.set(b, await readStruct());
}
const keys = ["start", "down", "down", "up"];
const snaps = keys.map((k, i) => (i === 0 ? series.get("start") : series.get("down") && i === 1 ? series.get("down") : null));
// simpler: collect in order
const order = ["start", "down1", "down2", "up"];
const collect = [series.get("start"), series.get("down"), series.get("up")];
console.log("(press sequence: start, down, down, up -- re-collecting explicitly)");
const vals = [];
const s0 = await readStruct(); vals.push(s0);
for (const b of ["down", "down", "up"]) { press(b); await sleep(800); vals.push(await readStruct()); }
for (let off = 0; off < 0x120; off += 4) {
  const w = vals.map((v) => v.readUInt32LE(off));
  if (new Set(w).size > 1 && w.every((x) => x <= 0x40)) {
    console.log(`  +0x${off.toString(16).padStart(3, "0")}  series=[${w.join(",")}]  ${off === 0x74 ? "  <== THE DECOMP'S SELECTION FIELD" : ""}`);
  }
}
db.s?.close();
process.exit(0);
