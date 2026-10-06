#!/usr/bin/env node
/**
 * psp-ar-vmhook5.mjs -- THE HOOK, corrected.
 *
 * Bugs fixed from the previous attempt:
 *   - the script armed before the game had booted ("CPU not started", pc=0). It must FIRST
 *     navigate into Another Road and wait for the story container, so the CPU is running and a
 *     text line is actually being drawn.
 *   - register reads must be wrapped: the CPU can halt or the game can be mid-transition.
 *
 * Hook point: FUN_000d9bdc(id, container) -> text pointer for that id.
 *   RAM 0x88DDBDC; MIPS O32: a0 = message id, a1 = container (0 = default), v0 = text pointer.
 *
 * Register API: cpu.getAllRegs -> {categories:[{id,name,registerNames[35],uintValues[35]}]}
 *   so a0 = uintValues[ registerNames.indexOf("a0") ].
 * Resume: cpu.stepping. Status: cpu.status {stepping,paused,pc,ticks}.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar vmhook5", version: "0.1.0", timeout: 60000 }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}, ms = 8000) {
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
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}
async function status() {
  const s = await db.try_("cpu.status", {}, 5000);
  return s.ok ? s.r : null;
}
async function readRegs() {
  const r = await db.try_("cpu.getAllRegs", {}, 5000);
  if (!r.ok) return null;
  const cat = (r.r.categories || []).find((c) => c.name === "GPR") || (r.r.categories || [])[0];
  if (!cat || !cat.registerNames || !cat.uintValues) return null;
  const o = {};
  cat.registerNames.forEach((n, i) => { o[n] = cat.uintValues[i]; });
  return o;
}
async function containerMap() {
  const ram = await dumpAll();
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    const p18 = ram.readUInt32LE(i + 0x18);
    if (c > 0 && c < 4000 && p14 > RAM_BASE && p18 > RAM_BASE && p18 < RAM_BASE + ram.length) {
      const np0 = ram.readUInt32LE(p14 - RAM_BASE);
      if (np0 > RAM_BASE && np0 < RAM_BASE + ram.length) {
        const e0 = ram.indexOf(0, np0 - RAM_BASE);
        const nm0 = ram.subarray(np0 - RAM_BASE, e0).toString("latin1");
        if (nm0.startsWith("MSG_AR_") && !nm0.startsWith("MSG_AR_CHPTSEL")) {
          const entries = new Map();
          for (let k = 0; k < c; k++) {
            const np = ram.readUInt32LE(p14 - RAM_BASE + k * 4);
            const tp = ram.readUInt32LE(p18 - RAM_BASE + k * 4);
            let name = null, text = null;
            if (np >= RAM_BASE && np < RAM_BASE + ram.length) {
              const ee = ram.indexOf(0, np - RAM_BASE);
              name = ram.subarray(np - RAM_BASE, ee).toString("latin1");
            }
            if (tp >= RAM_BASE && tp < RAM_BASE + ram.length) {
              const o2 = tp - RAM_BASE;
              let e2 = o2;
              while (e2 + 1 < ram.length && e2 - o2 < 600 && !(ram[e2] === 0 && ram[e2 + 1] === 0)) e2 += 2;
              text = ram.subarray(o2, e2).toString("utf16le");
            }
            entries.set(k, { name, text });
          }
          return { base: RAM_BASE + i, count: c, entries };
        }
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return null;
}

// ---- 1. wait until the CPU is actually running ----
console.log("=== waiting for the CPU to start ===");
for (let i = 0; i < 20; i++) {
  const st = await status();
  if (st && st.pc !== 0) { console.log(`  CPU running: pc=0x${(st.pc >>> 0).toString(16)}`); break; }
  press("start");
  await sleep(3000);
}

// ---- 2. navigate into Another Road and wait for a story container ----
console.log("=== navigating into Another Road ===");
for (let i = 0; i < 3; i++) { press("start"); await sleep(2500); }
press("cross"); await sleep(3000);
press("cross"); await sleep(4000);
for (let i = 0; i < 12; i++) { press("up"); await sleep(200); }
press("cross"); await sleep(6000);

let cont = null;
for (let attempt = 0; attempt < 20; attempt++) {
  cont = await containerMap();
  if (cont) break;
  if (attempt % 3 === 2) press("cross");
  console.log(`  waiting for the story container... (${attempt + 1}/20)`);
  await sleep(4000);
}
if (!cont) { console.log("story container never loaded"); db.s?.close(); process.exit(0); }
console.log(`story container 0x${cont.base.toString(16).toUpperCase()} count=${cont.count}`);

// ---- 3. arm the execution breakpoint ----
await db.try_("cpu.breakpoint.remove", { address: LOOKUP });
const armed = await db.try_("cpu.breakpoint.add", { address: LOOKUP });
console.log(`\narmed 0x${LOOKUP.toString(16).toUpperCase()} -> ${armed.ok ? "OK" : armed.e}`);

// ---- 4. resume -> halt -> read a0 -> resolve ----
console.log("\n=== hook loop ===");
const seen = [];
for (let round = 1; round <= 16; round++) {
  const res = await db.try_("cpu.stepping", {}, 30000);
  const st = await status();
  const r = await readRegs();
  const a0 = r ? r.a0 : null;
  const a1 = r ? r.a1 : null;
  const pc = r ? r.pc : null;
  console.log(`\nround ${round}: resume=${res.ok ? "halted" : res.e}`);
  console.log(`   pc=0x${pc !== null ? (pc >>> 0).toString(16) : "?"}  a0=${a0} (0x${((a0 ?? 0) >>> 0).toString(16)})  a1=0x${((a1 ?? 0) >>> 0).toString(16)}`);
  if (a0 !== null && a0 !== 0 && pc !== null && (pc >>> 0) === LOOKUP) {
    const id = a0 & 0xffff;
    const e = cont.entries.get(id);
    const line = `${id} ${e ? e.name : "?"} ${e ? JSON.stringify((e.text || "").slice(0, 70)) : ""}`;
    if (!seen.includes(line)) { seen.push(line); }
    console.log(`   => BREAK ON LOOKUP: id=${id}  ${e ? e.name + "  " + JSON.stringify((e.text || "").slice(0, 70)) : "(id not in this container)"}`);
  }
  if (round % 4 === 0) { press("cross"); }
}
console.log("\n=== distinct lines observed ===");
for (const l of seen) console.log("  " + l);
if (!seen.length) console.log("  (none — see the per-round pc/a0 above to see where it halted)");
db.s?.close();
process.exit(0);
