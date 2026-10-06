#!/usr/bin/env node
/**
 * psp-ar-cshunt2.mjs -- find the character-select cursor by its ACTUAL value sequence.
 *
 * Why every earlier hunt failed: the ordinal hunt assumed the selection steps +1,+1,-1. On the
 * MAIN MENU it does. On CHARACTER SELECT it does NOT, because the display order is built from
 * NAME-TABLE ids, which jump:
 *
 *   display order -> name-table ids
 *   Goku 0, Teen Gohan 1, Gohan 2, Future Gohan 18, Vegeta 3, Trunks 4, Future Trunks 23,
 *   Krillin 5, Piccolo 6, Frieza 7, Android #18 8, Cell 9, Majin Buu 19, Kid Buu 10, Broly 12
 *
 * So the cursor word's successive values are a SUBSET of
 *   {0,1,2,3,4,5,6,7,8,9,10,12,18,19,23}
 * and are DISTINCT as the list advances (until it wraps). That is a much tighter signature
 * than +1/+1/-1, and it is what this hunts for -- at u8, u16 and u32, with a no-press control
 * snapshot so moving-but-unrelated noise is excluded.
 */
import { execFileSync } from "node:child_process";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const OUTJSON = "C:/Users/Devin Prater/AppData/Local/Temp/ar-cshunt2.json";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// name-table ids that are SELECTABLE, in display order
const DISPLAY_IDS = [0, 1, 2, 18, 3, 4, 23, 5, 6, 7, 8, 9, 19, 10, 12];
const IDSET = new Set(DISPLAY_IDS);
// display positions 0..14 are also worth testing (the cursor might be the position, not the id)
const POSSET = new Set(Array.from({ length: 15 }, (_, i) => i));

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar cshunt2", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

const db = new Debugger();
await db.connect();
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });
async function dumpAll() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

// snapshots: ctrl (no press) then 5 downs
console.log("ctrl"); const CTRL = await dumpAll();
const snaps = [];
for (let i = 1; i <= 5; i++) { press("down"); await sleep(1300); console.log(`down ${i}`); snaps.push(await dumpAll()); }
db.s?.close();

const n = snaps[0].length;
const results = [];

function seriesAt(get, i) { return snaps.map((s) => get(s, i)); }

for (const [width, get] of [
  [1, (b, i) => b[i]],
  [2, (b, i) => b.readUInt16LE(i)],
  [4, (b, i) => b.readUInt32LE(i)],
]) {
  const step = width;
  for (let i = 0; i + width <= n; i += step) {
    const ser = seriesAt(get, i);
    const ctrl = get(CTRL, i);
    // all values must be in the id set (or the position set)
    if (!ser.every((v) => IDSET.has(v) || POSSET.has(v))) continue;
    // distinct until wrap: allow the last to return to the first
    const uniq = new Set(ser);
    if (uniq.size < ser.length && !(uniq.size === ser.length - 1)) continue;
    // require the sequence to be a contiguous run of the display order (ids) or positions
    const idRun = ser.every((v, k) => v === DISPLAY_IDS[(DISPLAY_IDS.indexOf(ser[0]) + k) % DISPLAY_IDS.length]);
    const posRun = ser.every((v, k) => v === (ser[0] + k) % 15);
    if (!idRun && !posRun) continue;
    results.push({ addr: RAM_BASE + i, width, kind: idRun ? "display-id run" : "position run", series: ser, ctrl });
  }
}

console.log(`\n=== cursor candidates: ${results.length} ===`);
for (const r of results.slice(0, 40)) {
  console.log(`  0x${r.addr.toString(16).toUpperCase()}  u${r.width * 8}  ${r.kind}`);
  console.log(`        series=[${r.series.join(", ")}]  ctrl=${r.ctrl}`);
}
if (!results.length) console.log("  none");

writeFileSync(OUTJSON, JSON.stringify(results, null, 2));
console.log(`\nwrote ${OUTJSON}`);
process.exit(0);
