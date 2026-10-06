#!/usr/bin/env node
/**
 * psp-ar-vmhook4.mjs -- THE HOOK: track story lines by breaking on the message lookup.
 *
 * Hook point (decompiled, verified at the right address):
 *   FUN_000d9bdc(id, container) -> text pointer for that id
 *   RAM 0x88DDBDC, MIPS O32: a0 = message id, a1 = container (0 = default), v0 = text ptr
 *
 * API map measured from the running debugger:
 *   cpu.breakpoint.add {address}   -> execution breakpoint
 *   cpu.getAllRegs                 -> every GPR (a0 is register 4)  [used to read args]
 *   cpu.getReg {name:"a0"}         -> single register
 *   cpu.status                     -> {stepping, paused, pc, ticks}
 *   cpu.stepping / cpu.resume      -> resume; they reply only when the CPU next halts
 *   memory.read {address,size,replacements:false} -> base64 bytes
 *
 * Loop: arm -> resume -> (halt) -> read a0 -> resolve the id to text from the live container ->
 * resume. That reports the line being drawn, with no line index to find.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar vmhook4", version: "0.1.0", timeout: 60000 }); res(); } catch (e) { rej(e); } });
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
const press = (b) => { try { execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe", timeout: 3000 }); } catch {} };
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

// ---- register helpers ----
async function regs() {
  const r = await db.try_("cpu.getAllRegs", {}, 5000);
  if (!r.ok) return null;
  const cat = (r.r.categories || []).find((c) => c.id === 0) || (r.r.categories || [])[0];
  if (!cat) return null;
  const names = cat.registerNames || [];
  const vals = cat.registerValues || cat.uintValues || cat.values || [];
  const out = {};
  names.forEach((n, i) => { out[n] = typeof vals[i] === "object" ? (vals[i].uintValue ?? vals[i].value) : vals[i]; });
  return { out, raw: r.r };
}
async function reg(name) {
  const r = await db.try_("cpu.getReg", { name }, 5000);
  return r.ok ? (r.r.uintValue ?? r.r.register) : null;
}

// ---- container resolution (id -> name/text) ----
async function containerMap() {
  const ram = await dumpAll();
  const out = { base: null, entries: new Map(), all: [] };
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    const p18 = ram.readUInt32LE(i + 0x18);
    if (c > 0 && c < 4000 && p14 > RAM_BASE && p18 > RAM_BASE && p18 < RAM_BASE + ram.length) {
      const np0 = ram.readUInt32LE(p14 - RAM_BASE);
      if (np0 > RAM_BASE && np0 < RAM_BASE + ram.length) {
        const e = ram.indexOf(0, np0 - RAM_BASE);
        const nm0 = ram.subarray(np0 - RAM_BASE, e).toString("latin1");
        const entries = new Map();
        const all = [];
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
          all.push({ k, name });
        }
        if (nm0.startsWith("MSG_AR_")) return { base: RAM_BASE + i, count: c, entries, all };
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return out;
}

// ---- diagnostics: how do registers actually come back? ----
console.log("=== register API shape ===");
const rr = await regs();
if (rr) {
  console.log("  keys of getAllRegs response: " + Object.keys(rr.raw).join(", "));
  const cat = rr.raw.categories[0];
  console.log("  GPR category keys: " + Object.keys(cat).join(", "));
  console.log("  names/values present: names=" + (cat.registerNames || []).length + " values=" + ((cat.registerValues || cat.uintValues || cat.values || []).length));
  console.log("  a0 via getReg = " + await reg("a0"));
  console.log("  pc  via getReg = " + await reg("pc"));
}

// ---- arm ----
await db.try_("cpu.breakpoint.remove", { address: LOOKUP });
const armed = await db.try_("cpu.breakpoint.add", { address: LOOKUP });
console.log(`\narmed execution breakpoint @0x${LOOKUP.toString(16).toUpperCase()} -> ${armed.ok ? "OK" : armed.e}`);

// ---- the loop: resume, wait for the halt, read a0 ----
console.log("\n=== hook loop (resume -> halt -> read a0 -> resolve) ===");
let lastA0 = null;
for (let round = 1; round <= 14; round++) {
  // resume; this reply only arrives when the CPU halts again
  const res = await db.try_("cpu.stepping", {}, 45000);
  const st = await db.try_("cpu.status", {}, 5000);
  const a0 = await reg("a0");
  const a1 = await reg("a1");
  const pc = await reg("pc");
  const status = st.ok ? `paused=${st.r.paused} pc=0x${(st.r.pc >>> 0).toString(16)}` : "n/a";
  console.log(`\nround ${round}: resume=${res.ok ? "returned" : res.e}  ${status}`);
  console.log(`   a0=${a0} (0x${(a0 >>> 0).toString(16)})  a1=${a1 !== null ? "0x" + (a1 >>> 0).toString(16) : "null"}  pc=0x${(pc >>> 0).toString(16)}`);

  if (a0 !== null && a0 !== lastA0 && a0 !== 0xdeadbeef) {
    lastA0 = a0;
    const cm = await containerMap();
    if (cm.base) {
      const e = cm.entries.get(a0 & 0xffff) || cm.entries.get(a0);
      console.log(`   container 0x${cm.base.toString(16).toUpperCase()} count=${cm.count}`);
      console.log(`   => id ${a0 & 0xffff} = ${e ? (e.name + "  " + JSON.stringify((e.text || "").slice(0, 80))) : "(not in this container)"}`);
    } else {
      console.log("   (no MSG_AR container loaded to resolve against)");
    }
  }
  if (round === 1) console.log("   (if resume keeps timing out the CPU may already be halted; the a0 read still counts)");
}
db.s?.close();
process.exit(0);
