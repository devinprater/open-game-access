#!/usr/bin/env node
/**
 * psp-ar-desc.mjs -- read the menu DESCRIPTOR TABLE live and enumerate the menus.
 *
 * From the decompile (DisMenu.java):  FUN_000e1afc does
 *     *puRam000c139c = handle;               // menu state struct, behind a pointer
 *     *piRam00034660 = (int)(&DAT_001e7ba0 + menuId * 0x14);
 * So the per-menu descriptor table is at ELF 0x1E7BA0. With the confirmed ELF->RAM delta
 * (ghidra + 0x08804000), it is at RAM 0x08A01BA0 -- and it was observed BYTE-PRESENT there
 * with correctly relocated pointers (0x088F1204 etc.), so it is readable now.
 *
 * Row stride 0x14. Row index == menu id. Prints each row's words and decodes any word that
 * lands in the code segment as a function address.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const TABLE = 0x089EBBA0;      // 0x08804000 + 0x1E7BA0
const STRIDE = 0x14, ROWS = 32;
const CODE_LO = 0x08804000, CODE_HI = 0x08A83B50;   // first LOAD segment (relocated)

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar desc", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 15000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new D();
await db.connect();
const buf = await db.read(TABLE, ROWS * STRIDE);
console.log(`menu descriptor table @0x${TABLE.toString(16).toUpperCase()}  (${ROWS} rows x 0x${STRIDE.toString(16)})`);
console.log("row  idx  " + Array.from({ length: 5 }, (_, i) => `w${i}`.padStart(11)).join(" "));
for (let r = 0; r < ROWS; r++) {
  const words = [];
  for (let w = 0; w < 5; w++) words.push(buf.readUInt32LE(r * STRIDE + w * 4));
  const any = words.some((v) => v !== 0);
  if (!any) { console.log(`  ${String(r).padStart(2)}   (empty)`); continue; }
  const desc = words.map((v) => (v >= CODE_LO && v < CODE_HI) ? `fn:${(v - 0x08804000).toString(16)}` : String(v)).map((s) => s.padStart(11));
  console.log(`  ${String(r).padStart(2)}   ${desc.join(" ")}`);
}
db.s?.close();
process.exit(0);
