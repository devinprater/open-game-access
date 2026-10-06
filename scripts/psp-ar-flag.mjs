#!/usr/bin/env node
/**
 * psp-ar-flag.mjs -- find the unlock FLAG array now that the split is known.
 *
 * Established by cycling character select: 15 characters are selectable
 * (Goku, Teen Gohan, Gohan, Future Gohan, Vegeta, Trunks, Future Trunks, Krillin, Piccolo,
 *  Frieza, Android #18, Cell, Majin Buu, Kid Buu, Broly) and 9 of the 24 are locked
 * (Cooler, Gotenks, Gogeta, Vegito, Pikkon, Janemba, Super Buu, Dabura, Bardock).
 *
 * So the flag structure should have 24 slots with exactly 15 "available" and 9 "not".
 * This scans every window in RAM for that shape:
 *   - 24 consecutive bytes, each 0 or 1, with exactly 15 ones
 *   - 24 consecutive bytes with 15 non-zero / 9 zero
 *   - the same at stride 2 and 4
 * and prints the bit/byte pattern so it can be judged against the known roster order.
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar flag", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();
console.log(`ram ${ram.length} bytes; looking for a 24-slot 15-on/9-off structure`);

const results = [];
// u8 windows, stride 1
for (let i = 0; i < ram.length - 24; i++) {
  let ones = 0, ok = true;
  for (let k = 0; k < 24; k++) {
    const v = ram[i + k];
    if (v === 0) continue;
    if (v === 1) { ones++; continue; }
    ok = false; break;
  }
  if (ok && ones === 15) results.push({ addr: RAM_BASE + i, kind: "u8 0/1", pat: Array.from(ram.subarray(i, i + 24)).join("") });
}
// u8 windows, any non-zero counts as on
for (let i = 0; i < ram.length - 24; i++) {
  let nz = 0, ok = true;
  for (let k = 0; k < 24; k++) { const v = ram[i + k]; if (v) nz++; if (v > 0x20) { ok = false; break; } }
  if (ok && nz === 15) results.push({ addr: RAM_BASE + i, kind: "u8 any<=0x20", pat: Array.from(ram.subarray(i, i + 24)).map((x) => x ? 1 : 0).join("") });
}
// u16 stride 2
for (let i = 0; i + 48 <= ram.length; i += 2) {
  let nz = 0, ok = true;
  for (let k = 0; k < 24; k++) { const v = ram.readUInt16LE(i + k * 2); if (v) nz++; if (v > 0x100) { ok = false; break; } }
  if (ok && nz === 15) results.push({ addr: RAM_BASE + i, kind: "u16", pat: Array.from({ length: 24 }, (_, k) => ram.readUInt16LE(i + k * 2) ? 1 : 0).join("") });
}
// u32 stride 4
for (let i = 0; i + 96 <= ram.length; i += 4) {
  let nz = 0, ok = true;
  for (let k = 0; k < 24; k++) { const v = ram.readUInt32LE(i + k * 4); if (v) nz++; if (v > 0x10000) { ok = false; break; } }
  if (ok && nz === 15) results.push({ addr: RAM_BASE + i, kind: "u32", pat: Array.from({ length: 24 }, (_, k) => ram.readUInt32LE(i + k * 4) ? 1 : 0).join("") });
}

// dedupe overlapping addresses of the same kind
const seen = new Set();
const uniq = [];
for (const r of results) {
  const key = `${r.kind}:${r.addr}`;
  if (seen.has(key)) continue;
  seen.add(key); uniq.push(r);
}
console.log(`\ncandidates: ${uniq.length}`);
for (const r of uniq.slice(0, 60)) {
  console.log(`  0x${r.addr.toString(16).toUpperCase()}  ${r.kind.padEnd(14)}  ${r.pat}`);
}
process.exit(0);
