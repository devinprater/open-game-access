#!/usr/bin/env node
/**
 * psp-ar-csverify.mjs -- confirm 0x08ABC2E8 is the character-select cursor.
 *
 * Reads the word, presses DOWN, reads it again, and OCRs the on-screen name plate for each
 * step. If the id maps to the OCR'd name every time, the cursor is proven.
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-csverify";
const TESS = "C:/Users/Devin Prater/scoop/apps/tesseract-languages/current";
const URL = process.env.PSP_DEBUGGER || "ws://127.0.0.1:12345/debugger";
const CUR = 0x08ABC2E8;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const NAMES = ["Goku","Teen Gohan","Gohan","Vegeta","Trunks","Krillin","Piccolo","Frieza",
  "Android #18","Cell","Kid Buu","Cooler","Broly","Gotenks","Gogeta","Vegito","Pikkon",
  "Janemba","Future Gohan","Majin Buu","Super Buu","Dabura","Bardock","Future Trunks"];
const DISPLAY = [0,1,2,18,3,4,23,5,6,7,8,9,19,10,12];

class Debugger {
  constructor() { this.q = new Map(); this.t = 1; }
  connect() {
    return new Promise((res, rej) => {
      const s = new WebSocket(URL, "debugger.ppsspp.org");
      this.s = s;
      s.addEventListener("message", (e) => {
        let m; try { m = JSON.parse(String(e.data)); } catch { return; }
        if (m.ticket != null && this.q.has(String(m.ticket))) {
          const it = this.q.get(String(m.ticket)); this.q.delete(String(m.ticket));
          clearTimeout(it.timer);
          m.event === "error" ? it.rej(new Error(m.message || "request failed")) : it.res(m);
        }
      });
      s.addEventListener("open", async () => { try { await this.req("version", { name: "oga ar csverify", version: "0.1.0" }); res(); } catch (e) { rej(e); } });
      s.addEventListener("error", () => rej(new Error("socket error")));
      setTimeout(() => rej(new Error("connect timeout")), 10000);
    });
  }
  req(event, fields = {}) {
    const ticket = String(this.t++);
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.q.delete(ticket); rej(new Error(event + " timed out")); }, 20000);
      this.q.set(ticket, { res, rej, timer });
      this.s.send(JSON.stringify({ event, ticket, ...fields }));
    });
  }
  async read(a, s) { const r = await this.req("memory.read", { address: a, size: s, replacements: false }); return Buffer.from(r.base64 || "", "base64"); }
}

const db = new Debugger();
await db.connect();
function shot(label) {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const png = join(OUT, `${label}.png`);
  execFileSync("python", ["C:/Users/Public/ppm2png_pad.py", join(SHOTDIR, fresh[0]), png], { stdio: "pipe" });
  return png;
}
function ocr(png) {
  const base = png.replace(/\.png$/, "");
  try {
    execFileSync("tesseract", [png, base, "-l", "eng", "--psm", "6"], { stdio: "pipe", env: { ...process.env, TESSDATA_PREFIX: TESS } });
    return readFileSync(base + ".txt", "utf8").replace(/\s+/g, " ").trim();
  } catch { return ""; }
}
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

const rows = [];
for (let i = 0; i <= 8; i++) {
  const b = await db.read(CUR, 4);
  const id = b.readUInt32LE(0);
  const png = shot(String(i).padStart(2, "0"));
  const t = png ? ocr(png) : "";
  const m = t.match(/([A-Z][A-Za-z0-9 .#']{2,22})\s*[@®™©]?\s*(Normal|Perfect Form|Super Saiyan|Final|Base)/);
  const readName = m ? m[1].trim() : "";
  const expected = NAMES[id] ?? `id ${id}`;
  const match = readName && (readName.replace(/\s/g, "").toLowerCase().startsWith(expected.replace(/\s/g, "").toLowerCase()) || expected.replace(/\s/g, "").toLowerCase().startsWith(readName.replace(/\s/g, "").toLowerCase()));
  rows.push({ step: i, id, expected, screen: readName, match: !!match });
  console.log(`${i}  id=${String(id).padStart(2)}  table="${expected}"   screen="${readName}"  ${match ? "MATCH" : "?"}`);
  if (i < 8) { press("down"); await sleep(1600); }
}
writeFileSync("C:/Users/Devin Prater/AppData/Local/Temp/ar-csverify.json", JSON.stringify({ cur: "0x" + CUR.toString(16), rows }, null, 2));
console.log("\n" + rows.filter((r) => r.match).length + "/" + rows.length + " confirmed");
db.s?.close();
process.exit(0);
