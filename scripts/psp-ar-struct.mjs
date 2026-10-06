#!/usr/bin/env node
/**
 * psp-ar-struct.mjs -- read the menu module's state struct (fixed address 0xC139C) live.
 *
 * From the decompile (DbzArDecomp / DisMenu.java):
 *   FUN_000e1afc  is the menu module init. It stores its task handles at 0xC139C[0..2],
 *                 takes a menu id, and sets:
 *                     0xC139C + 0x70  = the menu SCREEN ID
 *                     0xC139C + 0x74  = the SELECTED ITEM ID   (-1 = nothing selected)
 *   FUN_000e2604  reads +0x74 and compares it to -1.
 *
 * If that struct is live, the reader gets BOTH a screen id and the selection from a FIXED
 * address -- no per-boot heap re-derivation.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const STRUCT = 0x088C539C;         // 0x08804000 + 0xC139C
const OFF_ID = 0x70, OFF_SEL = 0x74;
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar struct", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const buf = await db.read(STRUCT, 0x120);
console.log(`struct @0x${STRUCT.toString(16).toUpperCase()}`);
console.log("word-by-word (offset : value : as hex):");
for (let i = 0; i < 0x120; i += 4) {
  const v = buf.readUInt32LE(i);
  const mark = (i === OFF_ID) ? "  <== +0x70 MENU ID" : (i === OFF_SEL) ? "  <== +0x74 SELECTION" : "";
  if (v !== 0 || mark) console.log(`  +0x${i.toString(16).padStart(3, "0")} : ${String(v).padStart(11)} : 0x${v.toString(16).padStart(8, "0")}${mark}`);
}
console.log("");
console.log(`MENU ID (+0x70) = ${buf.readUInt32LE(OFF_ID)}`);
console.log(`SELECTION (+0x74) = ${buf.readUInt32LE(OFF_SEL)}`);
db.s?.close();
process.exit(0);
