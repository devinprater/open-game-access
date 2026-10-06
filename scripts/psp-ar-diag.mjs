#!/usr/bin/env node
/**
 * psp-ar-diag.mjs -- is the EMULATOR actually running the guest, or is it paused/stepping?
 * A game that ignores every button while input broadcasts fire is the classic signature of
 * a PAUSED emulator, not a frozen game.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar diag", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const db = new D();
await db.connect();

const st = await db.req("game.status");
console.log("game.status:", JSON.stringify(st));

try {
  const c1 = await db.req("cpu.status");
  console.log("cpu.status (1):", JSON.stringify(c1));
  await sleep(2000);
  const c2 = await db.req("cpu.status");
  console.log("cpu.status (2):", JSON.stringify(c2));
  if (c1.ticks != null && c2.ticks != null) {
    const d = c2.ticks - c1.ticks;
    console.log(`ticks advanced by ${d} in 2 s  -> ${d > 0 ? "EMULATOR IS RUNNING" : "EMULATOR IS NOT ADVANCING"}`);
  }
  if (c1.stepping) console.log("!! stepping = TRUE -- the guest is frozen in the debugger");
  if (c1.paused) console.log("!! paused = TRUE");
} catch (e) { console.log("cpu.status failed:", e.message); }

// a couple of live reads to prove the memory pipe still works
const u = await db.read(0x08800000, 16);
console.log("read @0x08800000:", u.toString("hex"));

db.s?.close();
process.exit(0);
