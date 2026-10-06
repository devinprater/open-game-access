#!/usr/bin/env node
/**
 * psp-ar-vmhook.mjs -- hook the message-lookup function to track story lines.
 *
 * THE HOOK POINT (from the decompile):
 *
 *   undefined4 FUN_000d9bdc(uint param_1, int param_2)
 *   {
 *     if (((param_2 != 0) || (param_2 = FUN_000d9db8(), param_2 != 0)) &&
 *        ((param_1 & 0xffff) < (uint)*(ushort *)(param_2 + 0x12))) {
 *       return *(undefined4 *)(*(int *)(param_2 + 0x18) + (param_1 & 0xffff) * 4);
 *     }
 *     return 0;
 *   }
 *
 * That is "give me the text pointer for message id param_1". So breaking on it reports the id
 * the engine is asking for -- i.e. the line being drawn -- with no need to find any line index.
 * a0 = id, a1 = container (0 means "use the default"), v0 = the returned text pointer.
 *
 * ELF vaddr 0xD9BDC -> RAM 0x08804000 + 0xD9BDC = 0x088DDBDC.
 *
 * This script first VERIFIES the address (reads the prologue), then probes which breakpoint API
 * the debugger accepts, then arms it and reports hits with registers.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
const LOOKUP = BASE + 0xD9BDC;      // FUN_000d9bdc
const DEFAULT_CONTAINER = BASE + 0xD9DB8;   // FUN_000d9db8
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar vmhook", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// ---- 1. verify the address really is FUN_000d9bdc ----
const pro = await db.read(LOOKUP, 0x20);
console.log(`=== verifying the hook address 0x${LOOKUP.toString(16).toUpperCase()} ===`);
const words = Array.from({ length: 8 }, (_, i) => pro.readUInt32LE(i * 4));
console.log("  words: " + words.map((w) => "0x" + w.toString(16).padStart(8, "0")).join(" "));
// expected prologue: addiu sp,sp,-N  sw ra,N(sp)  ...
const op0 = words[0] >>> 26, op1 = words[1] >>> 26;
console.log(`  opcodes: ${op0}, ${op1}  ${op0 === 0x09 ? "(addiu — plausible function prologue)" : ""}`);
if (!(op0 === 0x09 || op0 === 0x27 || op0 === 0x2B)) {
  console.log("  ⚠ does not look like a function prologue — the address may be wrong; continuing anyway");
}

// ---- 2. which breakpoint API does this PPSSPP accept? ----
console.log("\n=== probing breakpoint APIs ===");
const attempts = [
  ["memory.breakpoint.add", { address: LOOKUP, size: 4, type: "execute" }],
  ["memory.breakpoint.add", { address: LOOKUP, size: 4, type: "exec" }],
  ["cpu.breakpoint.add", { address: LOOKUP }],
  ["core.breakpoint.add", { address: LOOKUP }],
  ["debugger.breakpoint.add", { address: LOOKUP }],
];
const accepted = [];
for (const [ev, fields] of attempts) {
  const r = await db.try_(ev, fields);
  console.log(`  ${ev} ${JSON.stringify(fields)} -> ${r.ok ? "OK" : "ERROR: " + r.e}`);
  if (r.ok) accepted.push([ev, fields]);
}
if (!accepted.length) {
  console.log("\nNo execution-breakpoint API accepted. Falling back to reporting what the");
  console.log("debugger DOES expose so the next attempt has an accurate API map.");
}

// ---- 3. try to list breakpoints (to see the schema the running build uses) ----
for (const ev of ["memory.breakpoint.list", "cpu.breakpoint.list", "core.breakpoint.list"]) {
  const r = await db.try_(ev, {});
  if (r.ok) console.log(`\n${ev} -> ${JSON.stringify(r.r).slice(0, 600)}`);
}

// ---- 4. if an execution breakpoint exists, arm it and drive ----
if (accepted.length) {
  const [ev, fields] = accepted[0];
  console.log(`\n=== armed ${ev} at 0x${LOOKUP.toString(16).toUpperCase()}; driving the story ===`);
  console.log("(navigate to Another Road first)");
  for (let i = 0; i < 4; i++) { press("start"); await sleep(2200); }
  press("cross"); await sleep(2800);
  press("cross"); await sleep(3500);
  for (let i = 0; i < 12; i++) { press("up"); await sleep(220); }
  press("cross"); await sleep(6000);

  for (let step = 1; step <= 6; step++) {
    const list = await db.try_("memory.breakpoint.list", {});
    console.log(`\nstep ${step}  breakpoints: ${list.ok ? JSON.stringify(list.r).slice(0, 400) : "n/a"}`);
    press("cross"); await sleep(5000);
  }
}
db.s?.close();
process.exit(0);
