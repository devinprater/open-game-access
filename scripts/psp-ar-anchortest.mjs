#!/usr/bin/env node
/**
 * psp-ar-anchortest.mjs -- is "PLAYER\0\0COM\0" a unique re-locatable anchor for the
 * character-select cursor?
 *
 * The cursor (proven, 0x08ABC2E8 this boot) has the inline bytes
 *   50 4c 41 59 45 52 00 00 43 4f 4d 00   ("PLAYER" NUL NUL "COM" NUL)
 * just 0x10 bytes BEFORE it. If that byte string is unique in RAM, then
 *   cursor = (address of that string) + 0x10
 * and the reader can re-locate the cursor every boot (addresses here are per-boot).
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar anchortest", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
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
const parts = [];
for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
const ram = Buffer.concat(parts);
db.s?.close();

function findAll(pat, label) {
  const hits = [];
  let i = ram.indexOf(pat);
  while (i !== -1) { hits.push(RAM_BASE + i); i = ram.indexOf(pat, i + 1); }
  console.log(`${label}: ${hits.length} hit(s)`);
  for (const h of hits.slice(0, 12)) console.log(`    0x${h.toString(16).toUpperCase()}`);
  return hits;
}

// candidate anchors, most specific first
findAll(Buffer.from("PLAYER\x00\x00COM\x00", "latin1"), "PLAYER\\0\\0COM\\0");
findAll(Buffer.from("PLAYER\x00\x00COM", "latin1"), "PLAYER\\0\\0COM");
findAll(Buffer.from("PLAYER\x00COM\x00", "latin1"), "PLAYER\\0COM\\0");

// read the cursor through the anchor and compare against the known value
const hits = findAll(Buffer.from("PLAYER\x00\x00COM\x00", "latin1"), "-> using this one as anchor");
console.log("\n=== cursor via anchor ===");
for (const h of hits.slice(0, 8)) {
  const cur = ram.readUInt32LE(h + 0x10 - RAM_BASE);
  const label = cur <= 24 ? `id ${cur}` : `0x${cur.toString(16)}`;
  console.log(`  anchor 0x${h.toString(16).toUpperCase()} -> cursor 0x${(h + 0x10).toString(16).toUpperCase()}  value=${label}`);
  // also dump the words right around it so the struct shape is recorded
  const vals = [];
  for (let k = 0; k < 6; k++) vals.push(ram.readUInt32LE(h + 0x10 + k * 4 - RAM_BASE));
  console.log(`      +0..+0x14: [${vals.join(", ")}]`);
}
process.exit(0);
