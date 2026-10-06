#!/usr/bin/env node
/**
 * psp-ar-chaptersel.mjs -- probe the Another Road CHAPTER SELECT state.
 *
 * Established live: DAT_001b11d4 (RAM 0x89B51D4) goes 0 -> 1 on entering Another Road, and the
 * screen is "Chapter Select" (TOTAL complete %, City DF. %).
 *
 * This watches the story bytes 0x1B11D0-0x1B11E0 while pressing DOWN/UP, to see whether the
 * chapter index (DAT_001b11d5) or its neighbour (DAT_001b11d6, used as a second argument to
 * FUN_000314a8) tracks the chapter cursor.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const BASE = 0x08804000;
const WATCH = BASE + 0x1B11D0;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class Debugger {
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar chaptersel", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 20000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new Debugger();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

async function snap(tag) {
  const b = await db.read(WATCH, 0x10);
  const bytes = Array.from(b);
  const d4 = b[4], d5 = b[5], d6 = b[6];
  console.log(`${tag.padEnd(12)} bytes=[${bytes.join(",")}]  ARflag(d4)=${d4} chapterIdx(d5)=${d5} field(d6)=${d6}`);
  return { d4, d5, d6, bytes };
}

console.log("waiting for the screen to settle...");
await sleep(3000);
const base = await snap("baseline");

console.log("\n=== pressing DOWN x8 ===");
const downs = [];
for (let i = 1; i <= 8; i++) {
  press("down"); await sleep(1500);
  downs.push(await snap(`down${i}`));
}

console.log("\n=== pressing UP x3 ===");
for (let i = 1; i <= 3; i++) {
  press("up"); await sleep(1500);
  const s = await snap(`up${i}`);
}

db.s?.close();
process.exit(0);
