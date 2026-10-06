#!/usr/bin/env node
/**
 * psp-ar-csdown.mjs -- reach character select and move with DOWN, verifying the character
 * name changes each time.
 *
 * Devin's correction: on this screen DOWN is the mover, not RIGHT (a per-button screen sweep
 * agreed: down changed the screen 83%, left/right changed nothing).
 *
 * Flow: main menu -> down x4 (Training) -> cross -> character select -> down x6, OCR each.
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync, readFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-csdown";
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
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

console.log("=== to main menu check ===");
let t = ocr(shot("00"));
console.log("  " + JSON.stringify(t.slice(0, 80)));

if (!/Level 10000|Arcade|Z Trial/i.test(t)) {
  console.log("not on the main menu; press start/cross to get there");
  for (let i = 0; i < 6; i++) { press("start"); await sleep(1500); press("cross"); await sleep(2000); t = ocr(shot(`nav${i}`)); console.log("  " + JSON.stringify(t.slice(0, 80))); if (/Level 10000|Z Trial/i.test(t)) break; }
}

console.log("\n=== walk to Training (down x4) ===");
press("up", 12); await sleep(700);
for (let i = 0; i < 4; i++) { press("down"); await sleep(600); }
t = ocr(shot("at-training"));
console.log("  " + JSON.stringify(t.slice(0, 90)));

console.log("\n=== enter (cross) ===");
press("cross"); await sleep(4000);
t = ocr(shot("cs0"));
console.log("  cs0 " + JSON.stringify(t.slice(0, 110)));

console.log("\n=== press DOWN and watch the character change ===");
for (let i = 1; i <= 6; i++) {
  press("down"); await sleep(1600);
  const tt = ocr(shot(`cs-down${i}`));
  console.log(`  down ${i}  ${JSON.stringify(tt.slice(0, 110))}`);
}
