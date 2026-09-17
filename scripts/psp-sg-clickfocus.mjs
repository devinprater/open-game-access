// ⛔ RESTORE FOCUS BY CLICKING, THEN RE-TEST INPUT.
// SetForegroundWindow and AttachThreadInput both failed against a 0x0 'Windows Security'
// overlay. A real MOUSE CLICK inside the target window is the one path that normally still
// grants focus. PPSSPP maps a click on the game view to a touch/advance, so a click may
// also advance the story by itself.
//
// Note: PPSSPP's PauseOnLostFocus=False, so focus should not be the cause — but this
// settles it either way, and if input lands after clicking, focus WAS the problem.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { execFileSync } = await import("node:child_process");

const CLICK = `
Add-Type @'
using System;using System.Runtime.InteropServices;
public class M {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
  public static IntPtr Find() {
    IntPtr res = IntPtr.Zero;
    EnumWindows((h, p) => {
      var sb = new System.Text.StringBuilder(400);
      GetWindowText(h, sb, 400);
      if (sb.ToString().Contains("PPSSPP") && IsWindowVisible(h)) { res = h; return false; }
      return true;
    }, IntPtr.Zero);
    return res;
  }
}
'@
$h = [M]::Find()
if ($h -eq [IntPtr]::Zero) { Write-Output "NOWINDOW"; exit }
$r = New-Object M+RECT
[void][M]::GetClientRect($h, [ref]$r)
$pt = New-Object M+POINT
$pt.X = [int](($r.R - $r.L) / 2); $pt.Y = [int](($r.B - $r.T) / 2)
[void][M]::ClientToScreen($h, [ref]$pt)
Write-Output "clicking client center at $($pt.X),$($pt.Y)"
[void][M]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 300
[void][M]::SetCursorPos($pt.X, $pt.Y)
Start-Sleep -Milliseconds 200
[M]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 80
[M]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds 900
$fg = [M]::GetForegroundWindow()
$sb = New-Object System.Text.StringBuilder 300
[void][M]::GetWindowText($fg, $sb, 300)
Write-Output "foreground after click: $($sb.ToString())"
`;
console.log("clicking PPSSPP client centre ...");
try {
  console.log(execFileSync("powershell.exe", ["-NoProfile", "-Command", CLICK], { encoding: "utf8", timeout: 60000 })
    .trim().split("\n").map(l => "  " + l.trim()).join("\n"));
} catch (e) { console.log("  failed:", e.message); }

const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "click", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("\nhead now:", await u32(HEAD));
console.log("re-testing DEBUGGER injected input (cross x4, longer hold):");
for (let i = 0; i < 4; i++) {
  const b = await u32(HEAD);
  await req("input.buttons.press", { button: "cross", duration: 200 });
  await sleep(1800);
  const a = await u32(HEAD);
  console.log(`  press ${i + 1}: ${b} -> ${a}${a !== b ? "   <== MOVED" : ""}`);
}
s.close();
