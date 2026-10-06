#!/usr/bin/env node
/**
 * psp-ar-verify.mjs -- verify the menu-cursor candidate properly.
 *
 * The doc's standard: two or more of {several agreeing copies, coherent structure, a
 * HELD-OUT transition}. And a candidate is only a cursor if it does NOT move when you
 * don't press, and DOES move when the selection changes -- and if it tracks a DIFFERENT
 * list (the Options submenu) with the same address.
 *
 * Read-only except injected presses.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const CANDS = [0x08B0C448, 0x08B0C450, 0x08B0C75A, 0x08B0C958, 0x08B0C9C8, 0x08BA1D18];
const MAIN = ["Another Road", "Arcade", "Z Trial", "Network Battle", "Training", "Profile Card", "Options"];

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar verify", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
  async u32(a) { const r = await this.req("memory.read", { address: a, size: 4, replacements: false }); return Buffer.from(r.base64 || "", "base64").readUInt32LE(0); }
}

const db = new D();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

async function readAll(label) {
  const vals = [];
  for (const a of CANDS) vals.push(await db.u32(a));
  console.log(`  ${label.padEnd(22)} ${vals.map((v) => String(v).padStart(3)).join(" ")}`);
  return vals;
}

console.log("addr:                      " + CANDS.map((a) => a.toString(16).toUpperCase().slice(-4).padStart(3)).join(" "));
const base = await readAll("start");

console.log("\n--- no-press control (2 s) ---");
await sleep(2000);
const ctrl = await readAll("no press");
console.log("  control identical:", JSON.stringify(base) === JSON.stringify(ctrl));

console.log("\n--- sequence: down,down,down,up,up ---");
for (const step of ["down", "down", "down", "up", "up"]) {
  press(step); await sleep(700);
  await readAll(`after ${step}`);
}

console.log("\n--- walk to the LAST item (down x8) then check wrap ---");
for (let i = 1; i <= 8; i++) {
  press("down"); await sleep(650);
  const v = await readAll(`down #${i}`);
  if (i === 8) break;
}

console.log("\n--- HELD-OUT: go to Options and track the DIFFERENT list ---");
// navigate to item 6 (Options) then confirm
for (let i = 0; i < 12; i++) {
  const v = await db.u32(CANDS[0]);
  if (v >= 6) break;
  press("down"); await sleep(650);
}
await readAll("at last item");
press("cross"); await sleep(1500);
console.log("  (entered Options) expect a reset then its own tracking:");
await readAll("in Options");
for (const step of ["down", "down", "up"]) {
  press(step); await sleep(700);
  await readAll(`options ${step}`);
}
db.s?.close();
process.exit(0);
