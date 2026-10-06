#!/usr/bin/env node
/**
 * psp-ar-linetrack.mjs -- identify which word is the CURRENT LINE number.
 *
 * Live facts: the chapter-0 story container sits at RAM 0x8BA1B10 (7 lines) and two RAM words
 * hold its base: 0x8AB7C0C and 0x8BA13B0. One of the words around a holder must be the line
 * index -- so this dumps both neighbourhoods, advances the narration, and reports exactly which
 * word changed and by how much.
 *
 * The screen was on line 004, so an advance should take the index to 005.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
import { execFileSync } from "node:child_process";
import { join } from "node:path";
const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar linetrack", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
const off = (v) => v - RAM_BASE;

// locate the story container
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
        const nm = ram.subarray(off(np), e).toString("latin1");
        if (nm.startsWith("MSG_AR_")) { base = RAM_BASE + i; count = c; break; }
      }
    }
    i = ram.indexOf(Buffer.from("!MSG", "latin1"), i + 1);
  }
}
if (base === null) { console.log("no story container"); db.s?.close(); process.exit(0); }

// holders of the container base
const holders = [];
for (let i = 0; i + 4 <= ram.length; i += 4) {
  if (ram.readUInt32LE(i) === base) holders.push(RAM_BASE + i);
}
console.log(`container 0x${base.toString(16).toUpperCase()} count=${count}; holders: ${holders.map((h) => "0x" + h.toString(16).toUpperCase()).join(", ")}`);

const SPAN = 8;   // words either side
async function snap() {
  const out = [];
  for (const h of holders) {
    const from = h - SPAN * 4;
    const b = await db.read(from, (SPAN * 2 + 1) * 4);
    out.push({ holder: h, from, words: Array.from({ length: SPAN * 2 + 1 }, (_, k) => b.readInt32LE(k * 4)) });
  }
  return out;
}

const A = await snap();
console.log("\n=== before ===");
for (const s of A) {
  console.log(`  holder 0x${s.holder.toString(16).toUpperCase()}`);
  for (let k = 0; k < s.words.length; k++) {
    const d = k - SPAN;
    console.log(`     ${d >= 0 ? "+" : ""}${d}  0x${(s.from + k * 4).toString(16).toUpperCase()}  ${s.words[k]}`);
  }
}

console.log("\n=== press cross twice, 5s apart ===");
press("cross"); await sleep(5000);
const B = await snap();
press("cross"); await sleep(5000);
const C = await snap();

console.log("\n=== words that changed ===");
for (let hi = 0; hi < A.length; hi++) {
  for (let k = 0; k < A[hi].words.length; k++) {
    const a = A[hi].words[k], b = B[hi].words[k], c = C[hi].words[k];
    if (a === b && b === c) continue;
    const addr = A[hi].from + k * 4;
    console.log(`  0x${addr.toString(16).toUpperCase()}  ${a} -> ${b} -> ${c}   (offset ${k - SPAN} from holder 0x${A[hi].holder.toString(16).toUpperCase()})`);
  }
}
db.s?.close();
process.exit(0);
