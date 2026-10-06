#!/usr/bin/env node
/**
 * psp-ar-charsel.mjs -- reach CHARACTER SELECT and read its state.
 *
 * The roster table (0x08A243F0, 24 names) has no static reference in the ELF, so it is built
 * at runtime; the unlock state therefore has to be observed live.
 *
 * This: walks main menu -> Training -> character select, then
 *   1. re-finds the roster table,
 *   2. looks for a 24-entry PARALLEL array of small values near it (a lock/availability flag
 *      array would have the same slot count),
 *   3. runs the ordinal hunt (no-press control, down/down/up) to find the character cursor.
 *
 * One debugger connection for the whole run.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar charset", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const press = (b, n = 1) => { for (let i = 0; i < n; i++) execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" }); };
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

console.log("navigating: main menu -> Training (down x4) -> cross");
press("up", 12); await sleep(600);
press("down", 4); await sleep(800);
press("cross"); await sleep(4000);

let ram = await dumpAll();
db.s?.close();

// 1. re-find the roster pointer table: a run of words pointing into the name block
const NAME_LO = 0x08A25550, NAME_HI = 0x08A25800;
let best = null;
for (let i = 0; i + 4 * 8 <= ram.length; i += 4) {
  let run = 0;
  for (let k = 0; k < 40; k++) {
    const v = ram.readUInt32LE(i + k * 4);
    if (v >= NAME_LO && v <= NAME_HI) run++; else break;
  }
  if (run >= 8 && (!best || run > best.run)) best = { at: RAM_BASE + i, run };
}
if (!best) { console.log("roster table not found on this screen"); process.exit(0); }
console.log(`roster table @0x${best.at.toString(16).toUpperCase()}  (${best.run} contiguous name pointers)`);

// 2. parallel arrays near the table: 24 consecutive entries of small values
console.log("\n=== looking for a 24-entry parallel array near the table ===");
const base = best.at - RAM_BASE;
for (const [tag, start, stride] of [["before u32", base - 0x100, 4], ["before u16", base - 0x100, 2], ["before u8", base - 0x100, 1]]) {
  let found = false;
  for (let off = 0; off < 0x100 - 24 * stride; off += stride) {
    const vals = [];
    for (let k = 0; k < 24; k++) vals.push(ram.readUInt8 ? undefined : undefined);
  }
}
// simpler and explicit: scan the 0x400 before and after the table for a 24-long run of small ints
function scanRegion(from, to, stride, label) {
  const out = [];
  for (let a = from; a + 24 * stride <= to; a += stride) {
    const vals = [];
    for (let k = 0; k < 24; k++) {
      const o = a + k * stride - RAM_BASE;
      let v;
      if (stride === 4) v = ram.readUInt32LE(o);
      else if (stride === 2) v = ram.readUInt16LE(o);
      else v = ram[o];
      vals.push(v);
    }
    if (vals.every((v) => v <= 4)) out.push({ addr: RAM_BASE + a, vals });
  }
  if (out.length) {
    console.log(`  ${label}: ${out.length} run(s)`);
    for (const r of out.slice(0, 6)) console.log(`    0x${r.addr.toString(16).toUpperCase()}  [${r.vals.join(",")}]`);
  } else console.log(`  ${label}: none`);
  return out;
}
scanRegion(best.at - 0x400, best.at, 4, "u32 runs before");
scanRegion(best.at - 0x400, best.at, 1, "u8 runs before");
scanRegion(best.at + 4 * 40, best.at + 4 * 40 + 0x400, 4, "u32 runs after");
scanRegion(best.at + 4 * 40, best.at + 4 * 40 + 0x400, 1, "u8 runs after");

console.log("\n=== table + 0x40 around it ===");
for (let o = base - 0x20; o < base + 4 * 30; o += 4) {
  const v = ram.readUInt32LE(o);
  const tag = (RAM_BASE + o) === best.at ? "  <== TABLE" : "";
  console.log(`  0x${(RAM_BASE + o).toString(16).toUpperCase()}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}${tag}`);
}
process.exit(0);
