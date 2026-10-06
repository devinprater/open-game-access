#!/usr/bin/env node
/**
 * psp-ar-chars.mjs -- find the CHARACTER ROSTER and the per-character records.
 *
 * Lead from task 1: the decoded UI pool holds character titles ("Elite Warrior",
 * "Prince of Destruction", "King of Darkness", "Low-Class Warrior") around 0x08C01D74.
 * A roster is most likely a table of records carrying those titles (or pointers to them).
 *
 * Method: dump RAM, find the title strings, then look for (a) pointer tables into the string
 * region, and (b) structured records near the titles. Reports counts so a noisy result is
 * visible rather than assumed.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
import { readFileSync } from "node:fs";
const ELF = "C:/Users/Devin Prater/AppData/Local/Temp/dbz-ar-extract/EBOOT.dec";

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar chars", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
let elf = null; try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

// 1. all UTF-16LE strings, with addresses
const strings = [];
for (let i = 0; i + 8 <= ram.length; i += 2) {
  let j = i;
  while (j + 1 < ram.length && ram[j + 1] === 0 && ram[j] >= 0x20 && ram[j] < 0x7f) j += 2;
  if (j - i >= 6) { strings.push({ addr: RAM_BASE + i, s: ram.subarray(i, j).toString("utf16le") }); i = j; }
}
console.log(`UTF-16LE strings: ${strings.length}`);

// 2. character titles (the lead) and their addresses
const TITLE_HINT = /(Warrior|Prince|King|Lord|Saiyan|Master|Fighter|Demon|God|Elite|Class|Destruction|Darkness|Champion)/i;
const titles = strings.filter((x) => TITLE_HINT.test(x.s) && x.s.length <= 28 && !inElf(x.s));
console.log(`\n--- title-like strings NOT in the ELF (${titles.length}) ---`);
for (const t of titles.slice(0, 40)) console.log(`  0x${t.addr.toString(16).toUpperCase()}  ${JSON.stringify(t.s)}`);

// 3. the 0x08C01D74 neighbourhood, in address order -- a roster run would be contiguous
const near = strings.filter((x) => x.addr >= 0x08C01000 && x.addr < 0x08C04000).sort((a, b) => a.addr - b.addr);
console.log(`\n--- strings in 0x08C01000-0x08C04000 (${near.length}, address order) ---`);
for (const t of near.slice(0, 70)) console.log(`  0x${t.addr.toString(16).toUpperCase()}  ${JSON.stringify(t.s)}`);
process.exit(0);
