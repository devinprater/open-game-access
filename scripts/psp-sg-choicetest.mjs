// ⭐ THE CHOICE TEST — we are sitting on a genuine Phone Trigger state.
// Screen: date 8/5 (THU), mail icons in the HUD, no dialogue box, story stopped at head=65,
// CPU running, no button advances the story.
// Per the game's own code (FUN_0008d7f0) the phone has an OPEN/CLOSE state at
// 0x0897B1E7 / 0x0897B1E8. This presses TRIANGLE (the documented "take out phone" button)
// and reports whether that state changes, plus whether the story resumes.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14;
const PH_OPEN = 0x0897B1E7, PH_CLOSE = 0x0897B1E8, CFG = 0x089B5E34;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 10000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "choicetest", version: "1" });
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");
const u32 = async a => (await rd(a, 4)).readUInt32LE(0);
const u8 = async a => (await rd(a, 1))[0];

console.log("=== state at the suspected CHOICE ===");
console.log(`  head        = ${await u32(HEAD)}`);
console.log(`  phone open  = ${await u8(PH_OPEN)}   (0x0897B1E7)`);
console.log(`  phone close = ${await u8(PH_CLOSE)}  (0x0897B1E8)`);
console.log(`  cpu         = ${JSON.stringify(await req("cpu.status"))}`);

console.log("\n=== press TRIANGLE (take out phone) and watch the phone state + head ===");
for (let i = 1; i <= 6; i++) {
  await req("input.buttons.press", { button: "triangle", duration: 40 });
  await sleep(1600);
  const o = await u8(PH_OPEN), c = await u8(PH_CLOSE), h = await u32(HEAD);
  console.log(`  press ${i}: phone_open=${o} phone_close=${c} head=${h}`);
}

console.log("\n=== compare: press CROSS 4x (should NOT touch the phone) ===");
for (let i = 1; i <= 4; i++) {
  await req("input.buttons.press", { button: "cross", duration: 40 });
  await sleep(1400);
  console.log(`  press ${i}: phone_open=${await u8(PH_OPEN)} phone_close=${await u8(PH_CLOSE)} head=${await u32(HEAD)}`);
}

console.log("\n=== press UP and DOWN, watching head (a choice menu would move a selection) ===");
for (const b of ["up", "down", "up", "down"]) {
  const before = await u32(HEAD);
  await req("input.buttons.press", { button: b, duration: 40 });
  await sleep(1400);
  console.log(`  ${b.padEnd(5)} head ${before} -> ${await u32(HEAD)}`);
}
s.close();
