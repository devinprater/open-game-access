#!/usr/bin/env node
/**
 * psp-ar-nav.mjs -- navigate to the MAIN MENU deliberately, checking after every press.
 *
 * Why: blindly mashing start/cross enters Another Road's story intro, from which there is no
 * cheap exit. This presses ONE button, screenshots, and reports, so the arrival point is
 * known rather than assumed.
 *
 * Usage: node psp-ar-nav.mjs <button1,button2,...>   (sequential presses with checks)
 */
import { execFileSync } from "node:child_process";
import { readFileSync, readdirSync, statSync, copyFileSync } from "node:fs";
import { join } from "node:path";

const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const OUT = "C:/Users/Devin Prater/AppData/Local/Temp/ar-nav";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function shot(label) {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  if (!fresh.length) return null;
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  const dst = join(OUT, `${label}.ppm`);
  copyFileSync(join(SHOTDIR, fresh[0]), dst);
  return dst;
}

const steps = (process.argv[2] || "start,start,cross,cross").split(",");
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

console.log("baseline");
console.log(shot("00-start") ? "  00-start.ppm" : "  SHOT FAILED");
let n = 1;
for (const b of steps) {
  press(b);
  await sleep(2000);
  const p = shot(`${String(n).padStart(2, "0")}-after-${b}`);
  console.log(`after ${b.padEnd(6)} -> ${p ? p.split(/[\\/]/).pop() : "SHOT FAILED"}`);
  n++;
}
