#!/usr/bin/env node
/**
 * psp-ar-cursorhunt.mjs -- find the main-menu cursor on THIS boot.
 *
 * The cursor is a HEAP value, so it moved since September (the old 0x08C36F98 now reads 0).
 * Method (the project's own rules):
 *   - a NO-PRESS control snapshot pair, so activity is distinguishable from state
 *   - a small-ordinal filter (a cursor is 0..6, not a pointer)
 *   - a monotonic up-up-then-down sequence, which a counter/allocator does not satisfy
 *   - reject anything over 4096 so pointers and floats drop out
 *
 * Read-only except injected presses.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
const RAM_SIZE = 0x01800000;
const CHUNK = 0x100000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar hunt", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
  async read(address, size) {
    const r = await this.req("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
}

const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

async function dumpAll() {
  const parts = [];
  for (let off = 0; off < RAM_SIZE; off += CHUNK) parts.push(await db.read(RAM_BASE + off, Math.min(CHUNK, RAM_SIZE - off)));
  return Buffer.concat(parts);
}

console.log("snapshot 1: start");
const S1 = await dumpAll();
await sleep(1500);
console.log("snapshot 2: no-press control");
const S2 = await dumpAll();

console.log("press down");
press("down"); await sleep(700);
console.log("snapshot 3: after down #1");
const S3 = await dumpAll();

console.log("press down");
press("down"); await sleep(700);
console.log("snapshot 4: after down #2");
const S4 = await dumpAll();

console.log("press up");
press("up"); await sleep(700);
console.log("snapshot 5: after up #1");
const S5 = await dumpAll();
db.s?.close();

// byte-level series
const n = S1.length;
const cands = [];
let ctrlNoise = 0;
for (let i = 0; i < n; i++) {
  const a = S1[i], b = S2[i], c = S3[i], d = S4[i], e = S5[i];
  if (a !== b) ctrlNoise++;                    // moves without input -> not state
  // ⛔ the control test is a === b: STABLE with no input. (An earlier version of this
  // filter required a !== b, which selects exactly the animation churn it is meant to
  // exclude -- a value that moves on its own is not a cursor.)
  if (a === b && a !== c && c <= 0x40) {
    if (c === a + 1 && d === c + 1 && e === d - 1) cands.push([RAM_BASE + i, [a, b, c, d, e]]);
  }
}
console.log(`\nbytes that moved with NO press (control noise): ${ctrlNoise}`);
console.log(`candidates (small ordinal, +1,+1,-1 across down/down/up, stable on no-press): ${cands.length}`);
for (const [addr, s] of cands.slice(0, 40)) {
  console.log(`  0x${addr.toString(16).toUpperCase()}  series=[${s.join(",")}]`);
}

// also: any address whose value is a plausible cursor (0..7) in ALL five snapshots AND
// moved on C/D but not A/B, without demanding the exact +1 pattern
const loose = [];
for (let i = 0; i < n; i++) {
  const a = S1[i], b = S2[i], c = S3[i], d = S4[i], e = S5[i];
  if (a !== b) continue;
  if (a > 8 || c > 8 || d > 8 || e > 8) continue;
  if (c !== a) loose.push([RAM_BASE + i, [a, b, c, d, e]]);
}
console.log(`\nlooser list (stable on no-press, ordinal in all, moved on press #1): ${loose.length}`);
// rank by how "cursor-like": small max, changed twice
loose.sort((x, y) => Math.max(...x[1]) - Math.max(...y[1]));
for (const [addr, s] of loose.slice(0, 30)) {
  console.log(`  0x${addr.toString(16).toUpperCase()}  series=[${s.join(",")}]`);
}
