#!/usr/bin/env node
/**
 * psp-ar-lineindex.mjs -- find the LINE INDEX counter.
 *
 * Established: 0x8AB7C0C holds the pointer to the ACTIVE story container (it equals the
 * container base). The line index is a different variable, so this hunts it the tightest way
 * available: dump all RAM, advance the narration, dump again, and report ONLY words that both
 * CHANGED and are small (0..32) in at least one dump. A line counter must satisfy both.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar lineindex", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

const A = await dumpAll();
console.log("A dumped; advancing");
press("cross"); await sleep(5000);
const B = await dumpAll();
press("cross"); await sleep(5000);
const C = await dumpAll();
console.log("C dumped");

const n = Math.min(A.length, B.length, C.length);
const u32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);

const smalls = [];
let changedTotal = 0;
for (let i = 0; i + 4 <= n; i += 4) {
  const a = u32(A, i), b = u32(B, i), c = u32(C, i);
  if (a === b && b === c) continue;
  changedTotal++;
  const vals = [a, b, c];
  if (vals.some((v) => v >= 0 && v <= 32)) {
    // a line counter should also be non-decreasing or at least monotone-ish
    smalls.push({ addr: RAM_BASE + i, a, b, c });
  }
}
console.log(`\n${changedTotal} words changed total; ${smalls.length} of them small (0..32)`);
console.log("=== small changed words (a line counter must appear here) ===");
for (const s of smalls.slice(0, 80)) {
  console.log(`  0x${s.addr.toString(16).toUpperCase()}  ${s.a} -> ${s.b} -> ${s.c}`);
}
db.s?.close();
process.exit(0);
