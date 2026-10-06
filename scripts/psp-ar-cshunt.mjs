#!/usr/bin/env node
/**
 * psp-ar-cshunt.mjs -- on the CHARACTER SELECT screen, find:
 *   1. the selection cursor (ordinal hunt: no-press control, then right/left/up/down)
 *   2. the roster ROW records, including any per-character flag that differs between rows
 *
 * Why: the unlock flag was not in the ELF, not in a parallel array beside the name table, and
 * not in plaintext in the save. A locked character's row must differ from an unlocked one, so
 * this dumps the screen's state and looks for a roster-shaped structure.
 *
 * ONE connection (the debugger allows one client).
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const NAME_LO = 0x08A25550, NAME_HI = 0x08A25800;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar cshunt", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// 0. roster table still resident?
const ram0 = await dumpAll();
let table = null;
for (let i = 0; i + 4 * 8 <= ram0.length; i += 4) {
  let run = 0;
  for (let k = 0; k < 40; k++) { const v = ram0.readUInt32LE(i + k * 4); if (v >= NAME_LO && v <= NAME_HI) run++; else break; }
  if (run >= 8 && (!table || run > table.run)) table = { at: RAM_BASE + i, run };
}
console.log(`roster table: @0x${table ? table.at.toString(16).toUpperCase() : "NOT FOUND"} (${table ? table.run : 0} pointers)`);

// 1. ordinal hunt for the cursor: S1 start, S2 no-press, S3 right, S4 right, S5 left
const snaps = [];
console.log("snapshot 1 (start)"); snaps.push(await dumpAll());
await sleep(1500);
console.log("snapshot 2 (no press)"); snaps.push(await dumpAll());
for (const b of ["right", "right", "left"]) {
  press(b); await sleep(800);
  console.log(`snapshot ${snaps.length + 1} (after ${b})`); snaps.push(await dumpAll());
}
const [S1, S2, S3, S4, S5] = snaps;
const n = S1.length;
const u32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);

console.log("\n=== ordinal candidates (stable on no-press, +1,+1,-1) ===");
const hits = [];
for (let i = 0; i + 8 < n; i += 4) {
  const a = u32(S1, i), b = u32(S2, i), c = u32(S3, i), d = u32(S4, i), e = u32(S5, i);
  if (a !== b) continue;
  if (a > 0x40 || c > 0x40 || d > 0x40 || e > 0x40) continue;
  if (c === a + 1 && d === c + 1 && e === d - 1) hits.push({ addr: RAM_BASE + i, series: [a, b, c, d, e] });
}
console.log(`  ${hits.length} candidate(s)`);
for (const h of hits.slice(0, 20)) console.log(`    0x${h.addr.toString(16).toUpperCase()}  [${h.series.join(",")}]`);

// 2. any 24-slot structure: look for a run of small values whose count matches the roster,
//    and report the byte layout so a flag array is visible if present
console.log("\n=== 24-entry small-value runs anywhere (u8, stride 1, <=4) ===");
let found = 0;
for (let i = 0; i + 24 <= n && found < 12; i++) {
  let ok = true;
  for (let k = 0; k < 24; k++) if (S1[i + k] > 4) { ok = false; break; }
  if (ok) { console.log(`    0x${(RAM_BASE + i).toString(16).toUpperCase()}  [${Array.from(S1.subarray(i, i + 24)).join(",")}]`); found++; i += 24; }
}
if (!found) console.log("    none");
db.s?.close();
process.exit(0);
