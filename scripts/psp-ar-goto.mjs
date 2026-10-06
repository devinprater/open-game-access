#!/usr/bin/env node
/**
 * psp-ar-goto.mjs -- drive to a named screen, verifying with OCR after every press.
 *
 * Why: blind mashing either stalls on the title or falls into Another Road's story intro
 * (no cheap exit). This presses one button, screenshots, OCRs, and only continues when the
 * text says it moved.
 *
 * Usage: node psp-ar-goto.mjs mainmenu | charset
 */
import { execFileSync } from "node:child_process";
import { readdirSync, statSync, copyFileSync, readFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-goto";
const TESS = "C:/Users/Devin Prater/scoop/apps/tesseract-languages/current";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function sh(cmd, args) { return execFileSync(cmd, args, { stdio: "pipe" }).toString(); }
function shot(label) {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const ppm = join(SHOTDIR, fresh[0]);
  const png = join(OUT, `${label}.png`);
  sh("python", ["C:/Users/Public/ppm2png_pad.py", ppm, png]);
  return png;
}
function ocr(png) {
  const base = png.replace(/\.png$/, "");
  try {
    execFileSync("tesseract", [png, base, "-l", "eng", "--psm", "6"], { stdio: "pipe", env: { ...process.env, TESSDATA_PREFIX: TESS } });
    return readFileSync(base + ".txt", "utf8");
  } catch (e) { console.error("ocr failed: " + e.message); return ""; }
}
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

const target = process.argv[2] || "mainmenu";
let n = 0;
for (let step = 0; step < 22; step++) {
  const png = shot(String(n).padStart(2, "0"));
  const t = png ? ocr(png).replace(/\s+/g, " ").trim() : "";
  console.log(`${String(n).padStart(2)}  ${JSON.stringify(t.slice(0, 90))}`);

  if (target === "mainmenu" && /Level 10000|Power Level|Arcade|Z Trial|Profile Card/i.test(t)) {
    console.log("\n=> MAIN MENU reached"); break;
  }
  if (target === "charset" && /Select Characters|PLAYER|RANDOM/i.test(t)) {
    console.log("\n=> CHARACTER SELECT reached"); break;
  }
  // decide the next press from what is on screen
  if (/CRIWARE|^$|^[^A-Za-z]*$/.test(t)) press("start");
  else if (/Load|Game Data|DRAGON BALL Z SHIN B/i.test(t)) press("cross");
  else if (/Level 10000|Arcade|Z Trial/i.test(t)) press(target === "charset" ? "down" : "cross");
  else press("start");
  n++;
  await sleep(2200);
}
