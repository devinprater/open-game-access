// ⛔ REAL-KEYSTROKE TEST — the decisive comparison.
// PPSSPP maps keyboard keys to PSP buttons: Cross=1-54, Circle=1-52, Triangle=1-47,
// Square=1-29, Start=1-62. If the DEBUGGER's input.buttons.press does nothing but a real
// Windows keystroke DOES move the write head, then the debugger's injection is the broken
// link (not the game, not the ROM) and the harness must drive the pad by keystrokes.
// Watch the write head — a value that only moves when the game actually reacts.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { execFileSync } = await import("node:child_process");

const VK = { cross: 0x36, circle: 0x34, triangle: 0x2F, square: 0x1D, start: 0x3E, select: 0x32 };
// 0x36 = '6' (1-54) 0x34 = '4' (1-52) 0x2F = 'V'? -> use SendInput with the scancode instead.

function sendKey(vk, ms = 120) {
  execFileSync("powershell.exe", ["-NoProfile", "-Command", `
Add-Type @'
using System;using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
}
'@
[K]::keybd_event(${vk}, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds ${ms}
[K]::keybd_event(${vk}, 0, 2, [UIntPtr]::Zero)
`], { encoding: "utf8", timeout: 20000 });
}

const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "keys", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

const base = await u32(HEAD);
console.log("head before:", base, "\n");

// focus PPSSPP first with AppActivate (which reported True)
try {
  execFileSync("powershell.exe", ["-NoProfile", "-Command",
    `$p=Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue; if($p){$sh=New-Object -ComObject WScript.Shell; $null=$sh.AppActivate($p.Id)}`],
    { encoding: "utf8", timeout: 15000 });
} catch {}
await sleep(800);

console.log("=== REAL KEYSTROKES (cross = VK 0x36) x6, watching head ===");
for (let i = 0; i < 6; i++) {
  const b = await u32(HEAD);
  try { sendKey(VK.cross, 120); } catch (e) { console.log("  send failed:", e.message); }
  await sleep(1600);
  const a = await u32(HEAD);
  console.log(`  key ${i + 1}: ${b} -> ${a}${a !== b ? "   <== MOVED" : ""}`);
}

console.log("\n=== REAL KEYSTROKES (triangle/circle) x4 ===");
for (const [name, vk] of [["triangle", VK.triangle], ["circle", VK.circle]]) {
  for (let i = 0; i < 4; i++) {
    const b = await u32(HEAD);
    try { sendKey(vk, 120); } catch {}
    await sleep(1400);
    const a = await u32(HEAD);
    console.log(`  ${name} ${i + 1}: ${b} -> ${a}${a !== b ? "   <== MOVED" : ""}`);
  }
}
s.close();
