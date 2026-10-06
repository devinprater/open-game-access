#!/usr/bin/env node
/**
 * psp-ar-find.mjs -- LOCATE the (list_len, index) pair by PATTERN, with no hardcoded address.
 *
 * Why this is necessary: the pair lives on the heap and moves on every BOOT *and* every
 * SCREEN change (it read 0xFFFFFFFF on a different screen). No pointer to it exists in RAM,
 * so the only robust way is to recognise its shape each time.
 *
 * Shape: a u32 LIST LENGTH (small, 1..64) immediately followed by a u32 INDEX < length.
 * Prints every candidate with its address and the pair values, so the count is visible and
 * a too-noisy pattern is discovered rather than assumed.
 *
 * Usage: node psp-ar-find.mjs [--win 0x08A00000] [--size 0x200000]
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const args = process.argv.slice(2);
const flag = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] ? parseInt(args[i + 1]) : d; };
const WIN = flag("win", 0x08A00000);
const SIZE = flag("size", 0x200000);
const CHUNK = 0x100000;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar find", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
for (let o = 0; o < SIZE; o += CHUNK) parts.push(await db.read(WIN + o, Math.min(CHUNK, SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();

const u32 = (i) => ram.readUInt32LE(i);
const cands = [];
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const len = u32(i);
  if (len < 1 || len > 64) continue;
  const idx = u32(i + 4);
  if (idx >= len) continue;
  // require the two words to be exactly the pair (no third word needed); note the address
  cands.push({ addr: WIN + i, len, idx });
}
console.log(`scanned 0x${WIN.toString(16).toUpperCase()} +0x${SIZE.toString(16)}  ->  ${cands.length} (len,index) candidates`);
for (const c of cands.slice(0, 60)) {
  console.log(`  0x${c.addr.toString(16).toUpperCase()}   len=${String(c.len).padStart(3)}  idx=${c.idx}`);
}
if (cands.length > 60) console.log(`  ... ${cands.length - 60} more`);
