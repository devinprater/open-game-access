#!/usr/bin/env node
/**
 * psp-ar-active.mjs -- find the ACTIVE MENU state at runtime using the verified descriptor
 * table as the anchor.
 *
 * The decompile says (FUN_000e1afc):
 *     piRam00034660[1] = menuId;
 *     *piRam00034660  = (int)(&DAT_001e7ba0 + menuId * 0x14);
 *
 * The table is CONFIRMED present at RAM 0x089EBBA0 with relocated handler pointers, so
 * whatever holds `table + id*0x14` IS the active-menu struct. Search RAM for exactly that,
 * and require the next word to BE the id -- that pair is the signature, and it cannot be
 * a coincidence.
 *
 * This avoids the ambiguous 0x34660/0xC139C addresses entirely.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar active", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
console.log(`ram ${ram.length} bytes; table @0x${TABLE.toString(16).toUpperCase()}`);

// --- 1. words equal to table + id*stride, with id following ---
const hits = [];
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
  const off = v - TABLE;
  if (off % STRIDE !== 0) continue;
  const id = off / STRIDE;
  const next = ram.readUInt32LE(i + 4);
  if (next === id) hits.push({ holder: RAM_BASE + i, ptr: v, id, next, third: ram.readUInt32LE(i + 8), fourth: ram.readUInt32LE(i + 12) });
}
console.log(`\n(pointer->table, id) pairs: ${hits.length}   <-- this is the active-menu struct`);
for (const h of hits) {
  console.log(`  holder 0x${h.holder.toString(16).toUpperCase()}  ->table[${h.id}]  id=${h.next}  w2=${h.third}  w3=${h.fourth}`);
}

// --- 2. if found, dump the struct + hunt the selection nearby ---
if (hits.length) {
  const h = hits[0];
  const base = h.holder;
  const buf = await (async () => { const d2 = new D(); await d2.connect(); const b = await d2.read(base - 0x40, 0x140); d2.s?.close(); return b; })();
  console.log(`\nstruct area around 0x${base.toString(16).toUpperCase()} (-0x40..+0x100):`);
  for (let i = 0; i < buf.length; i += 4) {
    const addr = base - 0x40 + i;
    const v = buf.readUInt32LE(i);
    const marks = [];
    if (addr === base) marks.push("<== table ptr");
    if (addr === base + 4) marks.push("<== menu id");
    if (v > 0 && v < 0x40) marks.push("small");
    console.log(`  0x${addr.toString(16).toUpperCase()}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}  ${marks.join(" ")}`);
  }
}
process.exit(0);
