#!/usr/bin/env node
/**
 * psp-ar-loghook.mjs -- use a LOGGING breakpoint, the right instrument for this build.
 *
 * Why: the plain execution breakpoint on FUN_000d9bdc demonstrably FIRES (the game halts and
 * input calls start timing out), but this PPSSPP build returns 0xdeadbeef for registers at the
 * halt and cpu.stepping will not resume. However the breakpoint schema carries:
 *
 *     log: true, logFormat: "..."
 *
 * A logging breakpoint makes PPSSPP record what happened ITSELF, in its own log stream, instead
 * of handing the client a halted CPU. That bypasses both problems at once: no register read at
 * the halt, and no resume needed.
 *
 * This script:
 *   1. prints EVERY pushed message (not just ticket replies) so any hit notification is visible
 *   2. arms cpu.breakpoint.add with log enabled and a format mentioning the argument regs
 *   3. drives the narration and reports whatever comes back
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
const LOOKUP = BASE + 0xD9BDC;
import { execFileSync } from "node:child_process";
import { join } from "node:path";
const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const pushed = [];
class D {
  constructor() { this.q = new Map(); this.t = 1; }
  connect() {
    return new Promise((res, rej) => {
      const s = new WebSocket(URL, "debugger.ppsspp.org");
      this.s = s;
      s.addEventListener("message", (e) => {
        let m; try { m = JSON.parse(String(e.data)); } catch { pushed.push({ raw: String(e.data).slice(0, 300) }); return; }
        if (m.ticket != null && this.q.has(String(m.ticket))) {
          const it = this.q.get(String(m.ticket)); this.q.delete(String(m.ticket));
          clearTimeout(it.timer);
          m.event === "error" ? it.rej(new Error(m.message || "request failed")) : it.res(m);
        } else {
          pushed.push(m);   // an unsolicited push (breakpoint hit, log line, ...)
        }
      });
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar loghook", version: "0.1.0", timeout: 60000 }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}, ms = 10000) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, ms);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  async try_(event, fields, ms) { try { return { ok: true, r: await this.req(event, fields, ms) }; } catch (e) { return { ok: false, e: e.message }; } }
}
const db = new D();
await db.connect();
const press = (b) => { try { execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe", timeout: 4000 }); } catch {} };
async function findStory() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  const ram = Buffer.concat(parts);
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    if (c > 0 && c < 4000 && p14 > RAM_BASE && p14 < RAM_BASE + ram.length) {
      const np = ram.readUInt32LE(p14 - RAM_BASE);
      if (np > RAM_BASE && np < RAM_BASE + ram.length) {
        const e = ram.indexOf(0, np - RAM_BASE);
        const nm = ram.subarray(np - RAM_BASE, e).toString("latin1");
        if (nm.startsWith("MSG_AR_") && !nm.startsWith("MSG_AR_CHPTSEL")) return { base: RAM_BASE + i, count: c };
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
for (let a = 0; a < 18; a++) {
  cont = await findStory();
  if (cont) break;
  if (a % 3 === 2) press("cross");
  await sleep(4000);
}
console.log(`story container: ${cont ? "0x" + cont.base.toString(16).toUpperCase() + " count=" + cont.count : "NOT LOADED"}`);

// try logging-breakpoint variants
await db.try_("cpu.breakpoint.remove", { address: LOOKUP });
const variants = [
  { address: LOOKUP, log: true, logFormat: "id=%08x" },
  { address: LOOKUP, log: true, logFormat: "{a0}" },
  { address: LOOKUP, log: true },
  { address: LOOKUP },
];
let armedOK = false;
for (const v of variants) {
  const r = await db.try_("cpu.breakpoint.add", v);
  console.log(`  cpu.breakpoint.add ${JSON.stringify(v)} -> ${r.ok ? "OK " + JSON.stringify(r.r).slice(0, 200) : "ERR " + r.e}`);
  if (r.ok) { armedOK = true; break; }
}

console.log("\n=== driving; watching for ANY pushed message or log line ===");
for (let round = 1; round <= 10; round++) {
  press("cross");
  await sleep(4000);
  if (pushed.length) {
    console.log(`  [round ${round}] ${pushed.length} pushed message(s):`);
    for (const p of pushed.splice(0)) console.log("     " + JSON.stringify(p).slice(0, 400));
  } else {
    console.log(`  [round ${round}] no pushed messages`);
  }
}

// also list breakpoint state + look for any log-retrieval event
const list = await db.try_("cpu.breakpoint.list", {});
console.log("\nbreakpoint list: " + JSON.stringify(list.r || list.e).slice(0, 500));
for (const ev of ["cpu.breakpoint.log", "log.get", "debugger.log", "cpu.log", "log"]) {
  const r = await db.try_(ev, {});
  if (r.ok) console.log(`${ev} -> ${JSON.stringify(r.r).slice(0, 500)}`);
}
db.s?.close();
process.exit(0);
