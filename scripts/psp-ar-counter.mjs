#!/usr/bin/env node
/**
 * psp-ar-counter.mjs -- pin the LINE COUNTER, respecting the settled-screen rule.
 *
 * The rule (recorded after three saturated diffs on this game): a RAM diff is worthless unless
 * the screen is CONFIRMED SETTLED. So every step here:
 *   1. takes two screenshots ~1.3 s apart and hashes them -> "settled" only if identical
 *   2. reads a SMALL window around the active-container pointer 0x8AB7C0C (cheap, so it can be
 *      sampled often)
 *   3. presses cross ONCE, waits, and re-reads
 * A step only counts as a clean single-line advance if the screen was settled before the press
 * and changed after it.
 *
 * Best lead: 0x8AB7C10 -- the word right after the container pointer -- moved +1 on one advance.
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-counter";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const WIN_LO = 0x08AB7BFC, WIN_HI = 0x08AB7C34;   // around the container pointer
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar counter", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

function shotHash() {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const p = join(SHOTDIR, fresh[0]);
  const buf = readFileSync(p);
  return createHash("md5").update(buf).digest("hex").slice(0, 10) + ":" + buf.length;
}
async function win() {
  const b = await db.read(WIN_LO, WIN_HI - WIN_LO);
  return Array.from({ length: (WIN_HI - WIN_LO) / 4 }, (_, i) => b.readInt32LE(i * 4));
}
async function findContainer() {
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
        if (ram.subarray(np - RAM_BASE, e).toString("latin1").startsWith("MSG_AR_") &&
            !ram.subarray(np - RAM_BASE, e).toString("latin1").startsWith("MSG_AR_CHPTSEL")) {
          return { base: RAM_BASE + i, count: c };
        }
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return null;
}

// ---- navigate ----
console.log("=== navigate into Another Road ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(2200); }
press("cross"); await sleep(2800);
press("cross"); await sleep(3500);
for (let i = 0; i < 12; i++) { press("up"); await sleep(220); }
press("cross"); await sleep(6500);

const cont = await findContainer();
console.log(`story container: ${cont ? "0x" + cont.base.toString(16).toUpperCase() + " count=" + cont.count : "NOT LOADED"}`);

console.log("\n=== steps (settled = two identical screenshots before the press) ===");
console.log("addr offsets are from 0x8AB7BFC; the container pointer is the word at +0x10");

let prev = null;
for (let step = 0; step <= 8; step++) {
  const h1 = shotHash(); await sleep(1300); const h2 = shotHash();
  const settled = h1 === h2;
  const w = await win();
  const ptrIdx = (0x08AB7C0C - WIN_LO) / 4;
  const rowVals = w.map((v, k) => `${(WIN_LO + k * 4 - 0x08AB7BFC).toString(16)}:${v}`).join(" ");
  console.log(`\nstep ${step}  settled=${settled}  shot=${h1}`);
  console.log(`   window  ${rowVals}`);
  console.log(`   container ptr = ${w[ptrIdx]} (0x${(w[ptrIdx] >>> 0).toString(16)})`);
  if (prev) {
    const changed = [];
    for (let k = 0; k < w.length; k++) if (w[k] !== prev[k]) changed.push(`+0x${((WIN_LO + k * 4) - 0x08AB7BFC).toString(16)}: ${prev[k]} -> ${w[k]}`);
    console.log(`   CHANGED  ${changed.length ? changed.join(" | ") : "(nothing)"}`);
  }
  prev = w;
  if (step < 8) { press("cross"); await sleep(4200); }
}
db.s?.close();
process.exit(0);
