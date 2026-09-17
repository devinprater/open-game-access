// ⛔ ROOT CAUSE: the emulator is stuck in STEPPING mode (cpu.status "stepping": true), so the
// CPU does not advance (ticks delta = 0). Everything that looked like "input is dead" — a
// pinned write head, zero RAM churn from presses, an unresponsive title screen — is caused
// by the CPU being halted, NOT by the harness and NOT by the game.
//
// I set this myself: an earlier probe called `cpu.stepping` and the request timed out, but
// the setting still applied. This script finds the right way to RESUME.
const URL = "ws://127.0.0.1:12345/debugger";
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 6000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });

console.log("before:", JSON.stringify(await req("cpu.status")));

const attempts = [
  ["cpu.stepping {stepping:false}", "cpu.stepping", { stepping: false }],
  ["cpu.stepping {stepping:0}",     "cpu.stepping", { stepping: 0 }],
  ["cpu.resume",                    "cpu.resume",   {}],
  ["cpu.run",                       "cpu.run",      {}],
  ["cpu.continue",                  "cpu.continue", {}],
  ["cpu.pause {paused:false}",      "cpu.pause",    { paused: false }],
];
for (const [label, ev, args] of attempts) {
  try { const r = await req(ev, args); console.log(`  ${label.padEnd(30)} -> ${JSON.stringify(r).slice(0, 100)}`); }
  catch (e) { console.log(`  ${label.padEnd(30)} -> ${e.message}`); }
  await sleep(400);
  const st = await req("cpu.status").catch(() => null);
  if (st) console.log(`      status: stepping=${st.stepping} paused=${st.paused}`);
  if (st && !st.stepping) { console.log("      *** RESUMED ***"); break; }
}

console.log("\nverify the CPU is really advancing (ticks over 3s):");
const a = await req("cpu.status"); await sleep(3000); const b = await req("cpu.status");
console.log(`  ticks ${a.ticks} -> ${b.ticks}  delta=${b.ticks - a.ticks}`);
console.log(`  pc    0x${a.pc.toString(16).toUpperCase()} -> 0x${b.pc.toString(16).toUpperCase()}`);
s.close();
