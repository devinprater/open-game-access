#!/usr/bin/env node
/**
 * psp-ar-chsel2.mjs -- JOB A: find the Chapter Select BROWSE cursor, this time with a
 * screenshot at every step so a screen transition can never be mistaken for a cursor move.
 *
 * Previous failure: the full-RAM diff was swamped (50,301 words changed) because the game had
 * moved to a cutscene between dumps. This run screenshots before/between/after the presses and
 * reports the story state each time, so the diff is only trusted when the screen is stable.
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-chsel2";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar chsel2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
function shot(label) {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const png = join(OUT, `${label}.png`);
  execFileSync("python", ["C:/Users/Public/ppm2png_pad.py", join(SHOTDIR, fresh[0]), png], { stdio: "pipe" });
  return png;
}
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}
async function storyBytes() {
  const b = await db.read(BASE + 0x1B11D0, 0x10);
  return { ar: b[4], idx: b[5], f6: b[6], f7: b[7] };
}

// ---- navigate ----
console.log("=== navigate to main menu ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(2200); }
press("cross"); await sleep(2800);
press("cross"); await sleep(3500);
for (let i = 0; i < 12; i++) { press("up"); await sleep(220); }
console.log("=== enter Another Road ===");
press("cross"); await sleep(6000);
const s1 = await storyBytes();
console.log(`  after entering: ${JSON.stringify(s1)}`);
shot("00-entered");

// advance the intro narration to reach Chapter Select
for (let i = 1; i <= 5; i++) {
  press("cross"); await sleep(5000);
  const s = await storyBytes();
  const p = shot(`0${i}-advance`);
  console.log(`  advance ${i}: ${JSON.stringify(s)}  shot=${p ? "yes" : "no"}`);
}

// ---- now the guarded diff ----
console.log("\n=== guarded diff: dump / down / dump / down / dump ===");
const s0 = await storyBytes();
shot("A-before");
const A = await dumpAll();
console.log(`  A taken, story=${JSON.stringify(s0)}`);

press("down"); await sleep(2200);
shot("B-down1");
const B = await dumpAll();
const sB = await storyBytes();
console.log(`  B taken, story=${JSON.stringify(sB)}`);

press("down"); await sleep(2200);
shot("C-down2");
const C = await dumpAll();
const sC = await storyBytes();
console.log(`  C taken, story=${JSON.stringify(sC)}`);

press("up"); await sleep(2200);
const DD = await dumpAll();
const sD = await storyBytes();
console.log(`  D taken (after up), story=${JSON.stringify(sD)}`);

db.s?.close();

const n = Math.min(A.length, B.length, C.length, DD.length);
const u32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);
let changed = 0;
const smalls = [];
for (let i = 0; i + 4 <= n; i += 4) {
  const a = u32(A, i), b = u32(B, i), c = u32(C, i), d = u32(DD, i);
  if (a === b && b === c && c === d) continue;
  changed++;
  const allSmall = [a, b, c, d].every((v) => (v >= 0 && v <= 0x1000) || v === 0xffffffff);
  if (allSmall) smalls.push({ addr: RAM_BASE + i, a, b, c, d });
}
console.log(`\n=== ${changed} words changed in total ===`);
console.log(`=== small/index-like (possible cursor): ${smalls.length} ===`);
for (const s of smalls.slice(0, 60)) {
  console.log(`  0x${s.addr.toString(16).toUpperCase()}  ${s.a} -> ${s.b} -> ${s.c}  (after up: ${s.d})`);
}
console.log("\nstory flags: " + JSON.stringify({ s0, sB, sC, sD }));
