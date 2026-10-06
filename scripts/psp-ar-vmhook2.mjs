#!/usr/bin/env node
/**
 * psp-ar-vmhook2.mjs -- hook the message lookup properly.
 *
 * Corrections learned from the previous run:
 *   - memory.breakpoint.add with type "execute" is a DATA breakpoint (the list shows
 *     read/write/change flags), NOT an execution breakpoint. Arming it on a code address gives
 *     0 hits, which is expected -- do not read that as "the function is not called".
 *   - cpu.breakpoint.add IS the execution-breakpoint API (the list echoes the disassembled
 *     instruction, e.g. "addiu sp,sp,-0x20").
 *
 * Strategy, best instrument first:
 *   1. Find the live story container and its TEXT-POINTER ARRAY (container + 0x18 -> array of
 *      pointers to each line's UTF-16 text).
 *   2. Arm a MEMORY READ breakpoint on that array. It must fire exactly when the engine fetches
 *      a line's text, and the reader of that array is the draw/lookup code.
 *   3. Also arm an EXECUTION breakpoint on FUN_000d9bdc (the id->text lookup) to see whether it
 *      is the per-line path.
 *   4. Drive the narration and report hits for both.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
const LOOKUP = BASE + 0xD9BDC;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar vmhook2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 20000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  async try_(event, fields) { try { return { ok: true, r: await this.req(event, fields) }; } catch (e) { return { ok: false, e: e.message }; } }
}

const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}
async function findContainer() {
  const ram = await dumpAll();
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    const p18 = ram.readUInt32LE(i + 0x18);
    if (c > 0 && c < 4000 && p14 > RAM_BASE && p18 > RAM_BASE && p18 < RAM_BASE + ram.length) {
      const np = ram.readUInt32LE(p14 - RAM_BASE);
      if (np > RAM_BASE && np < RAM_BASE + ram.length) {
        const e = ram.indexOf(0, np - RAM_BASE);
        const nm = ram.subarray(np - RAM_BASE, e).toString("latin1");
        if (nm.startsWith("MSG_AR_") && !nm.startsWith("MSG_AR_CHPTSEL")) return { base: RAM_BASE + i, count: c, p14, p18 };
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return null;
}

// clear anything already armed from the previous run
for (const ev of ["memory.breakpoint.remove", "cpu.breakpoint.remove"]) {
  const r = await db.try_(ev, { address: LOOKUP });
  if (r.ok) console.log(`cleared ${ev} @0x${LOOKUP.toString(16)}`);
}

console.log("=== navigate into Another Road ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(2200); }
press("cross"); await sleep(2800);
press("cross"); await sleep(3500);
for (let i = 0; i < 12; i++) { press("up"); await sleep(220); }
press("cross"); await sleep(6500);

const cont = await (async () => {
  for (let attempt = 0; attempt < 24; attempt++) {
    const c = await findContainer();
    if (c) return c;
    // not loaded yet: the narration may need nudging along
    if (attempt % 3 === 2) press("cross");
    console.log(`  waiting for the story container... (${attempt + 1}/24)`);
    await sleep(4000);
  }
  return null;
})();
if (!cont) { console.log("story container not loaded — cannot arm the text-array breakpoint"); db.s?.close(); process.exit(0); }
console.log(`story container 0x${cont.base.toString(16).toUpperCase()}  count=${cont.count}`);
console.log(`  text-pointer array @0x${cont.p18.toString(16).toUpperCase()}  (${cont.count * 4} bytes)`);

// arm BOTH instruments
const r1 = await db.try_("memory.breakpoint.add", { address: cont.p18, size: cont.count * 4, type: "read" });
console.log(`\nmemory READ breakpoint on the text array -> ${r1.ok ? "OK" : "ERROR " + r1.e}`);
const r2 = await db.try_("cpu.breakpoint.add", { address: LOOKUP });
console.log(`cpu EXECUTION breakpoint on FUN_000d9bdc -> ${r2.ok ? "OK" : "ERROR " + r2.e}`);

async function report(label) {
  const m = await db.try_("memory.breakpoint.list", {});
  const c = await db.try_("cpu.breakpoint.list", {});
  console.log(`\n[${label}]`);
  if (m.ok) for (const b of m.r.breakpoints || []) console.log(`   mem 0x${b.address.toString(16)} size=${b.size} r=${b.read} w=${b.write} hits=${b.hits}`);
  if (c.ok) for (const b of c.r.breakpoints || []) console.log(`   cpu 0x${b.address.toString(16)} hits=${b.hits} code=${JSON.stringify(b.code)}`);
}
await report("armed");

console.log("\n=== advancing the narration ===");
for (let step = 1; step <= 6; step++) {
  press("cross"); await sleep(5000);
  await report(`after advance ${step}`);
}
db.s?.close();
process.exit(0);
