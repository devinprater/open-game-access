#!/usr/bin/env node
/**
 * psp-burst.mjs — press a button, then capture a rapid series of frames.
 *
 * ⛔ WHY A BURST IS NECESSARY. A single screenshot N seconds after a press can only
 * see a screen that STAYED. If the game shows a menu that auto-advances, blinks, or
 * times out — and a title screen's "Press START" prompt is exactly the blinking kind —
 * then one late capture will show the attract and you will conclude the button did
 * nothing. A burst makes the transition itself visible instead of inferring it.
 *
 * It reports a hash per frame so a CHANGE is a fact, and writes a PPM for every frame
 * so the change can be looked at rather than assumed.
 *
 * Usage:
 *   psp-burst.mjs [--button start] [--count 16] [--every 250] [--tag t] [--hold 25]
 */
import { writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { createHash } from "node:crypto";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";

const args = process.argv.slice(2);
const flag = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : d; };

const BUTTON = flag("button", "start");
const COUNT = Number(flag("count", "16"));
const EVERY = Number(flag("every", "250"));
const HOLD = Number(flag("hold", "25"));
const TAG = flag("tag", "burst");
const OUT = flag("out", "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe");

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

class Debugger {
  constructor(url = URL, timeoutMs = 20000) {
    this.url = url; this.timeoutMs = timeoutMs; this.pending = new Map(); this.nextTicket = 1;
  }
  async connect() {
    const s = new WebSocket(this.url, "debugger.ppsspp.org"); this.socket = s;
    s.addEventListener("message", (e) => this.#on(String(e.data)));
    s.addEventListener("close", () => this.#fail(new Error("disconnected")));
    s.addEventListener("error", () => {});
    await new Promise((res, rej) => {
      const t = setTimeout(() => rej(new Error("connect timeout")), 8000);
      s.addEventListener("open", () => { clearTimeout(t); res(); }, { once: true });
      s.addEventListener("error", () => { clearTimeout(t); rej(new Error("connect failed")); }, { once: true });
    });
    await this.request("version", { name: "OGA burst", version: "0.1.0" });
  }
  #on(d) { let m; try { m = JSON.parse(d); } catch { return; }
    if (m.ticket != null && this.pending.has(String(m.ticket))) {
      const e = this.pending.get(String(m.ticket)); this.pending.delete(String(m.ticket));
      clearTimeout(e.timer); m.event === "error" ? e.reject(new Error(m.message)) : e.resolve(m);
    } }
  #fail(err) { for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(err); } this.pending.clear(); }
  request(event, fields = {}) {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.reject(new Error("not connected"));
    const tk = String(this.nextTicket++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.pending.delete(tk); rej(new Error(`${event} timed out`)); }, this.timeoutMs);
      this.pending.set(tk, { resolve: res, reject: rej, timer });
      this.socket.send(JSON.stringify({ event, ticket: tk, ...fields }));
    });
  }
  press(b, d) { return this.request("input.buttons.press", { button: b, duration: d }); }
  close() { try { this.socket?.close(); } catch {} }
}

// In-process window capture. PW_RENDERFULLCONTENT (2) is required: a plain BitBlt
// returns black on a flip-model swapchain.
const CAPTURE_PY = String.raw`
import ctypes, sys, base64
from ctypes import wintypes
u = ctypes.windll.user32; g = ctypes.windll.gdi32
hs = []
CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    b = ctypes.create_unicode_buffer(512); u.GetWindowTextW(h, b, 512)
    if "PPSSPP" in b.value and u.IsWindowVisible(h): hs.append((h, b.value))
    return True
u.EnumWindows(CB(cb), 0)
if not hs: print("NO_WINDOW"); sys.exit(3)
hwnd, title = hs[0]
r = wintypes.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
if w <= 0 or h <= 0: print("BAD_RECT"); sys.exit(3)
hdc = u.GetDC(hwnd); md = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, h); g.SelectObject(md, bmp)
ok = u.PrintWindow(hwnd, md, 2)
class BIH(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD)]
bi = BIH(); bi.biSize = ctypes.sizeof(BIH); bi.biWidth = w; bi.biHeight = -h
bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = 0
buf = ctypes.create_string_buffer(w*h*4)
g.GetDIBits(md, bmp, 0, h, buf, ctypes.byref(bi), 0)
print(f"{'OK' if ok else 'FAIL'} {w} {h}"); print(base64.b64encode(buf.raw).decode())
g.DeleteObject(bmp); g.DeleteDC(md); u.ReleaseDC(hwnd, hdc)
`;

async function capture(prefix) {
  const { execFileSync } = await import("node:child_process");
  const out = execFileSync("python", ["-c", CAPTURE_PY], { encoding: "utf8", timeout: 40000, maxBuffer: 256 * 1024 * 1024 });
  const nl = out.indexOf("\n");
  if (!out.slice(0, nl).startsWith("OK")) return null;
  const [w, h] = out.slice(0, nl).trim().split(" ").slice(1).map(Number);
  const raw = Buffer.from(out.slice(nl + 1).trim(), "base64");
  if (raw.length !== w * h * 4) return null;
  const header = Buffer.from(`P6\n${w} ${h}\n255\n`, "latin1");
  const body = Buffer.alloc(w * h * 3);
  let j = 0;
  for (let i = 0; i < w * h; i++) { body[j++] = raw[i * 4 + 2]; body[j++] = raw[i * 4 + 1]; body[j++] = raw[i * 4]; }
  const p = join(OUT, `${prefix}.ppm`);
  writeFileSync(p, Buffer.concat([header, body]));
  return { path: p, hash: createHash("sha256").update(body).digest("hex").slice(0, 12), w, h };
}

async function main() {
  mkdirSync(OUT, { recursive: true });
  const db = new Debugger();
  await db.connect();
  const log = [];
  try {
    const before = await capture(`${TAG}-00-before`);
    console.log(`  before      ${before?.hash} ${before?.w}x${before?.h}`);
    log.push({ step: "before", hash: before?.hash });

    console.log(`  pressing ${BUTTON} for ${HOLD} frames...`);
    await db.press(BUTTON, HOLD);      // answers AFTER the hold completes

    for (let i = 1; i <= COUNT; i++) {
      const c = await capture(`${TAG}-${String(i).padStart(2, "0")}`);
      const changed = c && c.hash !== log[log.length - 1].hash;
      console.log(`  +${String(i * EVERY).padStart(5)}ms  ${c?.hash}  ${changed ? "CHANGED" : "same"}`);
      log.push({ step: i, ms: i * EVERY, hash: c?.hash, changed });
      await sleep(EVERY);
    }
    const n = log.filter(s => s.changed).length;
    console.log(`\n  ${n} of ${COUNT} frames differ from the previous frame.`);
    console.log(`  files: ${TAG}-NN.ppm in ${OUT}`);
  } finally { db.close(); }
}

main().catch(e => { console.error(`!! ${e.message}`); process.exit(1); });
