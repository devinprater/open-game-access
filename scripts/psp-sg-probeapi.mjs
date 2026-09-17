// Probe the debugger's press API: what parameters does it take, and does a frame counter
// confirm the emulator is actually advancing?
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 12000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "probe", version: "1" });

console.log("=== info/capability events ===");
for (const ev of ["game.status", "cpu.status", "gpu.status", "input.status", "input.buttons.status",
                  "memory.info", "info", "version", "gpu.frame", "display.status"]) {
  try { const r = await req(ev, {}); console.log(`  ${ev.padEnd(22)} -> ${JSON.stringify(r).slice(0, 120)}`); }
  catch (e) { console.log(`  ${ev.padEnd(22)} -> ${e.message}`); }
}

console.log("\n=== parameter names on input.buttons.press ===");
const variants = [
  ["button only",              { button: "start" }],
  ["button+duration",          { button: "start", duration: 10 }],
  ["button+frames",            { button: "start", frames: 10 }],
  ["buttons (plural)",         { buttons: "start", duration: 10 }],
  ["button+duration+fps",      { button: "start", duration: 10, fps: 60 }],
  ["button+duration+cycle",    { button: "start", duration: 10, cycle: true }],
  ["button+duration+realtime", { button: "start", duration: 10, realtime: true }],
];
for (const [label, args] of variants) {
  try { const r = await req("input.buttons.press", args, 8000); console.log(`  ${label.padEnd(26)} -> ok`); }
  catch (e) { console.log(`  ${label.padEnd(26)} -> ${e.message}`); }
}

console.log("\n=== is the emulator advancing frames? (ticks over 3s) ===");
const a = await req("cpu.status"); await sleep(3000); const b = await req("cpu.status");
console.log(`  ticks: ${a.ticks} -> ${b.ticks}   delta=${b.ticks - a.ticks}`);
console.log(`  pc: 0x${a.pc.toString(16).toUpperCase()} -> 0x${b.pc.toString(16).toUpperCase()}`);
s.close();
