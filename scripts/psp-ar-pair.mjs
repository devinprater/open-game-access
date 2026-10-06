#!/usr/bin/env node
/**
 * psp-ar-pair.mjs -- find the (list_len, index) WORD PAIR on this boot.
 *
 * The reader needs both: index alone names the wrong item when two screens share it.
 * Last boot the pair sat 4 bytes apart (len at N, index at N+4), both u32.
 *
 * Method: 5 snapshots (start / no-press / down / down / up), then keep 4-ALIGNED u32s
 * that (a) are identical across the no-press pair, (b) step +1, +1, -1, (c) are small,
 * and (d) have a plausible list length in the word 4 bytes BEFORE them.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar pair", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
}
const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

const snaps = [];
console.log("S1 start"); snaps.push(await dumpAll());
await sleep(1500);
console.log("S2 no-press"); snaps.push(await dumpAll());
for (const b of ["down", "down", "up"]) {
  press(b); await sleep(750);
  console.log(`S${snaps.length + 1} after ${b}`); snaps.push(await dumpAll());
}
db.s?.close();

const [S1, S2, S3, S4, S5] = snaps;
const n = S1.length;
const u32 = (buf, i) => buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) | (buf[i + 3] << 24);

const hits = [];
for (let i = 0; i + 8 < n; i += 4) {
  const a = u32(S1, i), b = u32(S2, i), c = u32(S3, i), d = u32(S4, i), e = u32(S5, i);
  if (a !== b) continue;                        // moves with no input -> churn
  if (a > 0x40 || c > 0x40 || d > 0x40 || e > 0x40) continue;
  if (!(c === a + 1 && d === c + 1 && e === d - 1)) continue;
  const lenWord = i >= 4 ? u32(S1, i - 4) : -1;
  hits.push({ addr: RAM_BASE + i, series: [a, b, c, d, e], lenWord, lenOff: RAM_BASE + i - 4 });
}
console.log(`\n4-aligned (len,index) candidates: ${hits.length}`);
for (const h of hits) {
  const plausible = h.lenWord >= 2 && h.lenWord <= 64;
  console.log(`  index@0x${h.addr.toString(16).toUpperCase()}  series=[${h.series.join(",")}]  lenWord(@0x${h.lenOff.toString(16).toUpperCase()})=${h.lenWord}${plausible ? "   <== PLAUSIBLE LENGTH" : ""}`);
}
