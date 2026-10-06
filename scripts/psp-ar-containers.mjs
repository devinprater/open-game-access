#!/usr/bin/env node
/**
 * psp-ar-containers.mjs -- find EVERY loaded #MSG container in RAM, and the LIVE text pointer.
 *
 * Confirmed live: the system container at RAM 0x08A24200 reads "!MSG" -- the '#'->'!' load flag
 * flipped exactly as FUN_000da040 says -- with absolute fixed-up pointers at +0x14 / +0x18.
 *
 * But no word in RAM pointed at one of its message strings, which means the text ON SCREEN is
 * coming from a DIFFERENT container (the story text lives in data_sys_us.afs and is loaded
 * elsewhere). So this scans all of RAM for both '#MSG' and '!MSG', parses each, and then looks
 * for words pointing at ANY string inside those containers -- including pointers into the middle
 * of a text blob, since a reader may well hold an offset into a decoded buffer.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar containers", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
console.log(`ram ${ram.length} bytes`);

function readStr(abs, max = 200) {
  if (abs < RAM_BASE || abs >= RAM_BASE + ram.length) return null;
  const o = abs - RAM_BASE;
  const end = ram.indexOf(0, o);
  if (end < 0 || end - o > max) return null;
  return ram.subarray(o, end).toString("latin1");
}
function readWide(abs, max = 400) {
  if (abs < RAM_BASE || abs >= RAM_BASE + ram.length) return null;
  const o = abs - RAM_BASE;
  let e = o;
  while (e + 1 < ram.length && e - o < max && !(ram[e] === 0 && ram[e + 1] === 0)) e += 2;
  return ram.subarray(o, e).toString("utf16le");
}

// 1. every container, loaded or not
const found = [];
for (const magic of ["#MSG", "!MSG"]) {
  let i = ram.indexOf(Buffer.from(magic, "latin1"));
  while (i !== -1) {
    found.push({ at: RAM_BASE + i, magic });
    i = ram.indexOf(Buffer.from(magic, "latin1"), i + 1);
  }
}
console.log(`\n=== ${found.length} MSG containers in RAM ===`);
const parsed = [];
for (const f of found) {
  const o = f.at - RAM_BASE;
  const count = ram.readUInt16LE(o + 0x12);
  const p14 = ram.readUInt32LE(o + 0x14);
  const p18 = ram.readUInt32LE(o + 0x18);
  const loaded = f.magic === "!MSG";
  const ptrsOk = p14 >= RAM_BASE && p14 < RAM_BASE + ram.length && p18 >= RAM_BASE && p18 < RAM_BASE + ram.length;
  console.log(`  0x${f.at.toString(16).toUpperCase()}  ${f.magic}  count=${String(count).padStart(5)}  +0x14=0x${p14.toString(16)} +0x18=0x${p18.toString(16)}  ${loaded ? "LOADED" : "raw"}${ptrsOk ? "" : "  (pointers not absolute)"}`);
  if (loaded && ptrsOk && count > 0 && count < 20000) {
    const entries = [];
    for (let k = 0; k < count; k++) {
      try {
        const t = ram.readUInt32LE(p14 - RAM_BASE + k * 4);
        const x = ram.readUInt32LE(p18 - RAM_BASE + k * 4);
        entries.push({ name: readStr(t, 60), textPtr: x, text: readWide(x, 400) });
      } catch { break; }
    }
    parsed.push({ base: f.at, count, p14, p18, entries });
  }
}

// 2. show samples so the content is identifiable
for (const c of parsed) {
  console.log(`\n--- container 0x${c.base.toString(16).toUpperCase()} (${c.count} entries) ---`);
  for (let k = 0; k < Math.min(5, c.entries.length); k++) {
    const e = c.entries[k];
    console.log(`   [${k}] ${String(e.name).padEnd(26)} ${JSON.stringify((e.text || "").slice(0, 70))}`);
  }
  const withText = c.entries.filter((e) => e.text).length;
  console.log(`   (${withText}/${c.entries.length} have text)`);
}

// 3. words in RAM pointing into any parsed container's text blob
console.log("\n=== words pointing at a container string, or into a text blob ===");
const strings = [];
for (const c of parsed) for (const e of c.entries) {
  if (e.textPtr) strings.push({ ptr: e.textPtr, name: e.name, text: e.text, base: c.base });
}
const byPtr = new Map(strings.map((s) => [s.ptr, s]));
let hits = 0;
for (let i = 0; i + 4 <= ram.length; i += 4) {
  const v = ram.readUInt32LE(i);
  const s = byPtr.get(v);
  if (s) {
    console.log(`  0x${(RAM_BASE + i).toString(16).toUpperCase()} -> ${s.name}  ${JSON.stringify((s.text || "").slice(0, 50))}`);
    if (++hits > 30) break;
  }
}
if (!hits) console.log("   (none)");
db.s?.close();
process.exit(0);
