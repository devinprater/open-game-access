#!/usr/bin/env node
/**
 * psp-ar-vmhook3.mjs -- the working hook: halt on the message lookup, read the id, resume.
 *
 * Measured last run: arming a cpu EXECUTION breakpoint on FUN_000d9bdc (RAM 0x88DDBDC) made the
 * emulator HALT during the next press ("input.buttons.press timed out"). That is the breakpoint
 * firing, not a failure -- the debugger pauses the CPU and the input call then times out.
 *
 * So the loop is:
 *   1. arm the execution breakpoint on FUN_000d9bdc
 *   2. let the game run; when it stops, the CPU is halted at the function entry
 *   3. read the argument registers:  a0 = message ID, a1 = container pointer (0 = default)
 *   4. map that ID to text through the live container (name + UTF-16 text)
 *   5. RESUME, and repeat
 *
 * That yields "which line is being drawn" directly, with no line index to find.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
const LOOKUP = BASE + 0xD9BDC;   // FUN_000d9bdc  (id -> text pointer)

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar vmhook3", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 8000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  async try_(event, fields) { try { return { ok: true, r: await this.req(event, fields) }; } catch (e) { return { ok: false, e: e.message }; } }
}

const db = new D();
await db.connect();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- discover the register / stepping / resume API ----
console.log("=== API discovery ===");
const apiBits = {};
for (const ev of ["cpu.getAllRegs", "cpu.getRegs", "cpu.stepping", "cpu.stepInto", "cpu.resume",
                  "cpu.status", "cpu.getReg", "core.resume", "debugger.resume", "cpu.breakpoint.list"]) {
  const r = await db.try_(ev, ev === "cpu.getReg" ? { name: "a0" } : {});
  if (r.ok) console.log(`  ${ev} -> OK ${JSON.stringify(r.r).slice(0, 260)}`);
  apiBits[ev] = r.ok;
  if (!r.ok) console.log(`  ${ev} -> ERR ${r.e}`);
}
db.s?.close();
process.exit(0);
