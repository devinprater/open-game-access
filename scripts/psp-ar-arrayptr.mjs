#!/usr/bin/env node
/**
 * psp-ar-arrayptr.mjs -- find the current line WITHOUT a breakpoint.
 *
 * Previous searches tested whether any RAM word equals a line's TEXT pointer (the string itself)
 * and found none. But an engine walking a message list more naturally holds a pointer to the
 * ARRAY ENTRY it is on -- i.e.  p18 + index*4  (or p14 + index*4 for the names). That value is
 * distinct per line and would change as the narration advances.
 *
 * This tests exactly that, using only memory reads: for every word in RAM, is its value
 *   p18 + k*4   or   p14 + k*4   for some k in 0..count-1 ?
 * and then confirms by advancing one line and re-reading.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
import { execFileSync } from "node:child_process";
import { join } from "node:path";
const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar arrayptr", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}, ms = 20000) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, ms);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}
const db = new D();
await db.connect();
const press = (b) => { try { execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe", timeout: 4000 }); } catch {} };
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}
function findStory(ram) {
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    const p18 = ram.readUInt32LE(i + 0x18);
    if (c > 0 && c < 4000 && p14 > RAM_BASE && p18 > RAM_BASE && p18 < RAM_BASE + ram.length) {
      const np0 = ram.readUInt32LE(p14 - RAM_BASE);
      if (np0 > RAM_BASE && np0 < RAM_BASE + ram.length) {
        const e0 = ram.indexOf(0, np0 - RAM_BASE);
        const nm = ram.subarray(np0 - RAM_BASE, e0).toString("latin1");
        if (nm.startsWith("MSG_AR_") && !nm.startsWith("MSG_AR_CHPTSEL")) return { base: RAM_BASE + i, count: c, p14, p18 };
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return null;
}

console.log("=== navigate into Another Road ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(2400); }
press("cross"); await sleep(3000);
press("cross"); await sleep(4000);
for (let i = 0; i < 12; i++) { press("up"); await sleep(200); }
press("cross"); await sleep(6000);

let cont = null;
for (let a = 0; a < 20; a++) {
  const ram = await dumpAll();
  cont = findStory(ram);
  if (cont) break;
  if (a % 3 === 2) press("cross");
  console.log(`  waiting for the story container... (${a + 1}/20)`);
  await sleep(4000);
}
if (!cont) { console.log("story container never loaded"); db.s?.close(); process.exit(0); }
console.log(`story container 0x${cont.base.toString(16).toUpperCase()} count=${cont.count}`);
console.log(`  name array @0x${cont.p14.toString(16).toUpperCase()}   text array @0x${cont.p18.toString(16).toUpperCase()}`);

/** scan RAM for words equal to an array-entry address */
function scan(ram) {
  const want = new Map();
  for (let k = 0; k < cont.count; k++) {
    want.set(cont.p18 + k * 4, `text-array[${k}]`);
    want.set(cont.p14 + k * 4, `name-array[${k}]`);
  }
  const hits = [];
  for (let i = 0; i + 4 <= ram.length; i += 4) {
    const v = ram.readUInt32LE(i);
    const what = want.get(v);
    if (what) hits.push({ addr: RAM_BASE + i, what, v });
  }
  return hits;
}

async function pass(label) {
  const ram = await dumpAll();
  const hits = scan(ram);
  console.log(`\n[${label}] ${hits.length} word(s) point at an array entry`);
  for (const h of hits) console.log(`   0x${h.addr.toString(16).toUpperCase()} = ${h.what}  (0x${h.v.toString(16)})`);
  return hits;
}

const A = await pass("before");
console.log("\nadvancing one line...");
press("cross"); await sleep(5000);
const B = await pass("after 1 advance");
press("cross"); await sleep(5000);
const C = await pass("after 2 advances");

// did any holder CHANGE to a later entry?
console.log("\n=== holders that advanced ===");
const map = (hs) => new Map(hs.map((h) => [h.addr, h.what]));
const mB = map(B), mC = map(C);
let advanced = 0;
for (const h of A) {
  const b = mB.get(h.addr), c = mC.get(h.addr);
  if (b !== h.what || c !== h.what) { console.log(`   0x${h.addr.toString(16).toUpperCase()}  ${h.what} -> ${b} -> ${c}`); advanced++; }
}
if (!advanced) console.log("   (none — no array-entry pointer advances)");
db.s?.close();
process.exit(0);
