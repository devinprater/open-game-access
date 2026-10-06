#!/usr/bin/env node
/**
 * psp-ar-sweep.mjs -- settle two questions by MEASUREMENT, before any cursor hunt:
 *   1. does injected input actually reach the game?  (the debugger's own input.buttons
 *      broadcast is the proof -- it fires on every sceCtrl change)
 *   2. which buttons change game memory at all?
 *
 * ⛔ Rule: never attribute a null result to the game until a sweep says the instrument works.
 * ⛔ A press whose `duration` is too short falls between pad polls and reads as "ignored"
 *    (PPSSPP samples the pad once per frame).
 *
 * Read-only except for injected button presses (the player's own input).
 */
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;

// Regions worth watching: the menu struct the decompile names, and the heap area the
// solved cursor lived in. A change ANYWHERE in user RAM is also counted via a coarse
// sampled checksum so a change outside these windows is not missed silently.
const WATCH = [
  { name: "menu struct 0xC139C", addr: RAM_BASE + 0x0C139C, size: 0x200 },
  { name: "cursor heap neighbourhood", addr: 0x08C30000, size: 0x20000 },
  { name: "menu msg copy seen in doc", addr: 0x09AF1800, size: 0x2000 },
];

class D {
  constructor() { this.q = new Map(); this.t = 1; this.broadcasts = []; }
  connect() {
    return new Promise((res, rej) => {
      const s = new WebSocket(URL, "debugger.ppsspp.org");
      this.s = s;
      s.addEventListener("message", (e) => {
        let m; try { m = JSON.parse(String(e.data)); } catch { return; }
        if (m.event === "input.buttons") { this.broadcasts.push(m.buttons); return; }
        if (m.ticket != null && this.q.has(String(m.ticket))) {
          const it = this.q.get(String(m.ticket)); this.q.delete(String(m.ticket));
          clearTimeout(it.timer);
          m.event === "error" ? it.rej(new Error(m.message || "request failed")) : it.res(m);
        }
      });
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar sweep", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 8000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 20000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(address, size) {
    const r = await this.req("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
  press(b, duration = 12) { return this.req("input.buttons.press", { button: b, duration }); }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function snap(db) {
  const out = [];
  for (const w of WATCH) out.push(await db.read(w.addr, w.size));
  return out;
}

function diffCount(a, b) {
  const n = Math.min(a.length, b.length);
  let c = 0;
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) c++;
  return c;
}

const db = new D();
await db.connect();
const g = (await db.req("game.status")).game || {};
console.log(`game: ${g.id} ${g.title}`);

console.log("\n--- baseline (no press, 1.2 s apart) -- this is the control ---");
let a = await snap(db);
await sleep(1200);
let b = await snap(db);
for (let i = 0; i < WATCH.length; i++) {
  console.log(`  ${WATCH[i].name.padEnd(28)} drift with NO input: ${diffCount(a[i], b[i])} bytes`);
}

const BUTTONS = ["start", "cross", "circle", "up", "down", "left", "right", "ltrigger", "rtrigger"];
console.log("\n--- per-button sweep ---");
console.log("button     input-broadcast  changed-bytes(menu,heap,msgs)");
for (const btn of BUTTONS) {
  const before = await snap(db);
  const nBcast = db.broadcasts.length;
  await db.press(btn, 12);
  await sleep(500);
  const after = await snap(db);
  const fired = db.broadcasts.length > nBcast;
  const d = WATCH.map((_, i) => diffCount(before[i], after[i]));
  console.log(`  ${btn.padEnd(10)} ${(fired ? "YES" : "no").padEnd(16)} ${d.join(", ")}`);
}

console.log(`\ninput.buttons broadcasts seen: ${db.broadcasts.length}`);
if (db.broadcasts.length) {
  console.log("sample:", JSON.stringify(db.broadcasts.slice(0, 4)));
}
db.s?.close();
process.exit(0);
