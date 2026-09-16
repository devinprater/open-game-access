#!/usr/bin/env node
/**
 * psp-watch.mjs — poll specific RAM addresses LIVE while you press buttons.
 *
 * ⛔ WHY THIS EXISTS, AND WHY DIFFS ARE NOT ENOUGH.
 * Every cursor hunt in this investigation compared two snapshots: press, wait, dump, diff.
 * That method has a fatal blind spot — it cannot tell "changed when the PAGE changed" from
 * "changed when ANY BUTTON was pressed". It produced a candidate that looked perfect
 * (monotonic [3,4,5,5] across a scroll) and was actually counting resource allocations.
 *
 * Live polling fixes that by recording WHEN a value moves relative to WHAT was pressed:
 *
 *     t=0.0s  press right
 *     t=1.8s  press right    <-- a page counter must move HERE
 *     t=3.6s  wait           <-- and must NOT move here
 *
 * A page cursor moves only on the press that changes the page. An activity counter moves
 * on every press. One run separates them.
 *
 * ⛔ Addresses are RAM addresses (0x08800000+). They are converted to file offsets
 * internally — pass the address you read out of a dump, not an offset.
 *
 * Usage:
 *   psp-watch.mjs --addr 0x08978D80,0x08978DAC --plan "right,right,wait,right"
 *   psp-watch.mjs --addr 0x08AEA000 --plan "triangle,right,right" --interval 150
 *
 * `--plan` is a comma list; each item is a button or `wait`. Nothing is pressed for `wait`.
 */
import process from "node:process";

const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const RAM_BASE = 0x08800000;

const args = process.argv.slice(2);
const flag = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && args[i + 1] && !args[i + 1].startsWith("--") ? args[i + 1] : d; };

const ADDRS = (flag("addr", "") || "").split(",").map(s => s.trim()).filter(Boolean)
  .map(s => Number(s));
if (!ADDRS.length) { console.error("need --addr 0x...,0x..."); process.exit(2); }

const PLAN = (flag("plan", "") || "").split(",").map(s => s.trim()).filter(Boolean);
const HOLD = Number(flag("hold", "20"));            // frames
const INTERVAL = Number(flag("interval", "150"));   // ms between samples
const DURATION = Number(flag("duration", "1800"));  // ms per plan step
const WIDTH = Number(flag("width", "4"));           // bytes per address

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

class Debugger {
  constructor(url = URL, timeoutMs = 20000) { this.url = url; this.timeoutMs = timeoutMs; this.pending = new Map(); this.nextTicket = 1; }
  async connect() {
    const s = new WebSocket(this.url, "debugger.ppsspp.org"); this.socket = s;
    s.addEventListener("message", (e) => this.#on(String(e.data)));
    s.addEventListener("close", () => this.#fail(new Error("disconnected")));
    s.addEventListener("error", () => {});
    await new Promise((res, rej) => {
      const t = setTimeout(() => rej(new Error("connect timeout")), 8000);
      s.addEventListener("open", () => { clearTimeout(t); res(); }, { once: true });
      s.addEventListener("error", () => { clearTimeout(t); rej(new Error("connect failed")); }, { once: true });
    });
    await this.request("version", { name: "OGA watch", version: "0.1.0" });
  }
  #on(d) { let m; try { m = JSON.parse(d); } catch { return; }
    if (m.ticket != null && this.pending.has(String(m.ticket))) {
      const e = this.pending.get(String(m.ticket)); this.pending.delete(String(m.ticket));
      clearTimeout(e.timer); m.event === "error" ? e.reject(new Error(m.message)) : e.resolve(m);
    } }
  #fail(err) { for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(err); } this.pending.clear(); }
  request(event, fields = {}) {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.reject(new Error("not connected"));
    const tk = String(this.nextTicket++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.pending.delete(tk); rej(new Error(`${event} timed out`)); }, this.timeoutMs);
      this.pending.set(tk, { resolve: res, reject: rej, timer });
      this.socket.send(JSON.stringify({ event, ticket: tk, ...fields }));
    });
  }
  async read(address, size) {
    const r = await this.request("memory.read", { address, size, replacements: false });
    return Buffer.from(r.base64 || "", "base64");
  }
  press(b, d) { return this.request("input.buttons.press", { button: b, duration: d }); }
  close() { try { this.socket?.close(); } catch {} }
}

const VALUE = (buf) => {                      // little-endian, up to 4 bytes
  let v = 0;
  for (let i = Math.min(buf.length, WIDTH) - 1; i >= 0; i--) v = v * 256 + buf[i];
  return v;
};

async function main() {
  const db = new Debugger();
  await db.connect();
  const series = ADDRS.map(() => []);
  const events = [];                            // {t, what}
  const t0 = Date.now();
  const now = () => ((Date.now() - t0) / 1000).toFixed(2);

  try {
    console.log(`watching ${ADDRS.length} address(es) every ${INTERVAL} ms`);
    for (const a of ADDRS) console.log(`  0x${a.toString(16).toUpperCase().padStart(8, "0")}`);
    console.log("");

    let sampling = true;
    const sampler = (async () => {
      while (sampling) {
        const bufs = await Promise.all(ADDRS.map(a => db.read(a, WIDTH).catch(() => null)));
        const vals = bufs.map(b => b ? VALUE(b) : null);
        series.forEach((s, i) => s.push(vals[i]));
        await sleep(INTERVAL);
      }
    })();

    for (const step of PLAN) {
      if (step === "wait") {
        events.push({ t: now(), what: "wait (no press)" });
        console.log(`[${now()}s] wait (no press)`);
        await sleep(DURATION);
      } else {
        await db.press(step, HOLD);
        events.push({ t: now(), what: step });
        console.log(`[${now()}s] pressed ${step}`);
        await sleep(DURATION);
      }
    }

    sampling = false;
    await sampler;

    // ---- report: did each address MOVE on a press, and did it move on a WAIT? ----
    console.log("");
    console.log("=== change points per address ===");
    series.forEach((s, i) => {
      const changes = [];
      for (let k = 1; k < s.length; k++) {
        if (s[k] !== s[k - 1]) changes.push({ idx: k, from: s[k - 1], to: s[k] });
      }
      const addr = ADDRS[i].toString(16).toUpperCase().padStart(8, "0");
      console.log(`  0x${addr}: ${changes.length} change(s)`);
      for (const c of changes.slice(0, 20)) {
        const secs = ((c.idx * INTERVAL) / 1000).toFixed(2);
        // which plan event was most recent before this sample?
        let nearest = "(start)";
        for (const e of events) if (parseFloat(e.t) <= parseFloat(secs)) nearest = e.what;
        console.log(`     t=${secs}s  ${c.from} -> ${c.to}   after: ${nearest}`);
      }
    });

    console.log("");
    console.log("=== READ THIS ===");
    console.log("  A PAGE CURSOR moves only after the press that changes the page,");
    console.log("  and does NOT move after a `wait` step.");
    console.log("  An ACTIVITY counter moves after EVERY press, including ones that");
    console.log("  change nothing on screen.");
    console.log("  The `after:` label on each change is what separates them.");

    // machine-readable dump for later analysis
    const { writeFileSync, mkdirSync } = await import("node:fs");
    const OUT = flag("out", "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe");
    mkdirSync(OUT, { recursive: true });
    const f = `${OUT}/watch-${flag("tag", String(Date.now()))}.json`;
    writeFileSync(f, JSON.stringify({ addrs: ADDRS, interval: INTERVAL, plan: PLAN, events, series }, null, 1) + "\n");
    console.log(`\n  raw series -> ${f}`);
  } finally { db.close(); }
}

main().catch(e => { console.error(`!! ${e.message}`); process.exit(1); });
