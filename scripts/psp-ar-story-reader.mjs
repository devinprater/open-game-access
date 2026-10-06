#!/usr/bin/env node
/**
 * psp-ar-story-reader.mjs -- LIVE story-line reader for DBZ Another Road.
 *
 * HOW IT WORKS (all verified live):
 *   1. Story text lives in data_sys_us.afs and is loaded ON DEMAND. While a cutscene is on
 *      screen, an additional "#MSG" container appears in RAM whose message NAMES are
 *      MSG_AR_<chapter>_<scene>_<line>, e.g. MSG_AR_000_00_000..006 for the chapter-0 intro.
 *   2. The game's own loader (FUN_000da040) fixes the container up IN PLACE, so at runtime:
 *          byte 0     '!' means loaded
 *          +0x12 u16  count
 *          +0x14 ptr  array of pointers to message NAMES (ASCII)
 *          +0x18 ptr  array of pointers to message TEXT   (UTF-16LE)
 *      So the reader parses the container itself -- no external lookup file, no guessing.
 *   3. The container's names identify the message set, and the game walks its entries in order
 *      as the narration advances (verified: the on-screen lines matched entries 001, 002, 003).
 *
 * Modes:
 *   node psp-ar-story-reader.mjs           follow the active story container (text)
 *   node psp-ar-story-reader.mjs --speak   one speakable line per change
 *   node psp-ar-story-reader.mjs --json    machine readable
 *   node psp-ar-story-reader.mjs --probe   report once and exit
 *   node psp-ar-story-reader.mjs --list    dump every line in the active container
 *
 * Read-only: never writes emulated memory.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const POLL_MS = 400;

class Debugger {
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "Open Game Access AR story reader", version: "1.0.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error(`could not connect to ${URL}`)), 10000);
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
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  close() { try { this.s?.close(); } catch {} }
}

const db = new Debugger();
await db.connect();

async function ramImage() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

const inRam = (v, ram) => v >= RAM_BASE && v < RAM_BASE + ram.length;
const off = (v) => v - RAM_BASE;

function cstr(ram, abs, max = 80) {
  if (!inRam(abs, ram)) return null;
  const o = off(abs);
  const end = ram.indexOf(0, o);
  if (end < 0 || end - o > max) return null;
  const s = ram.subarray(o, end).toString("latin1");
  return /^[\x20-\x7e]+$/.test(s) ? s : null;
}
function wstr(ram, abs, max = 600) {
  if (!inRam(abs, ram)) return null;
  const o = off(abs);
  let e = o;
  while (e + 1 < ram.length && e - o < max && !(ram[e] === 0 && ram[e + 1] === 0)) e += 2;
  if (e === o) return null;
  const s = ram.subarray(o, e).toString("utf16le");
  return s;
}

/** find every loaded story container: !MSG whose names start with MSG_AR */
function findStoryContainers(ram) {
  const out = [];
  let i = ram.indexOf(Buffer.from("!MSG", "latin1"));
  while (i !== -1) {
    const base = RAM_BASE + i;
    try {
      const count = ram.readUInt16LE(i + 0x12);
      const p14 = ram.readUInt32LE(i + 0x14);
      const p18 = ram.readUInt32LE(i + 0x18);
      if (count > 0 && count < 4000 && inRam(p14, ram) && inRam(p18, ram)) {
        const entries = [];
        for (let k = 0; k < count; k++) {
          const np = ram.readUInt32LE(off(p14) + k * 4);
          const tp = ram.readUInt32LE(off(p18) + k * 4);
          const name = np ? cstr(ram, np, 60) : null;
          const text = tp ? wstr(ram, tp) : null;
          entries.push({ k, name, text, textPtr: tp });
        }
        const ar = entries.filter((e) => e.name && e.name.startsWith("MSG_AR_"));
        if (ar.length === count && count > 0) out.push({ base, count, p14, p18, entries });
      }
    } catch {}
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
  return out;
}

/** words elsewhere in RAM that point at one of the container's strings -> the "current" message */
function currentPointer(ram, cont) {
  const byPtr = new Map();
  for (const e of cont.entries) if (e.textPtr) byPtr.set(e.textPtr, e);
  const arrLo = Math.min(cont.p14, cont.p18);
  const arrHi = Math.max(cont.p14 + cont.count * 4, cont.p18 + cont.count * 4);
  const found = [];
  for (let i = 0; i + 4 <= ram.length; i += 4) {
    const abs = RAM_BASE + i;
    if (abs >= arrLo && abs < arrHi) continue;
    const v = ram.readUInt32LE(i);
    if (byPtr.has(v)) found.push({ holder: abs, entry: byPtr.get(v) });
  }
  return found;
}

const mode = process.argv.includes("--probe") ? "probe"
  : process.argv.includes("--list") ? "list"
  : process.argv.includes("--speak") ? "speak"
  : process.argv.includes("--json") ? "json" : "plain";

if (mode === "probe" || mode === "list") {
  const ram = await ramImage();
  const conts = findStoryContainers(ram);
  if (!conts.length) {
    console.log("no story container loaded (not showing story text)");
    db.close(); process.exit(0);
  }
  for (const c of conts) {
    console.log(`story container @0x${c.base.toString(16).toUpperCase()}  ${c.count} lines`);
    const cur = currentPointer(ram, c);
    if (cur.length) for (const h of cur) console.log(`   current-line holder @0x${h.holder.toString(16).toUpperCase()} -> [${h.entry.k}] ${h.entry.name}`);
    if (mode === "list") for (const e of c.entries) console.log(`   [${e.k}] ${e.name}  ${JSON.stringify((e.text || "").replace(/\n/g, "\\n"))}`);
    else for (const e of c.entries.slice(0, 3)) console.log(`   [${e.k}] ${e.name}  ${JSON.stringify((e.text || "").slice(0, 70))}`);
  }
  db.close(); process.exit(0);
}

console.log("watching for story text...");
let lastKey = null;
let lastRam = null;
for (;;) {
  let ram;
  try { ram = await ramImage(); } catch { await new Promise((r) => setTimeout(r, 800)); continue; }
  const conts = findStoryContainers(ram);
  if (!conts.length) {
    if (lastKey !== "none") { lastKey = "none"; if (mode !== "json") console.log("(no story text on screen)"); }
    await new Promise((r) => setTimeout(r, POLL_MS));
    continue;
  }
  const c = conts[0];
  const cur = currentPointer(ram, c);
  const key = c.base + "/" + c.count + "/" + (cur.length ? cur[0].entry.k : -1);
  if (key !== lastKey) {
    const line = cur.length ? cur[0].entry : null;
    if (mode === "json") {
      console.log(JSON.stringify({ container: "0x" + c.base.toString(16), count: c.count,
        current: line ? { index: line.k, name: line.name, text: line.text } : null,
        all: c.entries.map((e) => ({ index: e.k, name: e.name })), at: Date.now() }));
    } else if (mode === "speak") {
      if (line?.text) console.log(line.text.replace(/\n/g, " "));
      else console.log(`story text loaded (${c.count} lines)`);
    } else {
      console.log(`container 0x${c.base.toString(16).toUpperCase()} (${c.count} lines)`);
      if (line) console.log(`  current [${line.k}] ${line.name}: ${JSON.stringify((line.text || "").replace(/\n/g, " "))}`);
      else console.log("  (current line not identified; use --list)");
    }
    lastKey = key;
  }
  lastRam = ram;
  await new Promise((r) => setTimeout(r, POLL_MS));
}
