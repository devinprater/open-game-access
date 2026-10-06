#!/usr/bin/env node
/**
 * psp-ar-cursorctx.mjs -- dump the u32 neighbourhood around the cursor on each menu, so a
 * per-screen field (list length, screen id, list pointer) shows up as a difference.
 *
 * index 0 is ambiguous between "Another Road" and "Assign Buttons"; something near the
 * cursor should say which list is live.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const CURSOR = 0x08BA1D18;
const FROM = 0x08BA1C80, SIZE = 0x180;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar ctx", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
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
  async u32(a) { return (await this.read(a, 4)).readUInt32LE(0); }
}
const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

async function ctx(label) {
  console.log(`\n=== ${label} ===  cursor=${await db.u32(CURSOR)}`);
  const buf = await db.read(FROM, SIZE);
  const rows = [];
  for (let i = 0; i < SIZE; i += 4) {
    const off = FROM + i;
    const v = buf.readUInt32LE(i);
    const mark = off === CURSOR ? "  <== CURSOR" : "";
    rows.push([off, v, mark]);
  }
  for (const [off, v, mk] of rows) console.log(`  0x${off.toString(16).toUpperCase()}  ${String(v).padStart(11)}  (0x${v.toString(16).padStart(8, "0")})${mk}`);
  return rows;
}

const mainRows = await ctx("MAIN MENU (cursor should be 6 = Options)");
press("cross"); await sleep(1800);
const optRows = await ctx("OPTIONS (cursor resets)");
db.s?.close();

console.log("\n=== words that DIFFER between the two screens ===");
for (let i = 0; i < mainRows.length; i++) {
  if (mainRows[i][1] !== optRows[i][1]) {
    console.log(`  0x${mainRows[i][0].toString(16).toUpperCase()}  main=${mainRows[i][1]} (0x${mainRows[i][1].toString(16)})  options=${optRows[i][1]} (0x${optRows[i][1].toString(16)})`);
  }
}
