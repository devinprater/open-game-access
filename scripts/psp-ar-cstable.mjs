#!/usr/bin/env node
/**
 * psp-ar-cstable.mjs -- dump the character-select structure found at ~0x08B4DB50.
 *
 * The looser hunt turned up a run of words with -1 sentinels and small ids
 * (0x8B4DB50: 10 -> 12, 0x8B4DB9C: -1 -> 3, 0x8B4DBBC: 11 -> -1). A -1 next to a small id is
 * what "empty/locked slot + character id" looks like, so this dumps the whole region and
 * shows it as both u32 and byte views, then re-reads it after moving the cursor to see which
 * slots change.
 */
import { execFileSync } from "node:child_process";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
const FROM = 0x08B4D900, SIZE = 0x600;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar cstable", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

async function dump(label) {
  const b = await db.read(FROM, SIZE);
  console.log(`\n=== ${label} : 0x${FROM.toString(16).toUpperCase()} +0x${SIZE.toString(16)} ===`);
  for (let i = 0; i < SIZE; i += 4) {
    const addr = FROM + i;
    const v = b.readUInt32LE(i);
    const bytes = [0, 1, 2, 3].map((k) => b[i + k]);
    const interesting = bytes.some((x) => x === 0xff) || (v <= 0x30 && v !== 0) || (v >= 0xfffffff0);
    if (interesting) console.log(`  0x${addr.toString(16).toUpperCase()}  u32=${String(v).padStart(11)}  bytes=[${bytes.join(",")}]`);
  }
  return b;
}

const A = await dump("before");
press("down"); await sleep(1200);
const B = await dump("after one down");
press("down"); await sleep(1200);
const C = await dump("after two downs");

console.log("\n=== words that changed between snapshots ===");
for (let i = 0; i < SIZE; i += 4) {
  const a = A.readUInt32LE(i), b = B.readUInt32LE(i), c = C.readUInt32LE(i);
  if (a !== b || b !== c) console.log(`  0x${(FROM + i).toString(16).toUpperCase()}  ${a} -> ${b} -> ${c}   bytes: [${A[i]},${A[i+1]},${A[i+2]},${A[i+3]}] -> [${B[i]},${B[i+1]},${B[i+2]},${B[i+3]}] -> [${C[i]},${C[i+1]},${C[i+2]},${C[i+3]}]`);
}
db.s?.close();
process.exit(0);
