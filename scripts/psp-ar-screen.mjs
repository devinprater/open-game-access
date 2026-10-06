#!/usr/bin/env node
/**
 * psp-ar-screen.mjs -- find a SCREEN DISCRIMINATOR so the cursor index is not ambiguous.
 *
 * Why it matters: index 0 means "Another Road" on the main menu and "Assign Buttons" in
 * Options. The index alone cannot name an item -- something must say WHICH list it is.
 *
 * Prints the cursor plus every non-zero byte in a window around the menu struct the
 * decompile names (file-offset 0xC139C), so the two screens can be compared.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const CURSOR = 0x08BA1D18;
const WIN = 0x088C539C;      // 0x08804000 + 0xC139C
const SIZE = 0x120;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar screen", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
  async read(address, size) { const r = await this.req("memory.read", { address, size, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  async u32(a) { return (await this.read(a, 4)).readUInt32LE(0); }
}

const label = process.argv[2] || "screen";
const db = new D();
await db.connect();
console.log(`=== ${label} ===`);
console.log(`cursor @0x${CURSOR.toString(16).toUpperCase()} = ${await db.u32(CURSOR)}`);
const buf = await db.read(WIN, SIZE);
const nz = [];
for (let i = 0; i < SIZE; i++) if (buf[i] !== 0) nz.push(`0x${(WIN + i).toString(16).toUpperCase().slice(-6)}=${buf[i]}`);
console.log(`non-zero bytes in 0x${WIN.toString(16).toUpperCase()} +0x${SIZE.toString(16)} (${nz.length}):`);
console.log("  " + nz.join("  "));
// also the u32 view of the first 0x40 bytes, since a screen id may be a word
const words = [];
for (let i = 0; i < 0x40; i += 4) words.push(`${buf.readUInt32LE(i)}`);
console.log(`first 16 words: ${words.join(", ")}`);
db.s?.close();
process.exit(0);
