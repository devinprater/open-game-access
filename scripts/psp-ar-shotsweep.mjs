#!/usr/bin/env node
/**
 * psp-ar-shotsweep.mjs -- Rule 0b applied to PIXELS: press each button and measure whether
 * the SCREEN changed. RAM windows can miss a change; a screenshot cannot.
 *
 * Shells out to the project's own capture (psp-probe.mjs shot) so the capture path is the
 * known-good one, then diffs the PPMs here.
 *
 * Read-only except injected presses.
 */
import { execFileSync } from "node:child_process";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";

const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function shotOnce() {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "shot"], { stdio: "pipe" });
  const after = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm"));
  const fresh = after.filter((f) => !before.has(f));
  if (!fresh.length) throw new Error("capture produced no new PPM");
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  return join(SHOTDIR, fresh[0]);
}

function readPPM(path) {
  const b = readFileSync(path);
  // header: P6 W H MAXVAL\n
  let i = 2, fields = [], sawHash = false;
  const toks = [];
  while (toks.length < 3 && i < b.length) {
    while (i < b.length && /[\s]/.test(String.fromCharCode(b[i]))) i++;
    if (b[i] === 0x23) { while (i < b.length && b[i] !== 0x0a) i++; continue; }
    let s = i;
    while (i < b.length && !/\s/.test(String.fromCharCode(b[i]))) i++;
    toks.push(Number(b.subarray(s, i).toString()));
  }
  i++; // single whitespace after maxval
  const [, w, h] = toks;
  const px = b.subarray(i);
  return { w, h, px };
}

function diffPixels(a, b) {
  const n = Math.min(a.px.length, b.px.length);
  let changed = 0, minX = 1e9, maxX = -1, minY = 1e9, maxY = -1;
  const w = a.w;
  for (let k = 0; k + 2 < n; k += 3) {
    if (a.px[k] !== b.px[k] || a.px[k + 1] !== b.px[k + 1] || a.px[k + 2] !== b.px[k + 2]) {
      changed++;
      const p = k / 3, x = p % w, y = (p / w) | 0;
      if (x < minX) minX = x; if (x > maxX) maxX = x;
      if (y < minY) minY = y; if (y > maxY) maxY = y;
    }
  }
  return { changed, bbox: maxX >= 0 ? [minX, minY, maxX, maxY] : null, total: n / 3 };
}

const BUTTONS = ["start", "cross", "circle", "square", "triangle", "up", "down", "left", "right", "ltrigger", "rtrigger"];
const press = (b) => execFileSync(process.execPath, [join(SCRIPTS, "psp-probe.mjs"), "press", b], { stdio: "pipe" });

console.log("--- CONTROL: two shots, no press at all ---");
let a = shotOnce(); await sleep(1200); let b = shotOnce();
let c = diffPixels(readPPM(a), readPPM(b));
console.log(`  no-press drift: ${c.changed} px (${(100 * c.changed / c.total).toFixed(2)}%) bbox=${JSON.stringify(c.bbox)}`);
const NOISE = c.changed;

console.log("\n--- per-button screen change ---");
for (const btn of BUTTONS) {
  const s1 = shotOnce();
  press(btn);
  await sleep(700);
  const s2 = shotOnce();
  const d = diffPixels(readPPM(s1), readPPM(s2));
  const real = d.changed > Math.max(NOISE * 2, 500);
  console.log(`  ${btn.padEnd(10)} changed=${String(d.changed).padStart(8)} px (${(100 * d.changed / d.total).toFixed(2)}%) bbox=${JSON.stringify(d.bbox)}  ${real ? "CHANGED" : ""}`);
}
