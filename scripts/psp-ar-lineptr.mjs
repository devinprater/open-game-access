#!/usr/bin/env node
/**
 * psp-ar-lineptr.mjs -- read the current line from the moving pointer.
 *
 * Measured: the word at 0x8AB7C0C advanced 146414352 -> 146415392 (+0x70 = 112 bytes) when the
 * narration advanced one line. So the engine holds a pointer that walks a sequence of 112-byte
 * line records; that word IS the current-line pointer.
 *
 * This dumps the record it points at and finds which field is the line's text.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const HOLDER = 0x08AB7C0C;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar lineptr", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const off = (v) => v - RAM_BASE;

// full RAM once, to find the story container + its pointers
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
const inRam = (v) => v >= RAM_BASE && v < RAM_BASE + ram.length;

let base = null, count = 0;
{
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const c = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    if (c > 0 && c < 4000 && inRam(p14)) {
      const np = ram.readUInt32LE(off(p14));
      if (inRam(np)) {
        const e = ram.indexOf(0, off(np));
        if (ram.subarray(off(np), e).toString("latin1").startsWith("MSG_AR_")) { base = RAM_BASE + i; count = c; break; }
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
}
if (base === null) { console.log("no story container loaded"); db.s?.close(); process.exit(0); }
const o = off(base);
const p18 = ram.readUInt32LE(o + 0x18);
const textPtrs = [];
for (let k = 0; k < count; k++) textPtrs.push(ram.readUInt32LE(off(p18) + k * 4));
const namePtrs = [];
const p14 = ram.readUInt32LE(o + 0x14);
for (let k = 0; k < count; k++) namePtrs.push(ram.readUInt32LE(off(p14) + k * 4));

console.log(`container 0x${base.toString(16).toUpperCase()} count=${count}`);
console.log(`text pointers: ${textPtrs.map((v) => "0x" + v.toString(16)).join(" ")}`);

const cur = ram.readUInt32LE(off(HOLDER));
console.log(`\ncurrent-line pointer (0x${HOLDER.toString(16).toUpperCase()}) = 0x${cur.toString(16).toUpperCase()}`);
const lineIndex = Math.round((cur - base) / 0x70);
console.log(`  -> (value - container) / 0x70 = ${lineIndex}   [expect the index of the line on screen]`);

console.log(`\n=== the 0x70-byte record at 0x${cur.toString(16).toUpperCase()} ===`);
const rec = await db.read(cur, 0x70);
for (let i = 0; i < 0x70; i += 16) {
  const row = [], asc = [];
  for (let j = 0; j < 16 && i + j < 0x70; j++) {
    row.push(rec[i + j].toString(16).padStart(2, "0"));
    asc.push(rec[i + j] >= 0x20 && rec[i + j] < 0x7f ? String.fromCharCode(rec[i + j]) : ".");
  }
  console.log(`  +0x${i.toString(16).padStart(2, "0")}  ${row.join(" ")}  ${asc.join("")}`);
}
console.log("\n  as u32 words:");
for (let i = 0; i + 4 <= 0x70; i += 4) {
  const v = rec.readUInt32LE(i);
  const mark = textPtrs.includes(v) ? "   <== matches a container TEXT pointer" : (namePtrs.includes(v) ? "   <== matches a container NAME pointer" : "");
  const wide = (v >= base && v < base + 0x1000) ? "  (inside container)" : "";
  console.log(`   +0x${i.toString(16).padStart(2, "0")}  ${String(v).padStart(11)}  0x${v.toString(16).padStart(8, "0")}${mark}${wide}`);
}

// also: the next few records, to see the stride as text
console.log("\n=== the next three 0x70-byte records: first word only (shows the walk) ===");
for (let k = 1; k <= 3; k++) {
  const a = cur + k * 0x70;
  const b = await db.read(a, 4);
  console.log(`  +${k}*0x70 = 0x${a.toString(16).toUpperCase()}  first word = 0x${b.readUInt32LE(0).toString(16)}`);
}
db.s?.close();
process.exit(0);
