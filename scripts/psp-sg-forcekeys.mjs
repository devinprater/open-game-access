// ⛔ FORCE FOCUS + REAL KEYSTROKES.
// PPSSPP is not foreground (a 0x0 'Windows Security' overlay holds it), and SetForegroundWindow
// returned 0. The standard way past that is AttachThreadInput: attach our thread to the
// current foreground thread, then SetForegroundWindow succeeds.
// Then send REAL keystrokes. PPSSPP maps Cross=key 54 ('6'), Circle=52 ('4'),
// Triangle=47 ('/'), Square=29 (Ctrl), Start=62.
// Watch the write head: if real keystrokes move it while debugger injection did not, the
// debugger's input path is the broken link.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { execFileSync } = await import("node:child_process");

const PS = `
Add-Type @'
using System;using System.Runtime.InteropServices;
public class F {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
}
'@
$p = Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue
if (-not $p) { Write-Output "NOPROC"; exit }
$h = $p.MainWindowHandle
if ($h -eq 0) { Write-Output "NOWINDOW"; exit }
$fg = [F]::GetForegroundWindow()
$t1 = [F]::GetWindowThreadProcessId($fg, [IntPtr]::Zero)
$t2 = [F]::GetCurrentThreadId()
[void][F]::AttachThreadInput($t2, $t1, $true)
[void][F]::ShowWindow($h, 9)
[void][F]::BringWindowToTop($h)
$ok = [F]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400
[void][F]::AttachThreadInput($t2, $t1, $false)
$fg2 = [F]::GetForegroundWindow()
$sb = New-Object System.Text.StringBuilder 300
[void][F]::GetWindowText($fg2, $sb, 300)
Write-Output "set=$ok foreground=$($sb.ToString())"

# now send real keystrokes: Cross = vk 0x36 ('6')
1..8 | ForEach-Object {
  [F]::keybd_event(0x36, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 90
  [F]::keybd_event(0x36, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 1400
}
Write-Output "KEYS_SENT"
`;
console.log("forcing focus + sending 8 real Cross keypresses ...");
try {
  const out = execFileSync("powershell.exe", ["-NoProfile", "-Command", PS], { encoding: "utf8", timeout: 90000 });
  console.log(out.trim().split("\n").map(l => "  " + l.trim()).join("\n"));
} catch (e) { console.log("  powershell failed:", e.message); }

// read the head afterwards - no socket needed if we just want the value, but use it anyway
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "fk", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
console.log("\nwrite head after real keystrokes:", await u32(HEAD));
s.close();
