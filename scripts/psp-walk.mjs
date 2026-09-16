#!/usr/bin/env node
/**
 * psp-walk.mjs — advance a PSP game through a button plan, capturing a screenshot and
 * optionally a RAM dump after every step.
 *
 * WHY THIS IS ITS OWN SCRIPT: the discovery loop for a reader is "put the game in a
 * state, look at the screen, read RAM at the same moment". A walk that does all three
 * in one process guarantees the screenshot and the dump describe the SAME instant —
 * doing it as separate commands lets the game move between them, and then every
 * comparison is between two different screens.
 *
 * ⛔ `duration` IS FRAMES, NOT MILLISECONDS (12 frames ~= 200 ms at 60 fps). A short
 * hold falls between the pad's per-frame polls and the game acts as if nothing was
 * pressed — which reads as "the emulator ignores input" and is not.
 *
 * Usage:
 *   psp-walk.mjs --plan "cross,cross,cross" --wait 2500 --tag menu
 *   psp-walk.mjs --plan "start:30,cross" --dump --wait 3000 --tag after-start
 *   psp-walk.mjs --plan "down,down,cross" --steps shot            (screenshot only)
 *
 * Steps run in order. Each `--tag` gets a numeric suffix per step.
 */
import { writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;
const RAM_SIZE = 0x01800000;
const CHUNK = 0x100000;
const HOLD_FRAMES = 12;

class Debugger {
  constructor(url = URL, timeoutMs = 30000) {
    this.url = url; this.timeoutMs = timeoutMs;
    this.socket = null; this.pending = new Map(); this.nextTicket = 1;
  }
  async connect() {
    const socket = new WebSocket(this.url, "debugger.ppsspp.org");
    this.socket = socket;
    socket.addEventListener("message", (e) => this.#onMessage(String(e.data)));
    socket.addEventListener("close", () => this.#failAll(new Error("disconnected")));
    socket.addEventListener("error", () => {});
    await new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error(`connect timeout ${this.url}`)), 8000);
      socket.addEventListener("open", () => { clearTimeout(t); resolve(); }, { once: true });
      socket.addEventListener("error", () => { clearTimeout(t); reject(new Error(`connect failed ${this.url}`)); }, { once: true });
    });
    await this.request("version", { name: "Open Game Access PSP walk", version: "0.1.0" });
    await this.request("broadcast.config.set", { disallowed: { logger: true } });
  }
  #onMessage(data) {
    let m; try { m = JSON.parse(data); } catch { return; }
    if (m.ticket != null && this.pending.has(String(m.ticket))) {
      const e = this.pending.get(String(m.ticket)); this.pending.delete(String(m.ticket));
      clearTimeout(e.timer);
      if (m.event === "error") e.reject(new Error(m.message || "err")); else e.resolve(m);
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
  async read(address, size) {
    const r = await this.request("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
  async press(button, duration = HOLD_FRAMES) {
    return this.request("input.buttons.press", { button, duration });
  }
  close() { try { this.socket?.close(); } catch {} }
}

// ---- args ----
const args = process.argv.slice(2);
const flag = (n, d = null) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : d; };
const has = (n) => args.includes(`--${n}`);

const OUT = flag("out", "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe");
const PLAN = (flag("plan", "") || "").split(",").map(s => s.trim()).filter(Boolean);
const WAIT = Number(flag("wait", "2200"));
const TAG = flag("tag", "step");
const DUMP = has("dump");
const SHOT = !has("nodump-screen");    // always screenshot unless explicitly off

// ⛔ CROP THE WINDOW CHROME, AND MEASURE IT RATHER THAN GUESSING. The captured client
// rect includes PPSSPP's own title strip AND its menu bar; 52 px left both in frame,
// so every "crop" still contained the words File/Emulation/Debug — which a diff would
// happily report as game-screen change. 80 px clears both at this window size.
const CHROME_TOP = Number(flag("chrome", "80"));

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const CAPTURE_PY = String.raw`
import ctypes, sys, base64
from ctypes import wintypes
u = ctypes.windll.user32; g = ctypes.windll.gdi32
hwnds = []
CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    b = ctypes.create_unicode_buffer(512); u.GetWindowTextW(h, b, 512)
    if "PPSSPP" in b.value and u.IsWindowVisible(h): hwnds.append((h, b.value))
    return True
u.EnumWindows(CB(cb), 0)
if not hwnds: print("NO_WINDOW"); sys.exit(3)
hwnd, title = hwnds[0]
r = wintypes.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
if w <= 0 or h <= 0: print("BAD_RECT"); sys.exit(3)
hdc = u.GetDC(hwnd); memdc = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, h); g.SelectObject(memdc, bmp)
ok = u.PrintWindow(hwnd, memdc, 2)
class BIH(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD)]
bi = BIH(); bi.biSize = ctypes.sizeof(BIH); bi.biWidth = w; bi.biHeight = -h
bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = 0
buf = ctypes.create_string_buffer(w*h*4)
g.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
print(f"{'OK' if ok else 'FAIL'} {w} {h}"); print(base64.b64encode(buf.raw).decode())
g.DeleteObject(bmp); g.DeleteDC(memdc); u.ReleaseDC(hwnd, hdc)
`;

async function capture(prefix) {
  const { execFileSync } = await import("node:child_process");
  const out = execFileSync("python", ["-c", CAPTURE_PY], { encoding: "utf8", timeout: 40000, maxBuffer: 256 * 1024 * 1024 });
  const nl = out.indexOf("\n");
  const head = out.slice(0, nl).trim();
  if (!head.startsWith("OK")) return null;
  const [w, h] = head.split(" ").slice(1).map(Number);
  const raw = Buffer.from(out.slice(nl + 1).trim(), "base64");
  if (raw.length !== w * h * 4) return null;
  const ch = h - CHROME_TOP;                        // drop the menu bar
  // ⛔ BUILD THE PPM BY CONCATENATION, NOT BY PRE-COMPUTING A HEADER OFFSET. The
  // previous version allocated 15 bytes for a header whose real length varies with the
  // digit count of w and ch ("P6\n960 492\n255\n" is 15 bytes, "P6\n960 544\n255\n" is
  // also 15 — but the indexOf chain below then double-counted and the file came out
  // TRUNCATED). PIL reports that as "image file is truncated", which reads like a
  // capture problem rather than an arithmetic bug in the writer.
  const header = Buffer.from(`P6\n${w} ${ch}\n255\n`, "latin1");
  const body = Buffer.alloc(w * ch * 3);
  let j = 0;
  for (let y = CHROME_TOP; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      body[j++] = raw[i + 2]; body[j++] = raw[i + 1]; body[j++] = raw[i];
    }
  }
  if (j !== body.length) throw new Error(`ppm body short: wrote ${j}, want ${body.length}`);
  const p = join(OUT, `${prefix}.ppm`);
  writeFileSync(p, Buffer.concat([header, body]));
  return { ppm: p, w, h: ch };
}

async function main() {
  mkdirSync(OUT, { recursive: true });
  const db = new Debugger();
  await db.connect();
  const meta = { plan: PLAN, wait: WAIT, steps: [] };
  try {
    await sleep(600);
    // ⛔ AN EMPTY PLAN STILL CAPTURES ONCE. Without this, "--plan ''" (the natural way
    // to say "just screenshot what is on screen now") wrote no image at all and exited
    // clean — which reads as a capture failure rather than a no-op.
    if (PLAN.length === 0) {
      const s = await capture(`${TAG}-00-now`);
      process.stdout.write(`  (no plan) now -> ${s ? s.ppm.split(/[\\/]/).pop() : "CAPTURE FAILED"}\n`);
      meta.steps.push({ i: 0, button: null, shot: s ? s.ppm : null });
    }
    for (let i = 0; i < PLAN.length; i++) {
      const spec = PLAN[i];
      const [btn, durStr] = spec.split(":");
      const dur = durStr ? Number(durStr) : HOLD_FRAMES;
      // ⛔ A STEP OF `wait` PRESSES NOTHING and only waits — needed because the
      // interesting moment is often the INSTANT after a press, before a screen has had
      // time to settle or a demo loop has taken over. Without this the shortest
      // observable delay is one full press-plus-wait, which is too late to see a menu
      // that appears and then auto-advances.
      if (btn === "wait") { await sleep(dur); }
      else { await db.press(btn, dur); await sleep(WAIT); }
      const name = `${TAG}-${String(i + 1).padStart(2, "0")}-${btn}`;
      if (SHOT) {
        const s = await capture(name);
        process.stdout.write(`  ${btn.padEnd(8)} -> ${s ? s.ppm.split(/[\\/]/).pop() : "CAPTURE FAILED"}\n`);
        meta.steps.push({ i: i + 1, button: btn, duration: dur, shot: s ? s.ppm : null });
      }
      if (DUMP) {
        const parts = [];
        for (let off = 0; off < RAM_SIZE; off += CHUNK) {
          parts.push(await db.read(RAM_BASE + off, Math.min(CHUNK, RAM_SIZE - off)));
        }
        const bin = join(OUT, `${name}.bin`);
        writeFileSync(bin, Buffer.concat(parts));
        process.stdout.write(`  ${btn.padEnd(8)} -> ${bin.split(/[\\/]/).pop()} (24 MiB)\n`);
        meta.steps[meta.steps.length - 1].ram = bin;
      }
    }
    writeFileSync(join(OUT, `${TAG}-walk.json`), JSON.stringify(meta, null, 2) + "\n");
    console.log(`  walk metadata -> ${join(OUT, TAG + "-walk.json")}`);
  } finally { db.close(); }
}

main().catch(e => { console.error(`!! ${e.message}`); process.exit(1); });
