#!/usr/bin/env node
/**
 * psp-ar-reader.mjs -- LIVE menu + character-select reader for
 * DBZ: Shin Budokai — Another Road (ULUS10234).
 *
 * TWO anchors, because the game uses two different structures:
 *
 *  A) MENUS -- engine active-menu struct, found by a UNIQUE signature:
 *       the word W such that
 *         W points into the menu descriptor table (RAM 0x089EBBA0, row stride 0x14), AND
 *         the word AFTER W equals the row index
 *     -> exactly ONE match while a menu is on screen.
 *     From the decompile (FUN_000e1afc):
 *       +0x000 = descriptor table + menuId*0x14   +0x004 = menuId
 *       +0x070 = screen id                        +0x074 = selection (-1 = none)
 *     menuId is a real SCREEN DISCRIMINATOR (measured 1 -> 3 -> 1 entering/leaving a submenu).
 *
 *  B) CHARACTER SELECT -- the menu struct is NOT present on this screen (locateMenu returns
 *     zero hits), so it needs its own anchor:
 *       the byte string "PLAYER\0\0COM\0", whose +0x10 word is the highlighted character's
 *       NAME-TABLE ID (0..23).
 *     Measured: 2 occurrences of that string in RAM; the real one is the one whose +0x10 word
 *     is <= 24 (or the -1 RANDOM sentinel) and whose next three words are zero. The other is a
 *     plain text copy with garbage following.
 *
 *  ⛔ IDS ARE NAME-TABLE IDS, NOT LIST POSITIONS. The character-select list uses the game's OWN
 *     display order, so the id JUMPS as you press down:
 *        0 Goku, 1 Teen Gohan, 2 Gohan, 18 Future Gohan, 3 Vegeta, 4 Trunks,
 *        23 Future Trunks, 5 Krillin, 6 Piccolo, 7 Frieza, 8 Android #18, 9 Cell,
 *        19 Majin Buu, 10 Kid Buu, 11 Cooler, 12 Broly   (then RANDOM)
 *     ⛔ This is why the earlier +1/+1/-1 ordinal hunt found NOTHING on this screen: the value
 *     does not step by one, it jumps (2 -> 18 -> 3 -> 4 -> 23 -> 5 ...). Do not "fix" the hunt
 *     back to +1/+1/-1.
 *
 *  ⛔ BOTH ADDRESSES ARE PER-BOOT. Re-run the locators each session; the SIGNATURES are stable,
 *     the addresses are not.
 *
 * Read-only: never writes emulated memory.
 *
 * Usage:
 *   node psp-ar-reader.mjs            follow changes (text)
 *   node psp-ar-reader.mjs --speak    one speakable line per change
 *   node psp-ar-reader.mjs --json     machine-readable
 *   node psp-ar-reader.mjs --probe    locate once and print
 */
import process from "node:process";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000, RAM_SIZE = 0x01800000, CHUNK = 0x100000;
const TABLE = 0x089EBBA0, STRIDE = 0x14, ROWS = 24;
const POLL_MS = 150;

// Menu ids seen so far, with the items the earlier session verified against the display.
// `desc` is the game's OWN on-screen description for each item, read from the decoded
// UTF-16LE pool the engine builds below the menu struct. The order matches the item order,
// so item 0's description is desc[0]. Entries are additive: an unknown id reports honestly.
const MENUS = {
  1: {
    name: "Main menu",
    items: ["Another Road", "Arcade", "Z Trial", "Network Battle", "Training", "Profile Card", "Options"],
    desc: [
      "An original story that takes place after \"Trunks Another Story.\"",
      "In this mode, you fight CPU opponents one after the other at the World Tournament.",
      "Participate in battles with various conditions and test your limits.",
      "Battle other players in ad hoc mode. (Maximum 2 players).",
      "Select an opponent and practice.",
      "Manage profile cards or view battle data.",
      "Edit various settings and Save/Load.",
    ],
  },
  // ⛔ The item WORDS below come from the game's own resident text pool (verified), but the
  // DISPLAY ORDER of the last entries is not yet confirmed live (pool order is data order).
  6: {
    name: "Options",
    items: ["Assign Buttons", "Sound", "Save/Load", "Connection Style", "Voice Select", "Credits", "Screen Display"],
    desc: [
      "Change controls in a battle as desired.",
      "Adjust the volume of music and sound.",
      "Save/Load game data.",
      "Select whether games can be interrupted by challengers.",
      "Switch voices.",
      "View credits.",
      "Set the display for the Health and Ki Gauges.",
    ],
  },
};

// The 24 roster names in NAME-TABLE order (verified — dbzar-roster.md).
const CHARS = [
  "Goku", "Teen Gohan", "Gohan", "Vegeta", "Trunks", "Krillin", "Piccolo", "Frieza",
  "Android #18", "Cell", "Kid Buu", "Cooler", "Broly", "Gotenks", "Gogeta", "Vegito",
  "Pikkon", "Janemba", "Future Gohan", "Majin Buu", "Super Buu", "Dabura", "Bardock",
  "Future Trunks",
];
// The game's OWN selectable order, as ids (verified live by cycling with DOWN).
const DISPLAY_IDS = [0, 1, 2, 18, 3, 4, 23, 5, 6, 7, 8, 9, 19, 10, 11, 12];
const RANDOM_ID = -1;

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
      s.addEventListener("open", async () => { try { await this.req("version", { name: "Open Game Access AR reader", version: "3.0.0" }); res(); } catch (e) { rej(e); } });
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

async function ramImage() {
  const parts = [];
  for (let o = 0; o < RAM_SIZE; o += CHUNK) parts.push(await db.read(RAM_BASE + o, Math.min(CHUNK, RAM_SIZE - o)));
  return Buffer.concat(parts);
}

/** A) the engine's active-menu struct, by unique signature */
function locateMenu(ram) {
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

/** B) the character-select cursor, anchored on "PLAYER\0\0COM\0" + 0x10 */
const CS_ANCHOR = Buffer.from("PLAYER\x00\x00COM\x00", "latin1");
function locateCharSel(ram) {
  const hits = [];
  let i = ram.indexOf(CS_ANCHOR);
  while (i !== -1) {
    const cur = i + 0x10;
    if (cur + 0x10 <= ram.length) {
      const v = ram.readUInt32LE(cur);
      // ⛔ Only +0x04 and +0x08 are reliably zero (measured: [5,0,0,1,0,0] — a 1 appears at
      // +0x0C), so do NOT require +0x0C to be zero; that filter silently rejects the real
      // cursor. The text copy of the string has garbage at +0x04, so these two are enough.
      const tailZero = ram.readUInt32LE(cur + 4) === 0 && ram.readUInt32LE(cur + 8) === 0;
      if (tailZero && (v <= 0x30 || v === 0xffffffff)) hits.push({ base: RAM_BASE + cur, v });
    }
    i = ram.indexOf(CS_ANCHOR, i + 1);
  }
  return hits;
}

function describeMenu(menuId, sel) {
  const m = MENUS[menuId];
  if (!m) return { screen: `menu ${menuId}`, item: sel < 0 ? "(nothing selected)" : `item ${sel}`, known: false };
  if (sel < 0 || sel >= m.items.length) return { screen: m.name, item: "(nothing selected)", known: true };
  const it = m.items[sel];
  const d = m.desc ? m.desc[sel] : undefined;
  return { screen: m.name, item: it ?? `item ${sel} (unmapped)`, known: it != null, desc: d };
}

function describeChar(id) {
  if (id === RANDOM_ID || id > 23) return { screen: "Select Characters", item: "Random", known: true };
  const name = CHARS[id] ?? `character id ${id} (unmapped)`;
  const pos = DISPLAY_IDS.indexOf(id);
  return { screen: "Select Characters", item: name, known: CHARS[id] != null, pos };
}

// ---- locate once, preferring whichever structure is actually present ----
let ram = await ramImage();
const menuHits = locateMenu(ram);
const csHits = locateCharSel(ram);
let MENU_STRUCT = menuHits.length ? menuHits[0].base : null;
let CS_CURSOR = csHits.length ? csHits[0].base : null;

if (process.argv.includes("--probe")) {
  console.log(`menu struct : ${MENU_STRUCT ? "0x" + MENU_STRUCT.toString(16).toUpperCase() + ` (menuId=${menuHits[0].id})` : "not present"}`);
  console.log(`char cursor : ${CS_CURSOR ? "0x" + CS_CURSOR.toString(16).toUpperCase() + ` (id=${csHits[0].v})` : "not present"}`);
  if (MENU_STRUCT) {
    const b = await db.read(MENU_STRUCT, 0x80);
    const d = describeMenu(b.readUInt32LE(0x04), b.readUInt32LE(0x74));
    console.log(`=> ${d.screen}: ${d.item}${d.known ? "" : "   <== UNMAPPED"}`);
  } else if (CS_CURSOR) {
    const d = describeChar((await db.read(CS_CURSOR, 4)).readUInt32LE(0));
    console.log(`=> ${d.screen}: ${d.item}${d.known ? "" : "   <== UNMAPPED"}`);
  } else {
    console.log("=> no known structure found (not on a menu or character select)");
  }
  db.close();
  process.exit(0);
}

const mode = process.argv.includes("--speak") ? "speak" : process.argv.includes("--json") ? "json" : "plain";
if (MENU_STRUCT) console.log(`menu struct @0x${MENU_STRUCT.toString(16).toUpperCase()}`);
if (CS_CURSOR) console.log(`character cursor @0x${CS_CURSOR.toString(16).toUpperCase()}`);

let last = null;
let sinceRelocate = 0;
for (;;) {
  let d = null, raw = null;
  try {
    if (MENU_STRUCT) {
      const b = await db.read(MENU_STRUCT, 0x80);
      const menuId = b.readUInt32LE(0x04), screen = b.readUInt32LE(0x70), sel = b.readUInt32LE(0x74);
      if (menuId >= 1 && menuId <= 80) { d = describeMenu(menuId, sel); raw = { menuId, screen, sel }; }
    }
    if (!d && CS_CURSOR) {
      const v = (await db.read(CS_CURSOR, 4)).readUInt32LE(0);
      if (v <= 0x30 || v === 0xffffffff) { d = describeChar(v); raw = { charId: v }; }
    }
  } catch { await new Promise((r) => setTimeout(r, 500)); continue; }

  // if neither structure answers, re-locate: addresses are per-boot, and a screen change can
  // move us to the structure the other anchor covers
  if (!d && ++sinceRelocate >= 8) {
    sinceRelocate = 0;
    try {
      ram = await ramImage();
      const mh = locateMenu(ram), ch = locateCharSel(ram);
      MENU_STRUCT = mh.length ? mh[0].base : null;
      CS_CURSOR = ch.length ? ch[0].base : null;
    } catch {}
  }
  if (!d) { await new Promise((r) => setTimeout(r, POLL_MS)); continue; }
  sinceRelocate = 0;

  const key = JSON.stringify(raw);
  if (key !== last) {
    if (mode === "json") console.log(JSON.stringify({ ...raw, ...d, at: Date.now() }));
    else if (mode === "speak") console.log(d.item === "(nothing selected)" ? d.screen : d.item);
    else console.log(`${JSON.stringify(raw)}  ${d.screen}: ${d.item}${d.known ? "" : "   <== UNMAPPED"}`);
    last = key;
  }
  await new Promise((r) => setTimeout(r, POLL_MS));
}
