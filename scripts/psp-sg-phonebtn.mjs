// ⭐ WHICH BUTTON OPENS THE PHONE (the choice interface)?
// The screen at head=65 showed a mail notification banner, and this game's choices are
// Phone Trigger mail replies. Take a baseline screenshot, press a candidate button, take
// another, and report whether the screen entered a NEW state — comparing against the known
// idle-animation set so animation cannot be mistaken for a menu.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const { readFileSync, existsSync } = await import("node:fs");
const { execFileSync } = await import("node:child_process");
const { createHash } = await import("node:crypto");
const T = process.env.LOCALAPPDATA + "\\Temp\\psp-probe";

function shot(tag) {
  try {
    execFileSync("node", ["scripts/psp-walk.mjs", "--steps", "shot", "--tag", tag, "--plan", "wait"],
      { encoding: "utf8", timeout: 120000, cwd: process.cwd() });
    const p = `${T}\\${tag}-01-wait.ppm`;
    if (!existsSync(p)) return null;
    return createHash("md5").update(readFileSync(p)).digest("hex").slice(0, 10);
  } catch (e) { return "ERR"; }
}

const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}, ms = 10000) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("timeout")); }, ms); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "phonebtn", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);

console.log("idle animation hashes were: b66747bbe5 / dcaf701961 (alternating, no input)\n");
for (const b of ["triangle", "square", "start", "select", "ltrigger", "rtrigger", "circle", "cross"]) {
  const h0 = await u32(HEAD);
  const before = shot(`ph-${b}-a`);
  await req("input.buttons.press", { button: b, duration: 60 });
  await sleep(2500);
  const after = shot(`ph-${b}-b`);
  const h1 = await u32(HEAD);
  const inIdle = v => v === "b66747bbe5" || v === "dcaf701961";
  const novel = before && after && after !== before && !inIdle(after);
  console.log(`  ${b.padEnd(9)} shot ${before} -> ${after}   head ${h0} -> ${h1}` +
    (novel ? "   <== NEW SCREEN STATE" : ""));
}
s.close();
