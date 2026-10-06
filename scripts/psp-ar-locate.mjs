#!/usr/bin/env node
/**
 * psp-ar-locate.mjs -- locate the RUNTIME menu state, and measure the ELF->RAM delta.
 *
 * Why: the decompile's menu-struct address (0xC139C) and active-screen pointer (0x34660)
 * both land inside .text (0x0-0x19F16F = read-only code), so those addresses cannot be
 * where the game writes. They are either wrong or need a delta.
 *
 * The decompile DOES give a strong runtime signature:
 *   *piRam00034660 = (int)(&DAT_001e7ba0 + menuId * 0x14);
 * i.e. somewhere in RAM a word holds a pointer INTO the per-screen descriptor table at
 * ELF 0x1E7BA0. Finding that word locates the active menu screen and reveals the delta.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const LOAD_BASE = 0x08804000;
const DESC_ELF = 0x001E7BA0;
const DESC_RAM = LOAD_BASE + DESC_ELF;         // 0x08A01BA0
const ROWS = 40, STRIDE = 0x14;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar locate", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();
console.log(`ram ${ram.length} bytes; descriptor table should be at 0x${DESC_RAM.toString(16).toUpperCase()}`);

// sanity: does the descriptor table actually live at LOAD_BASE+0x1E7BA0?
const at = ram.readUInt32LE(DESC_RAM - RAM_BASE);
console.log(`  value at 0x${DESC_RAM.toString(16).toUpperCase()} = 0x${at.toString(16)} (expect a .text pointer like 0x000ED204)`);

// scan for any word pointing INTO the descriptor table
const lo = DESC_RAM, hi = DESC_RAM + ROWS * STRIDE;
const hits = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v >= lo && v < hi && (v - lo) % STRIDE === 0) {
    hits.push({ addr: RAM_BASE + i, ptr: v, id: (v - lo) / STRIDE });
  }
}
console.log(`\nwords pointing into the descriptor table: ${hits.length}`);
for (const h of hits.slice(0, 40)) {
  console.log(`  holder 0x${h.addr.toString(16).toUpperCase()}  -> 0x${h.ptr.toString(16).toUpperCase()}  (menu id ${h.id})`);
}

// if the active-screen pointer lives at ELF 0x34660, its holder should be at RAM 0x08838660
const holder = 0x08804000 + 0x34660;
const hv = ram.readUInt32LE(holder - RAM_BASE);
console.log(`\nvalue at the decompile's active-screen holder 0x${holder.toString(16).toUpperCase()} = 0x${hv.toString(16)}`);
console.log(hv >= lo && hv < hi ? "  => MATCHES the descriptor table (so 0x34660 IS the holder)" : "  => does NOT point at the descriptor table");
