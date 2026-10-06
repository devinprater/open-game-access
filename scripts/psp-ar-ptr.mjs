#!/usr/bin/env node
/**
 * psp-ar-ptr.mjs -- is there a STABLE pointer to the (len,index) pair?
 *
 * If some static location holds 0x08BFCF54 (or the index word), the reader can resolve the
 * pair itself instead of needing a re-derivation every boot. That is worth a lot: the pair
 * moves every boot because it is heap.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar ptr", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

const TARGETS = [0x08BFCF54, 0x08BFCF58, 0x08BFCF50];
const db = new D();
await db.connect();
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();
console.log(`ram ${ram.length} bytes`);

// also report what is AT the pair, to sanity-check
for (const t of TARGETS) {
  const off = t - RAM_BASE;
  console.log(`  target 0x${t.toString(16).toUpperCase()} value=${ram.readUInt32LE(off)}`);
}

for (const target of TARGETS) {
  const needle = Buffer.alloc(4);
  needle.writeUInt32LE(target, 0);
  const found = [];
  let i = ram.indexOf(needle, 0);
  while (i >= 0 && found.length < 20) {
    // only 4-aligned occurrences
    if (i % 4 === 0) found.push(RAM_BASE + i);
    i = ram.indexOf(needle, i + 1);
  }
  console.log(`\npointers to 0x${target.toString(16).toUpperCase()}: ${found.length}`);
  for (const f of found.slice(0, 10)) {
    const region = f < 0x08A83B50 ? "ELF/code segment" : "heap";
    console.log(`  0x${f.toString(16).toUpperCase()}  (${region})`);
  }
}
