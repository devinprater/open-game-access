// ⛔ PPSSPP's debugger allows only ONE client at a time. A separate presser process
// therefore cannot coexist with a follower watching the same socket. This proves it, and
// then shows the correct design: press AND poll on the SAME connection.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "oneclient", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");
const dec = b => { const c = b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)).toString("latin1");
  return c.replace(/\x81[\x67\x68]/g, "").replace(/\x81\x43/g, ", ").replace(/\x81\x65/g, "'")
          .replace(/\x81\x66/g, "'").replace(/\x81\xf4/g, "").split(/\s+/).filter(Boolean).join(" ").trim(); };

// 1) prove a SECOND client cannot connect
try {
  const s2 = new WebSocket(URL, "debugger.ppsspp.org");
  await new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error("second client: no answer")), 4000);
    s2.addEventListener("open", () => { clearTimeout(t); res(); });
    s2.addEventListener("error", () => { clearTimeout(t); rej(new Error("second client: refused")); });
  });
  console.log("second client CONNECTED (contradicts the one-client rule)");
  s2.close();
} catch (e) { console.log("second client:", e.message, "  <== one-client rule confirmed"); }

// 2) the correct design: press and poll on ONE connection
let last = await u32(HEAD);
console.log("\nfollowing with presses on the same socket. head =", last, "\n");
for (let round = 0; round < 14; round++) {
  await req("input.buttons.press", { button: "circle", duration: 20 });
  for (let k = 0; k < 16; k++) {          // watch closely right after the press
    await sleep(150);
    const wh = await u32(HEAD);
    if (wh === last) continue;
    const lb = await u32(LB);
    for (let i = wh - last; i >= 1; i--) {
      const a = lb + ((wh - (i - 1)) - 1) * ST;
      const nm = dec(await rd(a + 0x1c, 0x28)), tx = dec(await rd(a + 0x48, 0x60));
      console.log(`  ${nm || "(narration)"}: ${tx}`);
    }
    last = wh;
  }
}
console.log("\nfinal head =", last);
s.close();
