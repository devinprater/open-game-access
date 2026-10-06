#!/usr/bin/env node
/**
 * psp-dbzar-base.mjs -- DERIVE the Another Road load base from the running game.
 *
 * Method (never guess a base): take strings whose ELF vaddr is known, find the same
 * bytes in a live RAM dump, and compute  base = RAM_addr - vaddr.  Several independent
 * strings agreeing IS the confirmation.
 *
 * Read-only: this never writes emulated memory.
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
const RAM_SIZE = 0x01800000;
const CHUNK = 0x100000;

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
      s.addEventListener("open", async () => {
        try { await this.req("version", { name: "oga dbzar base", version: "0.1.0" }); res(); }
        catch (e) { rej(e); }
      });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 25000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(address, size) {
    const r = await this.req("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
}

const NEEDLES = [
  { vaddr: 0x001A3D30, s: "%05ddmg" },
  { vaddr: 0x001A3D24, s: "%02dHIT" },
  { vaddr: 0x001A4EE8, s: "[SYS] AUTO SAVE" },
  { vaddr: 0x001A4598, s: "data_sys_us" },
  { vaddr: 0x001A4F40, s: "[TITLE]" },
];

const db = new D();
await db.connect();
console.log("connected");

try {
  const st = await db.req("game.status");
  console.log("game.status:", JSON.stringify(st));
} catch (e) { console.log("game.status failed:", e.message); }

// full 24 MiB dump
const parts = [];
for (let off = 0; off < RAM_SIZE; off += CHUNK) {
  const size = Math.min(CHUNK, RAM_SIZE - off);
  parts.push(await db.read(RAM_BASE + off, size));
}
const ram = Buffer.concat(parts);
console.log("ram bytes:", ram.length);

console.log("");
console.log("needle                found_at      computed_base   consistent?");
const bases = [];
for (const n of NEEDLES) {
  const needle = Buffer.from(n.s, "latin1");
  let idx = 0, first = -1;
  while (true) {
    const i = ram.indexOf(needle, idx);
    if (i < 0) break;
    first = i; break;
  }
  if (first < 0) {
    console.log(`${n.s.padEnd(20)}  NOT FOUND (vaddr 0x${n.vaddr.toString(16).toUpperCase()})`);
    continue;
  }
  const at = RAM_BASE + first;
  const base = at - n.vaddr;
  bases.push(base);
  console.log(`${n.s.padEnd(20)}  0x${at.toString(16).toUpperCase().padStart(8, "0")}    0x${base.toString(16).toUpperCase().padStart(8, "0")}`);
}

console.log("");
if (bases.length) {
  const uniq = [...new Set(bases)];
  console.log("distinct bases:", uniq.map((b) => "0x" + b.toString(16).toUpperCase()).join(", "));
  if (uniq.length === 1) {
    const b = uniq[0];
    console.log(`=> LOAD BASE = 0x${b.toString(16).toUpperCase()}  (4-byte aligned: ${b % 4 === 0})`);
    console.log(`   sample runtime addrs: menu struct 0x${(b + 0x0C139C).toString(16).toUpperCase()}, vtable reg 0x${(b + 0x34660).toString(16).toUpperCase()}`);
  } else {
    console.log("=> INCONSISTENT -- do not trust any single string; investigate.");
  }
} else {
  console.log("=> NO STRING FOUND -- is the game past boot? Are the vaddrs right?");
}
db.s?.close();
process.exit(0);
