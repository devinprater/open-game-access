#!/usr/bin/env node
/**
 * psp-ar-currentmsg.mjs -- find the LIVE "currently displayed message" pointer.
 *
 * The container chain, from the decompile:
 *   FUN_0011fbf0 sets puRam0009f52c = &DAT_00220200   (the #MSG container)
 *   DAT_00220200 is in .data, so at runtime it is RAM 0x08804000 + 0x220200 = 0x08A24200
 *   fun_000da040 fixed the container up in place, so +0x14 and +0x18 are ABSOLUTE pointers:
 *       +0x12 u16  count
 *       +0x14 ptr  array of pointers to message NAMES  (ASCII)
 *       +0x18 ptr  array of pointers to message TEXT   (UTF-16)
 *
 * So: read the live container, build the set of text pointers, then scan all of RAM for a word
 * equal to one of those pointers OUTSIDE the container's own arrays. Any such word is the
 * engine holding "the text I am currently showing" -- which is exactly what a reader needs.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const BASE = 0x08804000;
const CONTAINER = BASE + 0x220200;   // DAT_00220200
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar curmsg", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

// 1. the container header, live
const hdr = await db.read(CONTAINER, 0x20);
console.log(`container @0x${CONTAINER.toString(16).toUpperCase()}`);
console.log(`  bytes: ${Array.from(hdr).map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
console.log(`  magic: ${hdr.subarray(0, 4).toString("latin1")}`);
console.log(`  count(+0x12) = ${hdr.readUInt16LE(0x12)}`);
const p14 = hdr.readUInt32LE(0x14);
const p18 = hdr.readUInt32LE(0x18);
console.log(`  +0x14 = 0x${p14.toString(16).toUpperCase()}   +0x18 = 0x${p18.toString(16).toUpperCase()}`);

const count = hdr.readUInt16LE(0x12);
if (count === 0 || p14 < 0x08800000 || p18 < 0x08800000) {
  console.log("\nContainer is not loaded/fixed-up yet (pointers are not absolute).");
  console.log("=> this screen is not showing a message, or the loader has not run.");
  db.s?.close();
  process.exit(0);
}

// 2. read both pointer arrays
const names = [], texts = [];
const a14 = await db.read(p14, count * 4);
const a18 = await db.read(p18, count * 4);
for (let i = 0; i < count; i++) {
  names.push(a14.readUInt32LE(i * 4));
  texts.push(a18.readUInt32LE(i * 4));
}
console.log(`\nfirst 5: name ptr 0x${names[0].toString(16)} text ptr 0x${texts[0].toString(16)}`);
// read the first few names/texts to prove the arrays are real
for (let i = 0; i < 4; i++) {
  const nb = await db.read(names[i], 40);
  const end = nb.indexOf(0);
  const name = nb.subarray(0, end > 0 ? end : 40).toString("latin1");
  const tb = await db.read(texts[i], 120);
  let e = 0; while (e + 1 < tb.length && !(tb[e] === 0 && tb[e + 1] === 0)) e += 2;
  const text = tb.subarray(0, e).toString("utf16le");
  console.log(`  [${i}] ${name.padEnd(24)} -> ${JSON.stringify(text.slice(0, 60))}`);
}

// 3. scan RAM for a word equal to one of the text pointers, outside the array itself
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();

const textSet = new Map();
texts.forEach((t, i) => { if (t) textSet.set(t, i); });
const nameSet = new Map();
names.forEach((t, i) => { if (t) nameSet.set(t, i); });

const arrLo = Math.min(p14, p18), arrHi = Math.max(p14 + count * 4, p18 + count * 4);
console.log(`\narray region: 0x${arrLo.toString(16)} .. 0x${arrHi.toString(16)}`);

const hits = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const abs = RAM_BASE + i;
  if (abs >= arrLo && abs < arrHi) continue;      // the arrays themselves
  const v = ram.readUInt32LE(i);
  if (textSet.has(v)) hits.push({ addr: abs, kind: "text", idx: textSet.get(v), ptr: v });
  else if (nameSet.has(v)) hits.push({ addr: abs, kind: "name", idx: nameSet.get(v), ptr: v });
}
console.log(`\n=== ${hits.length} words elsewhere in RAM pointing at a message name/text ===`);
for (const h of hits.slice(0, 40)) {
  console.log(`  0x${h.addr.toString(16).toUpperCase()}  ${h.kind} pointer -> [${h.idx}]`);
}
process.exit(0);
