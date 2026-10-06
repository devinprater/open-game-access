#!/usr/bin/env node
/**
 * psp-ar-reader.mjs -- LIVE menu reader for DBZ: Shin Budokai — Another Road (ULUS10234).
 *
 * ⭐ THIS REPLACES THE (list_len, index) HEAP-PAIR APPROACH. That pair moved on every boot
 * AND every screen change, and pattern-matching it was hopeless (3,921 candidates). This
 * route anchors on the engine's own menu struct, found by a signature that is UNIQUE:
 *
 *   locator: the word W such that
 *       W points into the menu descriptor table (RAM 0x089EBBA0, row stride 0x14), AND
 *       the word AFTER W equals the row index
 *   -> exactly ONE match was found while a menu is on screen.
 *
 * From the decompile (FUN_000e1afc) that word is the engine's active-menu pointer:
 *   +0x000 = descriptor table + menuId*0x14     +0x004 = menuId
 *   +0x070 = screen id                          +0x074 = selection (-1 = none)
 *
 * Measured: the struct address is STABLE within a session and across screen changes
 * (0x08BFC758 on every screen tested), and menuId changes 1 -> 3 -> 1 when entering and
 * leaving a submenu -- so menuId is a real SCREEN DISCRIMINATOR, which is what lets the
 * reader name a screen instead of guessing from a list length.
 *
 * ⛔ THE ADDRESS IS STILL PER-BOOT. Re-run the locator each session; the signature is
 *    stable, the address is not.
 *
 * Read-only: never writes emulated memory.
 *
 * Usage:
 *   node psp-ar-reader.mjs            follow changes (text)
 *   node psp-ar-reader.mjs --speak    one speakable line per change
 *   node psp-ar-reader.mjs --json     machine-readable
 *   node psp-ar-reader.mjs --probe    print the located struct once
 */
import process from "node:process";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
const POLL_MS = 150;

// Menu ids seen so far, with the items the earlier session verified against the display.
// Entries are additive: an unknown id reports honestly rather than guessing.
const MENUS = {
  1: { name: "Main menu", items: ["Another Road", "Arcade", "Z Trial", "Network Battle", "Training", "Profile Card", "Options"] },
};

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "Open Game Access AR reader", version: "2.0.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error(`could not connect to ${URL}`)), 10000);
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
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
  close() { try { this.s?.close(); } catch {} }
}

const db = new Debugger();
await db.connect();

async function locate() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  const ram = Buffer.concat(parts);
  const hits = [];
  for (let i = 0; i + 8 <= ram.length; i += 4) {
    const v = ram.readUInt32LE(i);
    if (v < TABLE || v >= TABLE + ROWS * STRIDE) continue;
    if ((v - TABLE) % STRIDE !== 0) continue;
    const id = (v - TABLE) / STRIDE;
    if (ram.readUInt32LE(i + 4) !== id) continue;
    hits.push({ base: RAM_BASE + i, id });
  }
  return hits;
}

const hits = await locate();
if (!hits.length) {
  console.log("No active-menu struct found. Either the game is not on a menu, or the table base changed.");
  console.log("Check: is ULUS10234 running, and is a menu on screen?");
  db.close();
  process.exit(1);
}
if (hits.length > 1) console.log(`WARNING: ${hits.length} struct matches (expected 1): ${hits.map((h) => "0x" + h.base.toString(16)).join(", ")}`);
const STRUCT = hits[0].base;

function describe(menuId, sel) {
  const m = MENUS[menuId];
  if (!m) return { screen: `menu ${menuId}`, item: sel < 0 ? "(nothing selected)" : `item ${sel}`, known: false };
  if (sel < 0) return { screen: m.name, item: "(nothing selected)", known: true };
  const it = m.items[sel];
  return { screen: m.name, item: it ?? `item ${sel} (unmapped)`, known: it != null };
}

if (process.argv.includes("--probe")) {
  const b = await db.read(STRUCT, 0x80);
  const menuId = b.readUInt32LE(0x04), screen = b.readUInt32LE(0x70), sel = b.readUInt32LE(0x74);
  console.log(`struct @0x${STRUCT.toString(16).toUpperCase()}  menuId=${menuId}  screen=${screen}  sel=${sel}`);
  const d = describe(menuId, sel);
  console.log(`=> ${d.screen}: ${d.item}${d.known ? "" : "   <== UNMAPPED"}`);
  db.close();
  process.exit(0);
}

const mode = process.argv.includes("--speak") ? "speak" : process.argv.includes("--json") ? "json" : "plain";
console.log(`menu struct @0x${STRUCT.toString(16).toUpperCase()} (located by signature)`);
let last = null;
for (;;) {
  let menuId, screen, sel;
  try {
    const b = await db.read(STRUCT, 0x80);
    menuId = b.readUInt32LE(0x04); screen = b.readUInt32LE(0x70); sel = b.readUInt32LE(0x74);
  } catch { await new Promise((r) => setTimeout(r, 500)); continue; }
  const key = `${menuId}/${screen}/${sel}`;
  if (key !== last) {
    const d = describe(menuId, sel);
    if (mode === "json") console.log(JSON.stringify({ menuId, screen, sel, ...d, at: Date.now() }));
    else if (mode === "speak") console.log(d.item === "(nothing selected)" ? d.screen : d.item);
    else console.log(`menuId=${menuId} screen=${screen} sel=${sel}  ${d.screen}: ${d.item}${d.known ? "" : "   <== UNMAPPED"}`);
    last = key;
  }
  await new Promise((r) => setTimeout(r, POLL_MS));
}
