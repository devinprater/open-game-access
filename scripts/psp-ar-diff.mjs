#!/usr/bin/env node
/**
 * psp-ar-diff.mjs -- press ONE button and report exactly which bytes changed, where.
 * Read-only except the injected press.
 *
 * Usage: node psp-ar-diff.mjs circle [--win 0x08C30000 --size 0x20000] [--repeat 3]
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar diff", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
  async read(address, size) {
    const r = await this.req("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
  press(b, duration = 12) { return this.req("input.buttons.press", { button: b, duration }); }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const args = process.argv.slice(2);
const btn = args[0] || "circle";
const flag = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] ? args[i + 1] : d; };
const WIN = parseInt(flag("win", "0x08C30000"));
const SIZE = parseInt(flag("size", "0x20000"));
const REPEAT = Number(flag("repeat", "3"));

const db = new D();
await db.connect();

async function readWin() { return db.read(WIN, SIZE); }

// control: no press at all
console.log(`window 0x${WIN.toString(16).toUpperCase()} +0x${SIZE.toString(16)}  button=${btn}  repeat=${REPEAT}`);
console.log("\n--- CONTROL: no press ---");
let a = await readWin();
await sleep(1200);
let b = await readWin();
let ctrl = 0;
for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) ctrl++;
console.log(`  changed with NO press: ${ctrl} bytes`);

console.log(`\n--- press ${btn} x${REPEAT}, diffing each time ---`);
const seen = new Map();
let prev = await readWin();
for (let r = 0; r < REPEAT; r++) {
  await db.press(btn, 12);
  await sleep(500);
  const now = await readWin();
  const changed = [];
  for (let i = 0; i < prev.length; i++) if (prev[i] !== now[i]) changed.push(i);
  console.log(`  round ${r + 1}: ${changed.length} bytes changed`);
  for (const i of changed.slice(0, 40)) {
    const addr = WIN + i;
    const key = addr;
    if (!seen.has(key)) seen.set(key, []);
    seen.get(key).push({ before: prev[i], after: now[i] });
  }
  prev = now;
}

console.log("\n--- every address that changed, with its value series ---");
const rows = [...seen.entries()].sort((x, y) => x[0] - y[0]);
for (const [addr, series] of rows.slice(0, 60)) {
  const vals = series.map((s) => s.after);
  const small = vals.every((v) => v <= 0x40);
  console.log(`  0x${addr.toString(16).toUpperCase()}  vals=${JSON.stringify(vals)}${small ? "   <== SMALL ORDINAL" : ""}`);
}
console.log(`\ntotal distinct addresses changed: ${rows.length}`);
db.s?.close();
process.exit(0);
