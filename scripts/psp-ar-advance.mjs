#!/usr/bin/env node
/**
 * psp-ar-advance.mjs -- press a button repeatedly until the screen STOPS changing, i.e. we
 * have arrived at a static screen (a menu), then report the cursor address.
 *
 * Why this shape: on this title the boot path runs through attract/cutscene segments where
 * only `cross`/`circle` do anything, and the menu is what comes after. "Advance till it
 * stops moving" is the shortest honest route to the menu.
 */
import { execFileSync } from "node:child_process";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";

const SHOTDIR = "C:/Users/Devin Prater/AppData/Local/Temp/psp-probe";
const SCRIPTS = "C:/Users/Devin Prater/oga-work-codex/scripts";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function sh(cmd, args) { return execFileSync(process.execPath, [join(SCRIPTS, cmd), ...args], { stdio: "pipe" }).toString(); }
function shotOnce() {
  const before = new Set(readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm")));
  sh("psp-probe.mjs", ["shot"]);
  const fresh = readdirSync(SHOTDIR).filter((f) => f.endsWith(".ppm") && !before.has(f));
  fresh.sort((a, b) => statSync(join(SHOTDIR, b)).mtimeMs - statSync(join(SHOTDIR, a)).mtimeMs);
  return join(SHOTDIR, fresh[0]);
}
function readPPM(path) {
  const b = readFileSync(path);
  let i = 2; const toks = [];
  while (toks.length < 3 && i < b.length) {
    while (i < b.length && /\s/.test(String.fromCharCode(b[i]))) i++;
    if (b[i] === 0x23) { while (i < b.length && b[i] !== 0x0a) i++; continue; }
    let s = i;
    while (i < b.length && !/\s/.test(String.fromCharCode(b[i]))) i++;
    toks.push(Number(b.subarray(s, i).toString()));
  }
  return { w: toks[1], px: b.subarray(i + 1) };
}
function changed(a, b) {
  const n = Math.min(a.px.length, b.px.length);
  let c = 0;
  for (let k = 0; k + 2 < n; k += 3) if (a.px[k] !== b.px[k] || a.px[k + 1] !== b.px[k + 1] || a.px[k + 2] !== b.px[k + 2]) c++;
  return c;
}

const BTN = process.argv[2] || "cross";
const MAX = Number(process.argv[3] || "30");

let prev = readPPM(shotOnce());
let staticCount = 0;
for (let n = 1; n <= MAX; n++) {
  sh("psp-probe.mjs", ["press", BTN]);
  await sleep(900);
  const cur = readPPM(shotOnce());
  const d = changed(prev, cur);
  console.log(`press ${String(n).padStart(2)} (${BTN}): screen changed ${d} px`);
  prev = cur;
  if (d === 0) { staticCount++; if (staticCount >= 2) { console.log(`\n=> screen STATIC after ${n} presses -- likely a menu`); break; } }
  else staticCount = 0;
}
console.log("\n--- cursor probe at the static screen ---");
console.log(sh("psp-ar-menus.mjs", ["probe"]));
