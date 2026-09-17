// hunt for a CHOICE state: run circle presses, and each round check whether the message
// log stopped advancing (a visual novel blocks on a choice) while the screen CHANGED.
// On a choice, the log stops growing and the game waits — that is the signature.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "choicehunt", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const u16 = async a => Buffer.from((await req("memory.read", { address: a, size: 2, replacements: false })).base64, "base64").readUInt16LE(0);
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");

// candidate "a choice is open" signals to probe — a state word and a counter.
// We do not know the address yet, so also snapshot a few plausible globals.
const PROBE = [0x089797E8, 0x08978F14, 0x089B5E34, 0x089B6185, 0x089B6173];

let last = await u32(HEAD);
console.log("start head =", last);
let stall = 0;
for (let round = 1; round <= 120; round++) {
  await req("input.buttons.press", { button: "circle", duration: 20 });
  // watch closely right after the press
  let moved = false;
  for (let k = 0; k < 10; k++) {
    await sleep(150);
    const wh = await u32(HEAD);
    if (wh !== last) { console.log(`  round ${round}: head ${last} -> ${wh}`); last = wh; moved = true; break; }
  }
  if (!moved) stall++; else stall = 0;
  // a choice blocks advance for several rounds
  if (stall >= 4) {
    console.log(`\n*** STALLED for ${stall} rounds at head=${last} — possible CHOICE ***`);
    for (const a of PROBE) {
      const v = await u32(a);
      console.log(`    u32 0x${a.toString(16).toUpperCase()} = ${v}  (0x${v.toString(16)})`);
    }
    // try pressing UP/DOWN to see if a cursor moves anywhere interesting
    console.log("    probing for a cursor: press down, re-read");
    const before = {};
    for (const a of PROBE) before[a] = await u32(a);
    await req("input.buttons.press", { button: "down", duration: 20 });
    await sleep(1200);
    for (const a of PROBE) { const v = await u32(a); if (v !== before[a]) console.log(`    CHANGED on down: 0x${a.toString(16).toUpperCase()}  ${before[a]} -> ${v}`); }
    break;
  }
}
console.log("\nfinal head =", last);
s.close();
