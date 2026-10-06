#!/usr/bin/env node
/**
 * psp-ar-currentline.mjs -- pin the CURRENT LINE inside the live story container.
 *
 * Known live: the story container for the chapter-0 intro is at RAM 0x8BA1B10 (7 lines,
 * MSG_AR_000_00_000..006). The screen was showing entry 004 ("In the ensuing battle with the
 * androids and Cell..."), yet no word in RAM pointed at that entry's TEXT buffer.
 *
 * So the engine tracks the line some other way. This looks for, in order of likelihood:
 *   1. a word equal to the container base      (the engine must hold the container)
 *   2. a word equal to the name-array / text-array pointers
 *   3. a word equal to a NAME string pointer   (readers often keep the name)
 *   4. small words (0..count) sitting right next to any of the above  -> container + index
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const CONTAINER = 0x08BA1B10;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar curline", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const off = (v) => v - RAM_BASE;
const inRam = (v) => v >= RAM_BASE && v < RAM_BASE + ram.length;

// find the story container for real (do not assume the address)
let base = null;
{
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const cnt = ram.readUInt16LE(i + 0x12);
    const p14 = ram.readUInt32LE(i + 0x14);
    if (cnt === 7 && inRam(p14)) {
      const np = ram.readUInt32LE(off(p14));
      const e = ram.indexOf(0, off(np));
      const nm = ram.subarray(off(np), e).toString("latin1");
      if (nm.startsWith("MSG_AR_000_00")) { base = RAM_BASE + i; break; }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
}
if (base === null) { console.log("story container not found (not on a story line right now)"); db.s?.close(); process.exit(0); }
const o = off(base);
const count = ram.readUInt16LE(o + 0x12);
const p14 = ram.readUInt32LE(o + 0x14);
const p18 = ram.readUInt32LE(o + 0x18);
console.log(`story container 0x${base.toString(16).toUpperCase()} count=${count} names@0x${p14.toString(16)} texts@0x${p18.toString(16)}`);

const namePtrs = [], textPtrs = [];
for (let k = 0; k < count; k++) {
  namePtrs.push(ram.readUInt32LE(off(p14) + k * 4));
  textPtrs.push(ram.readUInt32LE(off(p18) + k * 4));
}
console.log("name ptrs:", namePtrs.map((v) => "0x" + v.toString(16)).join(" "));

const targets = new Map();
targets.set(base, "CONTAINER");
if (targets.set) { targets.set(p14, "name-array"); targets.set(p18, "text-array"); }
namePtrs.forEach((v, k) => targets.set(v, "name[" + k + "]"));
textPtrs.forEach((v, k) => targets.set(v, "text[" + k + "]"));

const arrayLo = Math.min(p14, p18), arrayHi = Math.max(p14 + count * 4, p18 + count * 4);

console.log("\n=== words elsewhere in RAM equal to any of those pointers ===");
const hits = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const abs = RAM_BASE + i;
  if (abs >= arrayLo && abs < arrayHi) continue;
  const v = ram.readUInt32LE(i);
  if (targets.has(v)) hits.push({ addr: abs, v, what: targets.get(v) });
}
for (const h of hits.slice(0, 60)) {
  const nb = [];
  for (let d = -3; d <= 3; d++) {
    const a = h.addr + d * 4;
    if (a >= RAM_BASE && a + 4 <= RAM_BASE + ram.length) nb.push(`${d >= 0 ? "+" : ""}${d}:${ram.readUInt32LE(off(a))}`);
  }
  console.log(`  0x${h.addr.toString(16).toUpperCase()} = ${h.what}`);
  console.log(`      neighbours  ${nb.join("  ")}`);
}
console.log(`\n${hits.length} pointer holder(s)`);

console.log("\n=== words elsewhere equal to a small index that could be the LINE number ===");
const smalls = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const abs = RAM_BASE + i;
  if (abs >= arrayLo && abs < arrayHi) continue;
  const v = ram.readUInt32LE(i);
  if (v >= 0 && v <= count) smalls.push({ addr: abs, v });
}
console.log(`${smalls.length} words in 0..${count} (too many to be meaningful alone; listed first 20)`);
for (const s of smalls.slice(0, 20)) console.log(`  0x${s.addr.toString(16).toUpperCase()} = ${s.v}`);
db.s?.close();
process.exit(0);
