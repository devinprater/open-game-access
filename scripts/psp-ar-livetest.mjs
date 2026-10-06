#!/usr/bin/env node
/**
 * psp-ar-livetest.mjs -- end-to-end proof of the menu reader: run the reader's exact logic
 * while driving the game, and print what it announced at each step.
 *
 * This drives the reader from ONE process so the presses and the announcements cannot
 * drift apart (a background reader + separate presses can attribute a change to the
 * wrong cause).
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const G_LIST_LEN = 0x08BA1D14;
const G_CURSOR = 0x08BA1D18;
const LISTS = {
  7: { name: "Main menu", items: ["Another Road", "Arcade", "Z Trial", "Network Battle", "Training", "Profile Card", "Options"] },
  6: { name: "Options", items: ["Assign Buttons", "Sound", "Save/Load", "Connection Style", "Screen Display", "Voice Select"] },
};
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar livetest", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

function describe(len, idx) {
  const l = LISTS[len];
  if (!l) return `${len}/${idx}  <== unknown list`;
  return `${l.name}: ${l.items[idx] ?? `idx ${idx}?`}`;
}

async function announce(tag) {
  const len = await db.u32(G_LIST_LEN);
  const idx = await db.u32(G_CURSOR);
  console.log(`  ${tag.padEnd(22)} len=${len} idx=${idx}  ->  ${describe(len, idx)}`);
}

console.log("=== live menu reader test ===");
await announce("(start)");
console.log("\n-- leave Options with circle --");
press("circle"); await sleep(1800);
await announce("after circle");
console.log("\n-- walk the main menu: down x3, then up --");
for (const b of ["down", "down", "down", "up"]) {
  press(b); await sleep(800);
  await announce(`after ${b}`);
}
console.log("\n-- enter the last item (Options) with cross --");
press("cross"); await sleep(1800);
await announce("after cross");
db.s?.close();
process.exit(0);
