#!/usr/bin/env node
/**
 * psp-ar-page.mjs -- print the decoded UI strings in a chosen RAM page.
 *
 * The page at 0x08BF0000 sits immediately below the menu struct (0x08BFC758), so the screen
 * labels are likely there. Prints that page (and 0x08A2000 for comparison).
 *
 * Usage: node psp-ar-page.mjs [pageHex]
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar page", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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

const page = parseInt(process.argv[2] || "0x08BF0000");
const db = new D();
await db.connect();
const buf = await db.read(page, 0x10000);
db.s?.close();
console.log(`page 0x${page.toString(16).toUpperCase()} (0x10000 bytes)`);

let elf = null;
try { elf = readFileSync(ELF); } catch {}
const inElf = (s) => (elf ? elf.indexOf(Buffer.from(s, "latin1")) >= 0 : false);

const out = [];
for (let i = 0; i + 8 <= buf.length; i += 2) {
  let j = i;
  while (j + 1 < buf.length && buf[j + 1] === 0 && buf[j] >= 0x20 && buf[j] < 0x7f) j += 2;
  if (j - i >= 8) {
    const s = buf.subarray(i, j).toString("utf16le");
    out.push({ addr: page + i, s, inElf: inElf(s) });
    i = j;
  }
}
console.log(`UTF-16LE strings >=4 chars: ${out.length}  (${out.filter((o) => o.inElf).length} also in the ELF)`);
console.log("\n--- strings NOT in the ELF (runtime-decoded) ---");
for (const o of out.filter((x) => !x.inElf).slice(0, 90)) console.log(`  0x${o.addr.toString(16).toUpperCase()}  ${JSON.stringify(o.s)}`);
