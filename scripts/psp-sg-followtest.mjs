// same loop as psp-sg-live.mjs, but exits CLEANLY so stdout flushes.
const URL = "ws://127.0.0.1:12345/debugger", HEAD = 0x089797E8, LB = 0x08978F14, ST = 0xAC;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const s = new WebSocket(URL, "debugger.ppsspp.org");
let n = 1; const pend = new Map();
s.addEventListener("message", e => { let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.ticket && pend.has(m.ticket)) { const p = pend.get(m.ticket); pend.delete(m.ticket); m.event === "error" ? p.rej(new Error(m.message)) : p.res(m); } });
await new Promise((res, rej) => { s.addEventListener("open", res, { once: true }); s.addEventListener("error", rej, { once: true }); });
const req = (e, f = {}) => new Promise((res, rej) => { const t = String(n++); pend.set(t, { res, rej });
  setTimeout(() => { pend.delete(t); rej(new Error("to")); }, 8000); s.send(JSON.stringify({ event: e, ticket: t, ...f })); });
await req("version", { name: "ft", version: "1" });
const u32 = async a => Buffer.from((await req("memory.read", { address: a, size: 4, replacements: false })).base64, "base64").readUInt32LE(0);
const rd = async (a, z) => Buffer.from((await req("memory.read", { address: a, size: z, replacements: false })).base64, "base64");
const dec = b => { const c = b.subarray(0, b.indexOf(0) < 0 ? b.length : b.indexOf(0)).toString("latin1");
  return c.replace(/\x81\x67/g, "").replace(/\x81\x68/g, "").replace(/\x81\x43/g, ", ")
          .replace(/\x81\x65/g, "'").replace(/\x81\x66/g, "'").replace(/\x81\xf4/g, "")
          .split(/\s+/).filter(Boolean).join(" ").trim(); };
let last = await u32(HEAD);
console.log("start head =", last);
const t0 = Date.now();
while (Date.now() - t0 < 50000) {
  await sleep(120);
  const wh = await u32(HEAD);
  if (wh === last) continue;
  console.log(`[${((Date.now() - t0) / 1000).toFixed(1)}s] HEAD ${last} -> ${wh}`);
  const lb = await u32(LB);
  for (let i = wh - last; i >= 1; i--) {
    const k = (wh - (i - 1)) - 1, a = lb + k * ST;
    const nm = dec(await rd(a + 0x1c, 0x28)), tx = dec(await rd(a + 0x48, 0x60));
    console.log(`    NEW: ${nm ? nm + ": " : ""}${tx}`);
  }
  last = wh;
}
console.log("clean exit");
s.close();
