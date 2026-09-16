#!/usr/bin/env node
/**
 * psp-probe.mjs — read-only inspection of a running PSP game through PPSSPP's
 * built-in WebSocket debugger.
 *
 * WHY THIS EXISTS: Open Game Access' native adapters live inside the emulator core,
 * and there is no PSP core in the app. PPSSPP is a standalone Windows program whose
 * only scriptable surface is this debugger. So a PSP reader is built HERE, host-side,
 * and follows the same contract as an in-core adapter: identify the game, read real
 * game state, refuse to guess.
 *
 * ⛔ THIS TOOL NEVER WRITES EMULATED MEMORY. Everything below is a read plus, when
 * asked, a button press — the player's own input, injected. No pokes, ever: the whole
 * point of the project is that accessibility reads state rather than mutating it.
 *
 * Usage:
 *   node psp-probe.mjs status
 *   node psp-probe.mjs dump   [--out DIR] [--press cross,cross] [--pause]
 *   node psp-probe.mjs scan   [--out DIR] [--min 6] [--grep TEXT]
 *   node psp-probe.mjs press  cross[,cross...]
 *
 * PPSSPP must be running with the debugger enabled (RemoteDebuggerOnStartup=True in
 * psp.ini, or --debugger=PORT on the command line).
 */
import { writeFileSync, readFileSync, mkdirSync, existsSync } from "node:fs";
import { join } from "node:path";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";

// PSP user RAM. 24 MiB, and the offset arithmetic that matters: a PSP address A is at
// file offset (A - RAM_BASE). Getting this wrong silently reads unrelated bytes.
const RAM_BASE = 0x08800000;
const RAM_SIZE = 0x01800000;
const CHUNK = 0x100000;             // 1 MiB per request, well under the 2 MiB cap

// ⛔ `duration` IS A FRAME COUNT, NOT MILLISECONDS. PPSSPP samples the pad once per
// frame, so a small hold falls between polls and the game sees nothing — which looks
// exactly like "the emulator is ignoring input". 12 frames is ~200 ms at 60 fps.
const HOLD_FRAMES = 12;

// ---------------------------------------------------------------------------
// Minimal debugger client. Deliberately self-contained rather than importing the
// Another Road project's copy: this file is the deliverable and should not depend on
// a path outside the repo.
// ---------------------------------------------------------------------------
class Debugger {
  constructor(url = URL, timeoutMs = 20000) {
    this.url = url; this.timeoutMs = timeoutMs;
    this.socket = null; this.pending = new Map(); this.nextTicket = 1;
  }

  async connect() {
    const socket = new WebSocket(this.url, "debugger.ppsspp.org");
    this.socket = socket;
    socket.addEventListener("message", (e) => this.#onMessage(String(e.data)));
    socket.addEventListener("close", () => this.#failAll(new Error("PPSSPP disconnected")));
    socket.addEventListener("error", () => {});
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`could not connect to ${this.url}`)), 8000);
      socket.addEventListener("open", () => { clearTimeout(timer); resolve(); }, { once: true });
      socket.addEventListener("error", () => { clearTimeout(timer); reject(new Error(`could not connect to ${this.url}`)); }, { once: true });
    });
    await this.request("version", { name: "Open Game Access PSP probe", version: "0.1.0" });
    // ⛔ These are the categories PPSSPP 1.20.4 accepts. A NEWER protocol category must
    // not be sent unconditionally or the whole connection setup fails.
    await this.request("broadcast.config.set", { disallowed: { logger: true } });
  }

  #onMessage(data) {
    let msg; try { msg = JSON.parse(data); } catch { return; }
    if (msg.ticket != null && this.pending.has(String(msg.ticket))) {
      const entry = this.pending.get(String(msg.ticket));
      this.pending.delete(String(msg.ticket));
      clearTimeout(entry.timer);
      if (msg.event === "error") entry.reject(new Error(msg.message || "request failed"));
      else entry.resolve(msg);
    }
  }

  #failAll(err) { for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(err); } this.pending.clear(); }

  request(event, fields = {}) {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.reject(new Error("not connected"));
    const ticket = String(this.nextTicket++);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(ticket); reject(new Error(`${event} timed out`)); }, this.timeoutMs);
      this.pending.set(ticket, { resolve, reject, timer });
      this.socket.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }

  async status()   { return this.request("game.status"); }

  async read(address, size) {
    if (!Number.isInteger(address) || !Number.isInteger(size) || size < 0 || size > 0x200000) {
      throw new RangeError(`invalid memory range addr=${address} size=${size}`);
    }
    const r = await this.request("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }

  async press(button, duration = HOLD_FRAMES) {
    // ⛔ THE PARAMETER IS `button` (SINGULAR). Sending `buttons` fails with
    // "Missing 'button' parameter" — and that error is easy to read as an input
    // problem rather than a misspelled field.
    // Answers only AFTER the hold completes, so a long press does not return promptly.
    return this.request("input.buttons.press", { button, duration });
  }

  async stepping() {
    const r = await this.request("cpu.status");
    return !!r.stepping;
  }

  close() { try { this.socket?.close(); } catch {} }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
const cmd = args[0] || "status";
const flag = (name, dflt = null) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : dflt;
};
const has = (name) => args.includes(`--${name}`);

const OUT = flag("out", "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe");

/// Capture the emulator window to a PNG, in-process via ctypes.
///
/// ⛔ PRINTWINDOW NEEDS FLAG 2 (`PW_RENDERFULLCONTENT`). A plain BitBlt returns BLACK,
/// because the renderer uses a flip-model swapchain and the front buffer is not
/// readable. And do it in-process rather than spawning PowerShell per frame: a spawn
/// costs ~460 ms/frame, ctypes ~15 ms.
const CAPTURE_PY = String.raw`
import ctypes, sys
from ctypes import wintypes
u = ctypes.windll.user32
g = ctypes.windll.gdi32
# Find the emulator's main window by title class.
hwnds = []
CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    buf = ctypes.create_unicode_buffer(512)
    u.GetWindowTextW(h, buf, 512)
    if "PPSSPP" in buf.value and u.IsWindowVisible(h):
        hwnds.append((h, buf.value))
    return True
u.EnumWindows(CB(cb), 0)
if not hwnds:
    print("NO_WINDOW"); sys.exit(3)
hwnd, title = hwnds[0]
r = wintypes.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
if w <= 0 or h <= 0:
    print("BAD_RECT"); sys.exit(3)
hdc = u.GetDC(hwnd)
memdc = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, h)
g.SelectObject(memdc, bmp)
ok = u.PrintWindow(hwnd, memdc, 2)     # 2 = PW_RENDERFULLCONTENT
print(f"{'OK' if ok else 'PRINTFAIL'} {w} {h} {hwnd} {title}")
# hand the raw BGRX to python for PNG writing
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]
bi = BITMAPINFOHEADER(); bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
bi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
g.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
sys.stdout.write("B64:")
import base64
sys.stdout.write(base64.b64encode(buf.raw).decode())
g.DeleteObject(bmp); g.DeleteDC(memdc); u.ReleaseDC(hwnd, hdc)
`;

async function captureWindow(outPath) {
  const { execFileSync } = await import("node:child_process");
  let output;
  try {
    output = execFileSync("python", ["-c", CAPTURE_PY], { encoding: "utf8", timeout: 30000,
                                                           maxBuffer: 256 * 1024 * 1024 });
  } catch (err) {
    return { ok: false, reason: err.message.split("\n")[0] };
  }
  const nl = output.indexOf("\n");
  const header = output.slice(0, nl).trim();
  if (!header.startsWith("OK")) return { ok: false, reason: header || "capture failed" };
  const b64 = output.slice(nl + 1).replace("B64:", "").trim();
  const raw = Buffer.from(b64, "base64");
  const width = Number(header.split(" ")[1]), height = Number(header.split(" ")[2]);
  if (raw.length !== width * height * 4) return { ok: false, reason: `short pixels ${raw.length}` };

  // BGRX -> PNG without a dependency: write a PPM (P6) and let the caller convert,
  // or write PNG directly if pngjs is present. PPM keeps this dependency-free.
  const { writeFileSync } = await import("node:fs");
  const ppm = Buffer.alloc(15 + width * height * 3);
  const hdr = Buffer.from(`P6\n${width} ${height}\n255\n`, "latin1");
  hdr.copy(ppm, 0);
  for (let i = 0, j = hdr.length; i < width * height; i++, j += 3) {
    ppm[j] = raw[i * 4 + 2]; ppm[j + 1] = raw[i * 4 + 1]; ppm[j + 2] = raw[i * 4];
  }
  const ppmPath = outPath.replace(/\.png$/, "") + ".ppm";
  writeFileSync(ppmPath, ppm);
  return { ok: true, ppm: ppmPath, width, height, title: header.split(" ").slice(4).join(" ") };
}

/// Read all of user RAM and return it as one Buffer.
async function readAllRam(db, onProgress) {
  const parts = [];
  for (let off = 0; off < RAM_SIZE; off += CHUNK) {
    const size = Math.min(CHUNK, RAM_SIZE - off);
    const buf = await db.read(RAM_BASE + off, size);
    if (buf.length !== size) throw new Error(`short read at +0x${off.toString(16)}: got ${buf.length}, want ${size}`);
    parts.push(buf);
    if (onProgress) onProgress(off + size, RAM_SIZE);
  }
  return Buffer.concat(parts);
}

/// Find runs of printable text. Returns {offset, text} with offsets as PSP addresses.
function findStrings(buf, min = 6) {
  const out = [];
  let start = -1;
  for (let i = 0; i <= buf.length; i++) {
    const c = i < buf.length ? buf[i] : 0;
    const ok = c >= 0x20 && c <= 0x7e;
    if (ok) { if (start < 0) start = i; }
    else {
      if (start >= 0 && i - start >= min) out.push({ offset: start, text: buf.subarray(start, i).toString("latin1") });
      start = -1;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
async function cmdStatus(db) {
  const s = await db.status();
  const g = s.game || {};
  console.log("PPSSPP debugger: connected");
  console.log("  game id   :", g.id || "(none)");
  console.log("  title     :", g.title || "(none)");
  console.log("  version   :", g.version || "(none)");
  console.log("  paused    :", !!s.paused);
  console.log("  stepping  :", await db.stepping());
  return g;
}

async function cmdDump(db) {
  mkdirSync(OUT, { recursive: true });

  // ⛔ FREEZE BEFORE DUMPING A TIMED SCREEN. A live screen advances ~60x/second, so a
  // RAM dump and a screenshot taken moments apart describe DIFFERENT instants. For
  // anything that changes on its own, pause first, dump, then let the caller resume.
  const pause = has("pause");
  if (pause) { await db.request("cpu.stepping"); await new Promise(r => setTimeout(r, 250)); }

  const ram = await readAllRam(db, (done, total) => {
    if (done === total) process.stderr.write(`\r  read ${done}/${total} bytes\n`);
  });

  const stamp = flag("tag", String(Date.now()));
  const path = join(OUT, `ram-${stamp}.bin`);
  writeFileSync(path, ram);

  const g = (await db.status()).game || {};
  const metaPath = join(OUT, `meta-${stamp}.json`);
  writeFileSync(metaPath, JSON.stringify({
    game: g, base: RAM_BASE, size: RAM_SIZE, paused: pause, at: new Date().toISOString(),
  }, null, 2) + "\n");

  console.log(`  dumped ${RAM_SIZE} bytes -> ${path}`);
  console.log(`  metadata -> ${metaPath}`);

  if (!pause) { /* nothing to resume: we never paused */ }
  return path;
}

async function cmdScan(db) {
  const min = Number(flag("min", "6"));
  const grep = flag("grep", null);
  const files = args.slice(1).filter(a => !a.startsWith("--") && !a.endsWith(".bin") === false);
  // find the newest dump if none named
  let path = args.find(a => a.endsWith(".bin"));
  if (!path) {
    const dir = OUT;
    if (!existsSync(dir)) { console.error(`no dump directory at ${dir}; run: psp-probe.mjs dump`); process.exit(2); }
    const { readdirSync, statSync } = await import("node:fs");
    const bins = readdirSync(dir).filter(f => f.startsWith("ram-") && f.endsWith(".bin"))
      .map(f => ({ f, t: statSync(join(dir, f)).mtimeMs })).sort((a, b) => b.t - a.t);
    if (!bins.length) { console.error("no RAM dumps found; run: psp-probe.mjs dump"); process.exit(2); }
    path = join(dir, bins[0].f);
  }
  const buf = readFileSync(path);
  const hits = findStrings(buf, min).filter(h => !grep || h.text.includes(grep));
  console.log(`  ${path}`);
  console.log(`  ${buf.length} bytes, ${hits.length} strings (min ${min}${grep ? `, grep ${JSON.stringify(grep)}` : ""})`);
  for (const h of hits.slice(0, Number(flag("limit", "60")))) {
    console.log(`  0x${(RAM_BASE + h.offset).toString(16).toUpperCase().padStart(8, "0")}  ${JSON.stringify(h.text)}`);
  }
}

async function cmdShot() {
  // ⛔ A RAM READING WITHOUT A SCREENSHOT OF THE SAME MOMENT IS NOT EVIDENCE.
  // Every wrong conclusion in this project came from pairing a reading with a screen
  // that had moved on. The screenshot is how a finding gets checked.
  mkdirSync(OUT, { recursive: true });
  const stamp = flag("tag", String(Date.now()));
  const res = await captureWindow(join(OUT, `shot-${stamp}.png`));
  if (!res.ok) { console.error(`!! capture failed: ${res.reason}`); process.exit(1); }
  console.log(`  captured ${res.width}x${res.height} from ${JSON.stringify(res.title)}`);
  console.log(`  ${res.ppm}`);
  return res;
}

async function cmdPress(db) {
  const list = (args[1] || "").split(",").map(s => s.trim()).filter(Boolean);
  if (!list.length) { console.error("usage: psp-probe.mjs press cross[,circle...]"); process.exit(2); }
  for (const b of list) {
    await db.press([b]);
    process.stdout.write(`  pressed ${b} (${HOLD_FRAMES} frames)\n`);
    await new Promise(r => setTimeout(r, 120));
  }
}

async function main() {
  const db = new Debugger();
  await db.connect();
  try {
    switch (cmd) {
      case "status": await cmdStatus(db); break;
      case "dump":   await cmdStatus(db); await cmdDump(db); break;
      case "scan":   await cmdScan(db); break;
      case "press":  await cmdPress(db); break;
      case "shot":   await cmdShot(); break;
      default:
        console.error(`unknown command ${JSON.stringify(cmd)}; expected status | dump | scan | press | shot`);
        process.exit(2);
    }
  } finally {
    db.close();
  }
}

main().catch((err) => { console.error(`!! ${err.message}`); process.exit(1); });
