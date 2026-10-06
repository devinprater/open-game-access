#!/usr/bin/env node
/**
 * psp-ar-fullcycle.mjs -- navigate from a fresh boot to character select and enumerate the
 * WHOLE selectable roster by pressing DOWN and OCRing the name plate each time.
 *
 * Reliable navigation learned this session:
 *   title -> start x3 -> Load screen -> cross -> cross -> MAIN MENU
 *   main menu: up x12 (top), down x4 (Training), cross -> CHARACTER SELECT
 *
 * On character select DOWN cycles the character; the name plate reads e.g. "Cell @ Perfect Form".
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-full";
const TESS = "C:/Users/Devin Prater/scoop/apps/tesseract-languages/current";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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
const press = (b, n = 1) => { for (let i = 0; i < n; i++) execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" }); };

// --- navigate ---
console.log("=== navigate to character select ===");
for (let i = 0; i < 4; i++) { press("start"); await sleep(1800); const t = ocr(shot(`nav-s${i}`)); console.log(`  start ${i}: ${JSON.stringify(t.slice(0, 60))}`); if (/Level 10000|Z Trial/i.test(t)) break; }
let t = ocr(shot("nav-a"));
if (!/Level 10000|Z Trial/i.test(t)) { press("cross"); await sleep(2500); t = ocr(shot("nav-b")); console.log(`  cross: ${JSON.stringify(t.slice(0, 60))}`); }
if (!/Level 10000|Z Trial/i.test(t)) { press("cross"); await sleep(2500); t = ocr(shot("nav-c")); console.log(`  cross2: ${JSON.stringify(t.slice(0, 60))}`); }

console.log("  main menu? " + String(/Level 10000|Z Trial/i.test(t)));
press("up", 12); await sleep(900);
press("down", 4); await sleep(900);
press("cross"); await sleep(4500);
t = ocr(shot("cs-start"));
console.log("  charset: " + JSON.stringify(t.slice(0, 90)));

// --- cycle ---
console.log("\n=== cycling with DOWN ===");
const rows = [];
for (let i = 0; i <= 26; i++) {
  const png = shot(String(i).padStart(2, "0"));
  const s = png ? ocr(png) : "";
  // pull the "Name @ Form" fragment out of the noisy plate
  const m = s.match(/([A-Z][A-Za-z0-9 .#']{2,22})\s*[@®™©]?\s*(Normal|Perfect Form|Base|Super Saiyan|SSJ|Frieza|Final|2nd|3rd|1st)[^A-Za-z]/);
  const frag = m ? `${m[1].trim()} @ ${m[2]}` : null;
  rows.push({ i, s, frag });
  console.log(`  ${String(i).padStart(2)}  ${JSON.stringify((frag || s).slice(0, 70))}`);
  if (i < 26) { press("down"); await sleep(1500); }
}
writeFileSync("C:/Users/Public/charset-cycle.json", JSON.stringify(rows, null, 2));
console.log("\nwrote C:/Users/Public/charset-cycle.json");
