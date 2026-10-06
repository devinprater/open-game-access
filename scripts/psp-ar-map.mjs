#!/usr/bin/env node
/**
 * psp-ar-map.mjs -- walk a menu, enter each item, and record what screen opens.
 *
 * For each index it: navigates to the item, records (list_len, index) and a screenshot,
 * presses cross, waits, then records the NEW (list_len, index) and screenshot. The pair of
 * screenshots is what names the submenu; the recorded pair is what makes it readable.
 *
 * Read-only except injected presses.
 *
 * Usage: node psp-ar-map.mjs <lenAddr> <idxAddr> <screenshotName> [maxIndex]
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";
import { copyFileSync, existsSync, readdirSync, statSync } from "node:fs";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-map";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const LEN_ADDR = parseInt(process.argv[2], 16);
const IDX_ADDR = parseInt(process.argv[3], 16);
const TAG = process.argv[4] || "screen";
const MAXIDX = Number(process.argv[5] || "7");
const SKIP = (process.argv[6] || "").split(",").filter(Boolean).map(Number);

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar map", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 15000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async u32(a) { const r = await this.req("memory.read", { address: a, size: 4, replacements: false }); return Buffer.from(r.base64 || "", "base64").readUInt32LE(0); }
}
const db = new D();
await db.connect();
const press = (b, n = 1) => { for (let i = 0; i < n; i++) execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" }); };

function shot(label) {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const dst = join(OUT, `${label}.ppm`);
  copyFileSync(join(SHOTDIR, fresh[0]), dst);
  return dst;
}

const state = async () => `${await db.u32(LEN_ADDR)}/${await db.u32(IDX_ADDR)}`;

console.log(`mapping ${TAG}: len@0x${LEN_ADDR.toString(16).toUpperCase()} idx@0x${IDX_ADDR.toString(16).toUpperCase()}`);
console.log(`start state: ${await state()}`);

for (let idx = 0; idx < MAXIDX; idx++) {
  if (SKIP.includes(idx)) { console.log(`\n[${idx}] skipped`); continue; }
  // navigate: to the top, then down idx times
  press("up", 12);
  if (idx > 0) press("down", idx);
  await sleep(700);
  const before = await state();
  shot(`${TAG}-${idx}-a-item`);
  press("cross");
  await sleep(2600);
  const after = await state();
  const p = shot(`${TAG}-${idx}-b-entered`);
  console.log(`\n[${idx}] item state ${before}  ->  entered ${after}   shot=${p ? "yes" : "NO"}`);
  // back out
  press("circle");
  await sleep(2200);
  const back = await state();
  console.log(`      after circle: ${back}`);
  if (back !== before && back !== after) {
    // didn't return cleanly; try once more
    press("circle");
    await sleep(1800);
    console.log(`      after 2nd circle: ${await state()}`);
  }
}
db.s?.close();
console.log("\ndone.");
process.exit(0);
