#!/usr/bin/env node
/**
 * psp-ar-cscursor.mjs -- find the CHARACTER CURSOR on character select, using DOWN.
 *
 * Devin's correction: DOWN moves the selection here (Teen Gohan -> Gohan -> Future Gohan ->
 * Vegeta -> Trunks -> Future Trunks); right/left do nothing. The earlier hunt used right/left
 * and therefore found nothing.
 *
 * Method: 5 snapshots (start / no-press / down / down / up) and keep 4-aligned u32s that are
 * byte-stable with no input and step +1, +1, -1. Also reports the neighbouring word as a
 * possible list length, and dumps the roster row area for a lock flag.
 *
 * ONE connection.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar cscursor", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

console.log("S1 start"); const S1 = await dumpAll();
await sleep(1500);
console.log("S2 no-press"); const S2 = await dumpAll();
for (const b of ["down", "down", "up"]) {
  press(b); await sleep(900);
  console.log(`S${3 + ["down", "down", "up"].indexOf(b)} after ${b}`);
}
const S3 = await dumpAll();
press("down"); await sleep(900); console.log("S4 after down #1 (re-taken)");
const S4 = await dumpAll();
press("down"); await sleep(900); console.log("S5 after down #2 (re-taken)");
const S5 = await dumpAll();
press("up"); await sleep(900);
db.s?.close();

const n = Math.min(S1.length, S2.length, S3.length, S4.length, S5.length);
const u32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);

console.log("\n=== ordinal candidates (stable on no-press; +1,+1,-1) ===");
const hits = [];
for (let i = 0; i + 8 < n; i += 4) {
  const a = u32(S1, i), b = u32(S2, i), c = u32(S3, i), d = u32(S4, i), e = u32(S5, i);
  if (a !== b) continue;
  if (a > 0x40 || c > 0x40 || d > 0x40 || e > 0x40) continue;
  if (c === a + 1 && d === c + 1 && e === d - 1) hits.push({ addr: RAM_BASE + i, series: [a, b, c, d, e], lenWord: i >= 4 ? u32(S1, i - 4) : -1 });
}
console.log(`  ${hits.length} candidate(s)`);
for (const h of hits.slice(0, 25)) console.log(`    0x${h.addr.toString(16).toUpperCase()}  [${h.series.join(",")}]  lenWord=${h.lenWord}`);

// looser: any word that changed on down and not on the no-press pair, small
console.log("\n=== looser: small words that move only with input ===");
let cnt = 0;
for (let i = 0; i + 4 < n && cnt < 25; i += 4) {
  const a = u32(S1, i), b = u32(S2, i), c = u32(S3, i);
  if (a === b && a !== c && a <= 0x40 && c <= 0x40) { console.log(`    0x${(RAM_BASE + i).toString(16).toUpperCase()}  start=${a} noPress=${b} after=${c}`); cnt++; }
}
if (!cnt) console.log("    none");

// roster table presence for context
let table = null;
for (let i = 0; i + 4 * 8 <= n; i += 4) {
  let run = 0;
  for (let k = 0; k < 40; k++) { const v = u32(S1, i + k * 4); if (v >= NAME_LO && v <= NAME_HI) run++; else break; }
  if (run >= 8 && (!table || run > table.run)) table = { at: RAM_BASE + i, run };
}
console.log(`\nroster pointer run: @0x${table ? table.at.toString(16).toUpperCase() : "none"} (${table ? table.run : 0})`);
process.exit(0);
