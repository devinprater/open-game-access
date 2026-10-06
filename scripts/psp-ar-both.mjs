#!/usr/bin/env node
/**
 * psp-ar-both.mjs -- run BOTH menu-address strategies in ONE debugger session.
 *
 * The debugger allows only one client, and repeatedly connecting/disconnecting leaves
 * sockets in TIME_WAIT that block the next attempt (symptom: "connect timeout" forever).
 * So this does everything on a single connection.
 *
 * EXPERIMENT A -- find a holder that points into the menu descriptor table (ELF 0x1E7BA0,
 *   verified resident at RAM 0x089EBBA0). If the engine's active-menu struct is discoverable
 *   this way, the reader gets a screen id from a pointer, not a guessed heap address.
 *   Strict form also requires the next word to equal the row id.
 *
 * EXPERIMENT B -- the heap (list_len, index) pair: find 4-aligned u32 pairs that look like
 *   (small length, index < length). Reported with addresses so the winning approach is
 *   chosen on evidence, not preference.
 *
 * Also prints the game.status and a liveness control, so a null is readable.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar both", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 30000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new D();
await db.connect();
console.log("connected");

// liveness + context
try { const st = await db.req("game.status"); console.log("game:", st.game?.id, st.game?.title); } catch (e) { console.log("game.status:", e.message); }
try {
  const c1 = await db.req("cpu.status");
  await sleep(1200);
  const c2 = await db.req("cpu.status");
  console.log(`ticks advancing: ${c2.ticks - c1.ticks > 0} (control: the emulator is running)`);
} catch {}

const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
console.log(`ram: ${ram.length} bytes\n`);

console.log("=== EXPERIMENT A: holders pointing into the descriptor table ===");
const aHits = [];
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
  const off = v - TABLE;
  const id = off / STRIDE;
  const next = ram.readUInt32LE(i + 4);
  aHits.push({ holder: RAM_BASE + i, ptr: v, off, id: off % STRIDE === 0 ? id : -1, next, pair: next === id && off % STRIDE === 0 });
}
console.log(`  any pointer into table: ${aHits.length}`);
for (const h of aHits.slice(0, 30)) {
  console.log(`    holder 0x${h.holder.toString(16).toUpperCase()} -> 0x${h.ptr.toString(16).toUpperCase()} (table+0x${h.off.toString(16)}) id=${h.id} next=${h.next}${h.pair ? "   <== POINTER+ID PAIR" : ""}`);
}
const pairs = aHits.filter((h) => h.pair);
console.log(`  => strict (pointer,id) pairs: ${pairs.length}`);

console.log("\n=== EXPERIMENT B: (list_len, index) ordinal pairs ===");
const bHits = [];
for (let i = 0; i + 8 <= ram.length; i += 4) {
  const len = ram.readUInt32LE(i);
  if (len < 2 || len > 32) continue;
  const idx = ram.readUInt32LE(i + 4);
  if (idx >= len) continue;
  bHits.push({ addr: RAM_BASE + i, len, idx });
}
console.log(`  candidates: ${bHits.length}`);
for (const h of bHits.slice(0, 25)) console.log(`    0x${h.addr.toString(16).toUpperCase()}  len=${h.len}  idx=${h.idx}`);
console.log("");

console.log("=== VERDICT INPUT ===");
console.log(`A(strict)=${pairs.length}  A(any)=${aHits.length}  B=${bHits.length}`);
db.s?.close();
process.exit(0);
