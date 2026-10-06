#!/usr/bin/env node
/**
 * psp-ar-screenid2.mjs -- screen discriminator, filtered by STABILITY.
 *
 * 22,932 bytes differ between the main menu and Options, but an animation counter also
 * "differs". A real screen id is STABLE within a screen across repeated no-input reads.
 * So: dump each screen TWICE with no input, keep bytes identical within a screen, and
 * only then take the ones that differ between screens.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const CURSOR = 0x08BA1D18;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar screenid2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 25000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  async u32(a) { return (await this.read(a, 4)).readUInt32LE(0); }
}
const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

// --- currently in Options (from the previous run). Confirm, then two no-input dumps.
console.log(`cursor = ${await db.u32(CURSOR)} (Options expects a small index)`);
console.log("Options dump #1"); const O1 = await dumpAll();
await sleep(1300);
console.log("Options dump #2 (no input)"); const O2 = await dumpAll();

console.log("press circle -> back to main menu");
press("circle"); await sleep(2000);
console.log(`cursor = ${await db.u32(CURSOR)} (main menu)`);
console.log("Main dump #1"); const M1 = await dumpAll();
await sleep(1300);
console.log("Main dump #2 (no input)"); const M2 = await dumpAll();
db.s?.close();

const stable = (x, y, i) => x[i] === y[i];
const out = [];
for (let i = 0; i < M1.length; i++) {
  if (!stable(O1, O2, i)) continue;          // churn on the Options screen
  if (!stable(M1, M2, i)) continue;          // churn on the main screen
  if (O1[i] === M1[i]) continue;             // must differ BETWEEN screens
  if (O1[i] > 0x60 && M1[i] > 0x60) continue;
  out.push([RAM_BASE + i, M1[i], O1[i]]);
}
console.log(`\nSTABLE-within-screen and DIFFERENT-between-screen candidates: ${out.length}`);
for (const [addr, m, o] of out.slice(0, 40)) console.log(`  0x${addr.toString(16).toUpperCase()}  main=${m} (0x${m.toString(16)})  options=${o} (0x${o.toString(16)})`);
