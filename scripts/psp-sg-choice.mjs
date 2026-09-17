// ⭐ CHOICE DETECTOR.
// Steins;Gate's choices are the "Phone Trigger": a mail arrives and the player replies, which
// sets the route. Signature: the story STOPS advancing (write head stalls) while the CPU keeps
// running, and the screen shows a mail/phone state.
// This drives dialogue with a chosen button, and when the head stalls for several rounds it
// captures the screen and reports — that is the moment to check for a choice.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { writeFileSync, mkdirSync } = await import("node:fs");
const OUT = process.env.LOCALAPPDATA + "\\Temp\\psp-probe\\choice";
mkdirSync(OUT, { recursive: true });

const ADV = process.argv[2] || "cross";
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 10000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "choice", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const dec = b => { const c = b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)).toString("latin1");
  return c.replace(/\x81[\x67\x68]/g, "").replace(/\x81\x43/g, ", ").replace(/\x81[\x65\x66]/g, "'")
          .replace(/\x81\xf4/g, "").replace(/\s+/g, " ").trim(); };

console.log(`advancing with "${ADV}" — watching for a stall (a choice blocks the story)`);
let last = await u32(HEAD);
let stall = 0;
for (let round = 1; round <= 200; round++) {
  await req("input.buttons.press", { button: ADV, duration: 30 });
  let moved = false;
  for (let k = 0; k < 8; k++) {
    await sleep(200);
    const wh = await u32(HEAD);
    if (wh !== last) {
      const lb = await u32(LB);
      for (let i = wh - last; i >= 1; i--) {
        const a = lb + ((wh - (i - 1)) - 1) * 0xAC;
        const nm = dec(await req("memory.read", { address: a + 0x1c, size: 0x28, replacements: false }).then(r => Buffer.from(r.base64, "base64")));
        const tx = dec(await req("memory.read", { address: a + 0x48, size: 0x60, replacements: false }).then(r => Buffer.from(r.base64, "base64")));
        if (tx) console.log(`  [${String(wh).padStart(4)}] ${nm ? nm + ": " : ""}${tx}`);
      }
      last = wh; moved = true; stall = 0; break;
    }
  }
  if (!moved) {
    stall++;
    if (stall === 5) {
      console.log(`\n*** STALLED 5 rounds at head=${last} — possible CHOICE ***`);
      const st = await req("cpu.status");
      console.log(`    cpu stepping=${st.stepping} paused=${st.paused} ticks=${st.ticks}`);
      // capture the screen so the state is inspectable
      try {
        const { execFileSync } = await import("node:child_process");
        execFileSync("node", ["scripts/psp-walk.mjs", "--steps", "shot", "--tag", "choice", "--plan", "wait"], { encoding: "utf8", timeout: 120000 });
        console.log("    captured choice-01-wait.ppm");
      } catch (e) { console.log("    capture failed:", e.message); }
      // try the documented phone button and see if the head resumes
      for (const b of ["triangle", "square", "start", "circle", "cross", "right", "down", "up", "left"]) {
        const before = await u32(HEAD);
        await req("input.buttons.press", { button: b, duration: 40 });
        await sleep(1500);
        const after = await u32(HEAD);
        if (after !== before) { console.log(`    ${b} RESUMED the story: ${before} -> ${after}`); break; }
      }
      stall = 0;
      if ((await u32(HEAD)) !== last) { last = await u32(HEAD); }
    }
  }
}
console.log("\nfinal head =", await u32(HEAD));
s.close();
