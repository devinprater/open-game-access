#!/usr/bin/env node
/**
 * psp-try-buttons.mjs — press each button once and report which ones CHANGE THE SCREEN.
 *
 * ⛔ WHY: "the game ignores my input" was the single most expensive assumption in this
 * investigation, and it was never actually tested. Injection reaching sceCtrl is not the
 * same as a button doing something. This tool settles it by measurement: press, capture,
 * hash, and report which presses produced a different screen.
 *
 * It presses each button TWICE with a wait between, because several games need a second
 * press (dismiss, then advance) and a single press can look inert.
 *
 * ⛔ `duration` is FRAMES. And the request answers only AFTER the hold completes.
 *
 * Usage: psp-try-buttons.mjs [--buttons cross,circle,...] [--tag sweep]
 */
import { writeFileSync, mkdirSync } from "node:fs";
import { createHash } from "node:crypto";
import { join } from "node:path";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const args = process.argv.slice(2);
const flag = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : d; };

const OUT = flag("out", "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe");
const TAG = flag("tag", "sweep");
const HOLD = Number(flag("hold", "20"));
const WAIT = Number(flag("wait", "1800"));
const BUTTONS = (flag("buttons", "cross,circle,square,triangle,start,select,up,down,left,right,ltrigger,rtrigger")).split(",").map(s => s.trim()).filter(Boolean);

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

class Debugger {
  constructor(url = URL, timeoutMs = 25000) { this.url = url; this.timeoutMs = timeoutMs; this.pending = new Map(); this.nextTicket = 1; }
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
    await this.request("version", { name: "OGA button sweep", version: "0.1.0" });
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

const CAPTURE_PY = String.raw`
import ctypes, sys, base64
from ctypes import wintypes
u = ctypes.windll.user32; g = ctypes.windll.gdi32
hs = []
CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    b = ctypes.create_unicode_buffer(512); u.GetWindowTextW(h, b, 512)
    if "PPSSPP" in b.value and u.IsWindowVisible(h): hs.append(h)
    return True
u.EnumWindows(CB(cb), 0)
if not hs: print("NO_WINDOW"); sys.exit(3)
hwnd = hs[0]
r = wintypes.RECT(); u.GetClientRect(hwnd, ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
if w <= 0 or h <= 0: print("BAD_RECT"); sys.exit(3)
hdc = u.GetDC(hwnd); md = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, h); g.SelectObject(md, bmp)
u.PrintWindow(hwnd, md, 2)
class BIH(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD)]
bi = BIH(); bi.biSize = ctypes.sizeof(BIH); bi.biWidth = w; bi.biHeight = -h
bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
g.GetDIBits(md, bmp, 0, h, buf, ctypes.byref(bi), 0)
print(w, h); print(base64.b64encode(buf.raw).decode())
g.DeleteObject(bmp); g.DeleteDC(md); u.ReleaseDC(hwnd, hdc)
`;

async function capture() {
  const { execFileSync } = await import("node:child_process");
  const o = execFileSync("python", ["-c", CAPTURE_PY], { encoding: "utf8", timeout: 40000, maxBuffer: 256 * 1024 * 1024 });
  const nl = o.indexOf("\n");
  const [w, h] = o.slice(0, nl).trim().split(" ").map(Number);
  const raw = Buffer.from(o.slice(nl + 1).trim(), "base64");
  if (raw.length !== w * h * 4) return null;
  const body = Buffer.alloc(w * h * 3);
  let j = 0;
  for (let i = 0; i < w * h; i++) { body[j++] = raw[i * 4 + 2]; body[j++] = raw[i * 4 + 1]; body[j++] = raw[i * 4]; }
  return { hash: createHash("sha256").update(body).digest("hex").slice(0, 10), body };
}

async function main() {
  mkdirSync(OUT, { recursive: true });
  const db = new Debugger();
  await db.connect();
  const results = [];
  try {
    const base = await capture();
    console.log(`baseline screen hash: ${base?.hash}`);
    console.log("");
    for (const b of BUTTONS) {
      const before = await capture();
      await db.press(b, HOLD);
      await sleep(WAIT);
      const after = await capture();
      const changed = after && before && after.hash !== before.hash;
      console.log(`  ${b.padEnd(10)} ${changed ? "*** CHANGED ***" : "no change"}   (${before?.hash} -> ${after?.hash})`);
      if (changed) {
        const p = join(OUT, `${TAG}-${b}.ppm`);
        const header = Buffer.from(`P6\n${1706} ${after.body.length / (1706 * 3)}\n255\n`, "latin1");
        writeFileSync(p, Buffer.concat([header, after.body]));
        console.log(`      saved ${p}`);
      }
      results.push({ button: b, changed: !!changed });
    }
    const moved = results.filter(r => r.changed).map(r => r.button);
    console.log("");
    console.log(moved.length ? `buttons that changed the screen: ${moved.join(", ")}`
                             : `NO BUTTON CHANGED THE SCREEN — the game is not accepting input here.`);
  } finally { db.close(); }
}

main().catch(e => { console.error(`!! ${e.message}`); process.exit(1); });
