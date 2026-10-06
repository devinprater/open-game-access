#!/usr/bin/env node
/**
 * psp-ar-watch.mjs -- find what WRITES the roster table at runtime.
 *
 * DisRoster.java found 0 static ELF references to the table, so it is built at runtime. The
 * way to find the builder is a WRITE WATCHPOINT on the table, not static analysis.
 *
 * Two things happen here on ONE connection (the debugger allows one client):
 *   1. locate the roster table live (run of pointers into the name block)
 *   2. set memory breakpoints on the table and on the name block, press toward character
 *      select, and report any hit with the PC
 *
 * Read-only except injected presses.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const NAME_LO = 0x08A25550, NAME_HI = 0x08A25800;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class D {
  constructor() { this.q = new Map(); this.t = 1; this.events = []; }
  connect() {
    return new Promise((res, rej) => {
      const s = new WebSocket(URL, "debugger.ppsspp.org");
      this.s = s;
      s.addEventListener("message", (e) => {
        let m; try { m = JSON.parse(String(e.data)); } catch { return; }
        if (m.ticket == null) { this.events.push(m); return; }   // broadcast
        if (this.q.has(String(m.ticket))) {
          const it = this.q.get(String(m.ticket)); this.q.delete(String(m.ticket));
          clearTimeout(it.timer);
          m.event === "error" ? it.rej(new Error(m.message || "request failed")) : it.res(m);
        }
      });
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar watch", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const press = (b, n = 1) => { for (let i = 0; i < n; i++) execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" }); };

async function findTable() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  const ram = Buffer.concat(parts);
  let best = null;
  for (let i = 0; i + 4 * 8 <= ram.length; i += 4) {
    let run = 0;
    for (let k = 0; k < 40; k++) {
      const v = ram.readUInt32LE(i + k * 4);
      if (v >= NAME_LO && v <= NAME_HI) run++; else break;
    }
    if (run >= 8 && (!best || run > best.run)) best = { at: RAM_BASE + i, run };
  }
  return best;
}

// 1. the table exists right now (it is resident text)
const t = await findTable();
console.log(`roster table @0x${t ? t.at.toString(16).toUpperCase() : "NOT FOUND"} (${t ? t.run : 0} pointers)`);

// 2. capability probe: which breakpoint events does this PPSSPP accept?
console.log("\n=== breakpoint API probe ===");
for (const [ev, fields] of [
  ["memory.breakpoint.list", {}],
  ["memory.breakpoint.add", { address: 0x08A243F0, size: 4, type: "write" }],
  ["memory.breakpoint.add", { address: 0x08A243F0, type: "write" }],
]) {
  try { const r = await db.req(ev, fields); console.log(`  ${ev} ${JSON.stringify(fields)} -> OK ${JSON.stringify(r).slice(0, 160)}`); }
  catch (e) { console.log(`  ${ev} ${JSON.stringify(fields)} -> ${e.message}`); }
}

// 3. list what is now armed, then drive and watch for hits
try { const l = await db.req("memory.breakpoint.list"); console.log("\narmed:", JSON.stringify(l)); } catch (e) { console.log("\nlist failed:", e.message); }

console.log("\n=== driving toward character select (main menu -> Training) ===");
db.events.length = 0;
press("up", 12); await sleep(600);
press("down", 4); await sleep(800);
press("cross"); await sleep(4000);
press("cross"); await sleep(3000);

const hits = db.events.filter((e) => JSON.stringify(e).match(/breakpoint|hit|stepping|paused/i));
console.log(`broadcast events mentioning breakpoint/hit: ${hits.length}`);
for (const h of hits.slice(0, 20)) console.log("  " + JSON.stringify(h).slice(0, 200));
console.log(`total broadcasts seen: ${db.events.length}`);
for (const e of db.events.slice(0, 6)) console.log("  sample: " + JSON.stringify(e).slice(0, 150));

// 4. is the table still there, and did its entry count change?
const t2 = await findTable();
console.log(`\nroster table after navigation: @0x${t2 ? t2.at.toString(16).toUpperCase() : "NOT FOUND"} (${t2 ? t2.run : 0} pointers)`);
db.s?.close();
process.exit(0);
