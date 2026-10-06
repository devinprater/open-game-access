#!/usr/bin/env node
/**
 * psp-ar-active2.mjs -- loose search: ANY word pointing into the menu descriptor table.
 *
 * The strict (pointer,id) pair found nothing, which means the decomp's holder address
 * (0x34660) is not statically mapped -- consistent with it living in swappable overlay
 * memory. So drop the id requirement and find every pointer into the table, then report
 * which screen the game is on so a null result can be read correctly.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar active2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
try { const st = await db.req("game.status"); console.log("game:", JSON.stringify(st.game)); } catch {}
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();

const hits = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v >= TABLE && v < TABLE + ROWS * STRIDE) hits.push({ holder: RAM_BASE + i, ptr: v, off: v - TABLE, aligned: (v - TABLE) % STRIDE === 0 });
}
console.log(`words pointing into the descriptor table: ${hits.length}`);
for (const h of hits.slice(0, 40)) {
  console.log(`  0x${h.holder.toString(16).toUpperCase()}  -> 0x${h.ptr.toString(16).toUpperCase()}  (table+0x${h.off.toString(16)})${h.aligned ? "  ROW-ALIGNED" : ""}`);
}

// also: the earlier heap pair addresses, to see if we are on a menu at all
const sleep = 0;
for (const a of [0x08BFCF54, 0x08BFCF58]) {
  console.log(`  old heap candidate 0x${a.toString(16).toUpperCase()} = ${ram.readUInt32LE(a - RAM_BASE)}`);
}
// count words in 0x0880xxxx..0x088FFFFF that look like table pointers (sanity)
console.log(`\nnote: table range 0x${TABLE.toString(16).toUpperCase()}..0x${(TABLE + ROWS * STRIDE).toString(16).toUpperCase()}`);
process.exit(0);
