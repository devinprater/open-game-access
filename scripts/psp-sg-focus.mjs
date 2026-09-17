// ⛔ FOCUS TEST. PPSSPP is not the foreground window (Windows Security / Cua overlay have
// it). If the debugger's input injection depends on the target window being focused, every
// press is being discarded — which would explain a running CPU, a static log, and ZERO RAM
// change from presses.
// Non-destructive: bring PPSSPP to the foreground, press, and read the write head.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { execFileSync } = await import("node:child_process");

function focusPPSSPP() {
  execFileSync("python", ["-c", `
import ctypes, time
from ctypes import wintypes
u = ctypes.windll.user32
CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
found = []
def cb(h, l):
    t = ctypes.create_unicode_buffer(512); u.GetWindowTextW(h, t, 512)
    if 'PPSSPP' in t.value and u.IsWindowVisible(h): found.append(h)
    return True
u.EnumWindows(CB(cb), 0)
if not found:
    print("NOTFOUND"); raise SystemExit
h = found[0]
u.ShowWindow(h, 9)                 # SW_RESTORE
time.sleep(0.3)
u.SetForegroundWindow(h)
time.sleep(0.5)
fg = u.GetForegroundWindow()
t = ctypes.create_unicode_buffer(512); u.GetWindowTextW(fg, t, 512)
print("FOREGROUND:" + t.value[:50])
`], { encoding: "utf8", timeout: 20000 });
}

const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "focus", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("head before focusing:", await u32(HEAD));
console.log("focusing PPSSPP ...");
try { console.log(" ", focusPPSSPP().toString().trim()); } catch (e) { console.log("  focus failed:", e.message); }

console.log("\npressing cross 6x WITH focus, watching head:");
for (let i = 0; i < 6; i++) {
  const b = await u32(HEAD);
  await req("input.buttons.press", { button: "cross", duration: 40 });
  await sleep(1600);
  const a = await u32(HEAD);
  console.log(`  press ${i + 1}: ${b} -> ${a}${a !== b ? "   <== MOVED" : ""}`);
}
console.log("\ntrying triangle (the documented 'take out phone' button):");
for (let i = 0; i < 4; i++) {
  const b = await u32(HEAD);
  await req("input.buttons.press", { button: "triangle", duration: 40 });
  await sleep(1600);
  const a = await u32(HEAD);
  console.log(`  press ${i + 1}: ${b} -> ${a}${a !== b ? "   <== MOVED" : ""}`);
}
s.close();
