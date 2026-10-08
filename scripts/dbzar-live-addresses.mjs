#!/usr/bin/env node
/**
 * dbzar-live-addresses.mjs — build the RUNTIME address map for Another Road by scanning the
 * LIVE .text for lui/addiu pairs.
 *
 * ⛔ WHY THIS IS NECESSARY AND WHY THE STATIC FILE IS NOT ENOUGH: EBOOT.dec is relocatable.
 * `.rel.text` holds 57,072 bytes of relocation entries, and the loader patches the
 * `lui r, hi ; addiu r, r, lo` pair in place on load. So `addiu r18, r18, 0x1780` in the file
 * means the array lands at `0x08805780 + relocation`, and on this build it is actually
 * `0x08A852D0` — 2 MiB away. Reading the FILE and doing the arithmetic silently gives the
 * wrong address for every array in the game.
 *
 * This reads the instructions the CPU is ACTUALLY executing and reports the addresses they
 * compute, with the vaddr of each reference so the address can be attributed to a function.
 *
 * Read-only: it issues `memory.read` only. It never writes emulated memory.
 *
 * Usage:
 *   node dbzar-live-addresses.mjs                       # full .text scan, dump the map
 *   node dbzar-live-addresses.mjs --filter 0x08A8      # only addresses whose high 16 bits match
 *   node dbzar-live-addresses.mjs --find 0x08A852D0    # who computes this address
 *
 * Env: PSP_DEBUGGER (default ws://127.0.0.1:12345/debugger)
 */
import { writeFileSync } from "node:fs";

const WS_URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const CHUNK = 0x100000;                       // well under PPSSPP's per-request cap
const TEXT_LO = 0x08804000;                   // EBOOT load base + .text vaddr 0
const TEXT_HI = 0x08A03170;                   // end of .text (+0x19F170 from the base)
const RAM_LO = 0x08800000, RAM_HI = 0x0A000000;
const OUT = "C:/Users/Devin Prater/AppData/Local/hermes/cache/scratch/dbzar-reloc-map.json";

const args = process.argv.slice(2);
const flag = (n, d = null) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] ? args[i + 1] : d; };

const sock = new WebSocket(WS_URL, "debugger.ppsspp.org");
let t = 1; const P = new Map();
const req = (e, f = {}) => new Promise((res, rej) => {
  const tk = String(t++); P.set(tk, { res, rej });
  sock.send(JSON.stringify({ event: e, ticket: tk, ...f }));
  setTimeout(() => { P.delete(tk); rej(new Error(`timeout ${e}`)); }, 60000);
});
sock.addEventListener("message", (ev) => {
  let m; try { m = JSON.parse(String(ev.data)); } catch { return; }
  const p = P.get(String(m.ticket)); if (!p) return;
  P.delete(String(m.ticket));
  if (m.event === "error") p.rej(new Error(m.message || "err")); else p.res(m);
});
await new Promise((res, rej) => {
  sock.addEventListener("open", res, { once: true });
  sock.addEventListener("error", () => rej(new Error("cannot reach the PPSSPP debugger")), { once: true });
});
async function read(addr, size) {
  const parts = [];
  for (let off = 0; off < size; off += CHUNK) {
    const n = Math.min(CHUNK, size - off);
    const r = await req("memory.read", { address: addr + off, size: n, replacements: false });
    parts.push(Buffer.from(r.base64 || "", "base64"));
  }
  return Buffer.concat(parts);
}

const find = flag("find");
if (find) {
  const want = parseInt(find.replace(/^0x/i, ""), 16) >>> 0;
  const text = await read(TEXT_LO, TEXT_HI - TEXT_LO);
  const hits = [];
  for (let i = 0; i + 4 <= text.length; i += 4) {
    const w = text.readUInt32LE(i);
    const op = (w >>> 26) & 0x3F, rs = (w >>> 21) & 0x1F, rt = (w >>> 16) & 0x1F, imm = w & 0xFFFF;
    if (op !== 0x0F || rs !== 0) continue;                       // lui rt, imm
    const hi = (imm << 16) >>> 0;
    if (hi !== (want & 0xFFFF0000)) continue;
    const next = text.readUInt32LE(i + 4);
    const nop = (next >>> 26) & 0x3F, nrs = (next >>> 21) & 0x1F, nrt = (next >>> 16) & 0x1F;
    const nimm = next & 0xFFFF;
    const nlo = nimm & 0x8000 ? nimm - 0x10000 : nimm;
    if ((nop === 0x09 || nop === 0x0D) && nrs === rt) {
      const addr = (hi + nlo) >>> 0;
      if (addr === want) hits.push({ luiAt: (TEXT_LO + i) >>> 0, pairAt: (TEXT_LO + i + 4) >>> 0 });
    }
  }
  console.log(`references to 0x${want.toString(16).toUpperCase()}: ${hits.length}`);
  for (const h of hits) console.log(`   lui @ 0x${h.luiAt.toString(16)}  pair @ 0x${h.pairAt.toString(16)}`);
  sock.close(); process.exit(0);
}

console.log(`scanning live .text 0x${TEXT_LO.toString(16)}..0x${TEXT_HI.toString(16)} ` +
            `(${((TEXT_HI - TEXT_LO) / 1048576).toFixed(2)} MiB)`);
const text = await read(TEXT_LO, TEXT_HI - TEXT_LO);
console.log(`read ${text.length} bytes`);

const lui = new Map();          // reg -> hi
const found = new Map();        // addr -> [vaddrs of the lui]
for (let i = 0; i + 8 <= text.length; i += 4) {
  const w = text.readUInt32LE(i);
  const op = (w >>> 26) & 0x3F, rs = (w >>> 21) & 0x1F, rt = (w >>> 16) & 0x1F, imm = w & 0xFFFF;
  if (op === 0x0F && rs === 0) { lui.set(rt, (imm << 16) >>> 0); continue; }
  if ((op === 0x09 || op === 0x0D) && lui.has(rs)) {
    const base = lui.get(rs);
    const s = op === 0x09 && (imm & 0x8000) ? imm - 0x10000 : imm;
    const addr = (base + s) >>> 0;
    lui.delete(rs);
    if (addr >= RAM_LO && addr < RAM_HI) {
      if (!found.has(addr)) found.set(addr, []);
      const arr = found.get(addr);
      if (arr.length < 6) arr.push(TEXT_LO + i - 4);
    }
    continue;
  }
  if (rt !== 0) lui.delete(rt);
}
const keys = [...found.keys()].sort((a, b) => a - b);
const filter = flag("filter");
const want = filter ? parseInt(filter.replace(/^0x/i, ""), 16) >>> 0 : null;
const shown = want === null ? keys : keys.filter((a) => (a & 0xFFF00000) === (want & 0xFFF00000));
console.log(`resolved RAM addresses: ${keys.length}; shown: ${shown.length}`);
for (const a of shown) {
  console.log(`  0x${a.toString(16).toUpperCase().padStart(8, "0")}   <- ` +
              found.get(a).map((v) => v.toString(16).padStart(8, "0")).join(", "));
}
writeFileSync(OUT, JSON.stringify(
  keys.map((a) => ({ addr: "0x" + a.toString(16).toUpperCase(), refs: found.get(a).map((v) => v.toString(16).padStart(8, "0")) })), null, 1));
console.log(`wrote ${OUT}`);
sock.close(); process.exit(0);
