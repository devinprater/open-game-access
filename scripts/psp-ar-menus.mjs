#!/usr/bin/env node
/**
 * psp-ar-menus.mjs — LIVE menu reader for DBZ: Shin Budokai — Another Road (ULUS10234).
 *
 * The host-side adapter for the menus. Polls two words and prints the selected item.
 * No RAM dumps, no screenshots: two 4-byte reads per tick.
 *
 * ⭐ THE ADDRESSES WERE RE-DERIVED ON A LIVE BOOT (2026-10-06) — they are NOT the
 * addresses from the September session. The cursor lives on the HEAP, and the heap is
 * reallocated every boot: the old 0x08C36F98 read 0 and held texture bytes.
 *
 *   list_len = u32(0x08BA1D14)   7 on the main menu, 6 in Options   <- SCREEN DISCRIMINATOR
 *   cursor   = u32(0x08BA1D18)   the selected index within that list
 *
 * How they were confirmed: five RAM snapshots (start, no-press control, down, down, up).
 * The pair above is byte-stable with no input, and it stepped +1 on down, +1 on down,
 * -1 on up. It capped at 6 on a 7-item menu (no wrap), and on entering Options it reset
 * to 0 and then tracked that screen's own list (1 -> 2 -> 1) — the held-out transition.
 *
 * ⛔ THE INDEX ALONE IS NOT ENOUGH. Index 0 is "Another Road" on the main menu and
 * "Assign Buttons" in Options, so an index-only reader announces the wrong item. The
 * length says which list is live, which is why both words are read.
 *
 * ⛔ VERIFY PER BOOT. If it prints "unknown list (length N)", the heap moved again —
 * re-run the hunt rather than trusting these addresses.
 *
 * Read-only: never writes emulated memory.
 *
 * Usage:
 *   node psp-ar-menus.mjs            follow selection changes (plain text)
 *   node psp-ar-menus.mjs --speak    one speakable line per change
 *   node psp-ar-menus.mjs --json     machine-readable events
 *   node psp-ar-menus.mjs --probe    print the raw words once and exit
 */
import process from "node:process";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";

// ---- the addresses, re-derived live (2026-10-06) -----------------------------
const G_LIST_LEN = 0x08BFCF54;
const G_CURSOR = 0x08BFCF58;
const POLL_MS = 120;

// ---- item tables (verified against the game's own display) -------------------
const LISTS = {
  7: { name: "Main menu", items: ["Another Road", "Arcade", "Z Trial", "Network Battle", "Training", "Profile Card", "Options"] },
  6: { name: "Options", items: ["Assign Buttons", "Sound", "Save/Load", "Connection Style", "Screen Display", "Voice Select"] },
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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "Open Game Access AR menus", version: "1.0.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error(`could not connect to ${URL}`)), 8000);
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
  async u32(a) {
    const r = await this.req("memory.read", { address: a, size: 4, replacements: false });
    return Buffer.from(r.base64 || "", "base64").readUInt32LE(0);
  }
  close() { try { this.s?.close(); } catch {} }
}

function describe(len, idx) {
  const list = LISTS[len];
  if (!list) return { screen: "unknown list", item: `length ${len}, index ${idx}`, known: false };
  if (idx >= list.items.length) return { screen: list.name, item: `index ${idx} (out of range)`, known: false };
  return { screen: list.name, item: list.items[idx], known: true };
}

const args = process.argv.slice(2);
const mode = args.includes("--speak") ? "speak" : args.includes("--json") ? "json" : "plain";

const db = new Debugger();
await db.connect();

if (args.includes("--probe")) {
  const len = await db.u32(G_LIST_LEN);
  const cur = await db.u32(G_CURSOR);
  const d = describe(len, cur);
  console.log(`list_len=${len}  cursor=${cur}  => ${d.screen}: ${d.item}`);
  db.close();
  process.exit(0);
}

let lastLen = null, lastIdx = null;
for (;;) {
  let len, idx;
  try { len = await db.u32(G_LIST_LEN); idx = await db.u32(G_CURSOR); }
  catch { await new Promise((r) => setTimeout(r, 500)); continue; }

  if (len !== lastLen || idx !== lastIdx) {
    const d = describe(len, idx);
    const screenChanged = len !== lastLen;
    if (mode === "json") {
      console.log(JSON.stringify({ screen: d.screen, item: d.item, index: idx, length: len, known: d.known, at: Date.now() }));
    } else if (mode === "speak") {
      // On a screen change, name the screen too, so "Assign Buttons" is not mistaken for
      // a main-menu item; otherwise the item alone is enough.
      console.log(screenChanged ? `${d.screen}, ${d.item}` : d.item);
    } else {
      console.log(`${d.screen.padEnd(12)} [${idx}/${len}] ${d.item}${d.known ? "" : "   <== NOT A KNOWN LIST"}`);
    }
    lastLen = len; lastIdx = idx;
  }
  await new Promise((r) => setTimeout(r, POLL_MS));
}
