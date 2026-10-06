#!/usr/bin/env node
/**
 * psp-ar-chdiff.mjs -- JOB A: find the Chapter Select BROWSE cursor.
 *
 * The chapter index (0x89B51D5) does NOT move when browsing, so the on-screen cursor is a
 * different variable. This does the most general search possible: full 24 MiB RAM dumped
 * before/after each press, reporting EVERY 4-byte word that changed (and that looks like a
 * small index), rather than assuming a +1/+1/-1 shape.
 *
 * Navigates itself: title -> start x3 -> Load -> cross x2 -> main menu -> cross (Another Road).
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar chdiff", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// --- navigate to main menu, then Another Road ---
console.log("=== navigate to Another Road ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(2000); }
press("cross"); await sleep(2500);
press("cross"); await sleep(3500);
// main menu: up x10 -> top, then cross on "Another Road"
for (let i = 0; i < 10; i++) { press("up"); await sleep(250); }
press("cross"); await sleep(5000);
// chapter select appears after a title card
press("cross"); await sleep(4000);
press("cross"); await sleep(4000);

const A = await dumpAll();
console.log("baseline dumped; pressing down x2");
press("down"); await sleep(1800);
const B = await dumpAll();
press("down"); await sleep(1800);
const C = await dumpAll();
db.s?.close();

const n = Math.min(A.length, B.length, C.length);
const u32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);

const changed = [];
for (let i = 0; i + 4 <= n; i += 4) {
  const a = u32(A, i), b = u32(B, i), c = u32(C, i);
  if (a === b && b === c) continue;
  const small = [a, b, c].every((v) => (v >= 0 && v <= 0x1000) || v === 0xffffffff);
  changed.push({ addr: RAM_BASE + i, a, b, c, small });
}
console.log(`\n=== ${changed.length} words changed across two Down presses ===`);
console.log("--- small/index-like changes (most likely a cursor) ---");
for (const ch of changed.filter((x) => x.small).slice(0, 60)) {
  console.log(`  0x${ch.addr.toString(16).toUpperCase()}  ${ch.a} -> ${ch.b} -> ${ch.c}`);
}
console.log(`\n--- total small: ${changed.filter((x) => x.small).length}, other: ${changed.filter((x) => !x.small).length} ---`);
process.exit(0);
