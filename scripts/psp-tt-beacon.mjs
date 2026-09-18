#!/usr/bin/env node
// psp-tt-beacon.mjs — AUDIO BEACON on the objective chevron: turn the on-screen arrow's POSITION
// into a stereo pan + pitch cue so a blind player can steer.
//
// WHAT THIS IS (and is not)
//   The game draws an objective chevron on the field. The user asked for a live audio beacon on it.
//   This reads the SCREEN and localises the glyph — it does NOT reverse-engineer a direction vector.
//   The position/objective vector was NOT found in RAM after several attempts, so this sidesteps it.
//   This is the project's sanctioned fallback ("pixel/OCR only as fallback"), justified because the
//   arrow's bearing IS the state the player needs and exists only as a rendered glyph.
//
// THE ARROW, MEASURED (not guessed)
//   Double chevron, both heads pointing along the objective bearing. Colours sampled from a frame:
//       FILL    RGB(73,112,254)   periwinkle blue   (~1,228 px)
//       OUTLINE white             (soft glow)
//       sea     RGB(251,125,6)    orange  -> the fill is well separated from it
//   In the frame measured it sat at about x=1299, y=151 of 1706x1066, pointing UP.
//
//   ⚠️ AN EARLIER VERSION OF THIS SCRIPT LOOKED FOR CYAN (`b>150 & b-r>55 & g>110`) AND FOUND
//   NOTHING, then a looser mask matched 388,430 px of the WHOLE SCREEN (terrain + HUD portrait).
//   The fix was to sample the glyph's actual colours instead of assuming them.
//
// OUTPUT (this script MEASURES and prints; it does not play audio)
//   One line per sample:  bearing  pan  pitch  confidence  centre
//   Map pan -> left/right balance and pitch -> urgency in whatever audio sink you prefer.
//
// CALIBRATION (do not trust the defaults)
//   1. With the arrow pointing UP, check bearing ~ 0.
//   2. Fly so the arrow points LEFT, then RIGHT, and confirm the sign.
//   3. Use --dump to save the crop it selected and LOOK at it.
//
// USAGE
//   node scripts/psp-tt-beacon.mjs                  # single sample
//   node scripts/psp-tt-beacon.mjs --dump           # single sample + save the crop it picked
//   node scripts/psp-tt-beacon.mjs --interval 500   # continuous
//   node scripts/psp-tt-beacon.mjs --shot FILE      # analyse an existing PNG (no capture)

import { execFileSync } from 'node:child_process';

const argv = process.argv.slice(2);
const num = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? Number(argv[i + 1]) : d; };
const has = (f) => argv.indexOf(f) >= 0;

const INTERVAL = num('--interval', 0);      // 0 = single sample
const DUMP = has('--dump');
const SHOT_PATH = (() => {
  const i = argv.indexOf('--shot');
  // forward slashes: backslash paths get mangled before reaching Python
  // normalize to forward slashes: Windows accepts them and backslashes get mangled en route
  return i >= 0 && argv[i + 1] ? argv[i + 1].split('\\').join('/') : null;
})();
const PY = process.env.PSP_PYTHON || 'python.exe';

for (const v of ['--interval', '--shot']) {
  const i = argv.indexOf(v);
  if (i >= 0 && argv[i + 1] === undefined) { console.error(`missing value for ${v}`); process.exit(2); }
}

// Measured signature of the chevron fill: periwinkle blue.
const SIG = { r: 73, g: 112, b: 254 };

const PYCODE = String.raw`
import json, os, sys, math
try:
    from PIL import Image
except Exception as e:
    print(json.dumps({"error": "PIL missing: %s" % e})); sys.exit(0)

shot = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
dump = (len(sys.argv) > 2 and sys.argv[2] == "1")
tmpp = sys.argv[3] if len(sys.argv) > 3 else ""

# capture if we were not given a file
if shot is None:
    os.makedirs(tmpp, exist_ok=True)
    shot = os.path.join(tmpp, "beacon_shot.png")
    import subprocess
    r = subprocess.run([sys.executable, "scripts/psp-shot.py", shot], capture_output=True, text=True)
    if not os.path.exists(shot):
        print(json.dumps({"error": "capture failed", "stderr": (r.stderr or "")[-300:]})); sys.exit(0)

im = Image.open(shot).convert("RGB")
W, H = im.size
a = im.load()

# ⚠️ A TOLERANCE-BASED match on the fill colour FAILED validation: with +-46 it selected
# 2,220 px spanning 1704x978 — i.e. terrain and the HUD portrait share that blue. So match the colour
# EXACTLY (the glyph is flat-shaded), then require the surviving blob to be COMPACT (the glyph is a
# small 2D icon, not a region of the world).
TR, TG, TB = 73, 112, 254
TOL = 8
pts = []
for y in range(0, H, 2):
    for x in range(0, W, 2):
        r, g, b = a[x, y]
        if abs(r-TR) <= TOL and abs(g-TG) <= TOL and abs(b-TB) <= TOL:
            pts.append((x, y))

res = {"found": False, "n": len(pts), "size": [W, H]}

# Compactness gate: a glyph is small. 1706x1066 frame -> demand extent under 220x220 and at least
# 80 matched samples (sampling every 2px). Rejects world terrain and the large HUD portrait.
if pts:
    _xs = [p2[0] for p2 in pts]; _ys = [p2[1] for p2 in pts]
    _w = max(_xs)-min(_xs); _h = max(_ys)-min(_ys)
    if _w > 220 or _h > 220 or len(pts) < 80:
        res["rejected"] = {"n": len(pts), "extent": [_w, _h],
                           "why": "not compact enough to be the chevron glyph"}

if len(pts) >= 80 and "rejected" not in res:
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    cx = sum(xs)/len(xs); cy = sum(ys)/len(ys)

    # ⚠️ "Furthest point from the centroid" gave -101 deg on a chevron that points UP: the furthest
    # point is a WING CORNER, not the apex. A chevron `^` is symmetric about its pointing axis, and
    # its mass is concentrated in the wings, so:
    #   1. the MAJOR principal axis runs through the two wings (perpendicular to the pointing axis),
    #   2. the apex is the extreme point along the PERPENDICULAR of that axis,
    #   3. the apex side is the side with FEWER points (the wings hold the mass).
    n = len(pts)
    sxx = sum((x-cx)**2 for x in xs)/n
    syy = sum((y-cy)**2 for y in ys)/n
    sxy = sum((xs[i]-cx)*(ys[i]-cy) for i in range(n))/n
    theta = 0.5*math.atan2(2*sxy, sxx-syy)          # major-axis angle
    mvx, mvy = math.cos(theta), math.sin(theta)     # major axis (along the wings)
    pvx, pvy = -mvy, mvx                            # perpendicular = the pointing axis

    proj = [( (x-cx)*pvx + (y-cy)*pvy, x, y) for (x, y) in pts]
    lo = min(proj); hi = max(proj)
    # apex = whichever extreme end has FEWER points nearby (the wings are dense)
    def count_near(target, tol=12.0):
        return sum(1 for q in proj if abs(q[0]-target) < tol)
    apex = lo if count_near(lo[0]) < count_near(hi[0]) else hi
    dx, dy = apex[1]-cx, apex[2]-cy
    bearing = math.degrees(math.atan2(dx, -dy))     # 0=up, +ve=clockwise
    # Extent helps reject a cluster that is too small to be the glyph.
    w = max(xs)-min(xs); h = max(ys)-min(ys)
    res = {"found": True, "n": len(pts), "cx": cx, "cy": cy, "bearing": bearing,
           "bbox": [min(xs), min(ys), max(xs), max(ys)], "extent": [w, h], "size": [W, H]}

if dump and tmpp:
    os.makedirs(tmpp, exist_ok=True)
    im.crop((max(0,int(res.get("cx",W/2))-160), max(0,int(res.get("cy",H/2))-160),
             min(W,int(res.get("cx",W/2))+160), min(H,int(res.get("cy",H/2))+160))
       ).save(os.path.join(tmpp, "beacon_crop.png"))
    res["dump"] = os.path.join(tmpp, "beacon_crop.png")

print(json.dumps(res))
`;

// ⚠️ Use FORWARD SLASHES. A Windows path passed with backslashes arrived at Python as
// "C:\\Users\\Devin Prater\\AppData\\LocalTemppsp-probelf0.png" (separators eaten), which raised
// FileNotFoundError and surfaced as a bogus "chevron NOT found". Forward slashes work on Windows.
const TMP = process.env.LOCALAPPDATA
  ? `${process.env.LOCALAPPDATA}/Temp/psp-beacon`
  : '/tmp/psp-beacon';

function sampleOnce() {
  // ⚠️ Do NOT swallow a hard failure: when Python raises, it prints NO JSON, and the caller then
  // reports a misleading "chevron NOT found". Capture stderr and surface it as an error instead.
  try {
    const out = execFileSync(PY, ['-c', PYCODE, SHOT_PATH || '', DUMP ? '1' : '0', TMP],
      { encoding: 'utf8', maxBuffer: 1 << 24, stdio: ['ignore', 'pipe', 'pipe'] });
    const line = out.trim().split('\n').filter((l) => l.startsWith('{')).pop();
    return JSON.parse(line || '{}');
  } catch (e) {
    const err = ((e.stderr || '') + (e.stdout || '')).trim().split('\n').slice(-3).join(' | ');
    return { error: err || e.message };
  }
}

function render(r) {
  if (r.error) return '# error: ' + r.error;
  if (!r.found) return `# chevron NOT found (${r.n} candidate px of RGB(73,112,254)±46) — arrow may be off-screen`;
  const bearing = r.bearing;
  const pan = Math.max(-1, Math.min(1, bearing / 60));
  const pitch = Math.round(440 * Math.pow(2, Math.abs(bearing) / 90));
  const dir = bearing > 8 ? 'RIGHT' : bearing < -8 ? 'LEFT' : 'AHEAD';
  return `bearing=${bearing.toFixed(1)}deg pan=${pan.toFixed(2)} pitch=${pitch}Hz dir=${dir} ` +
         `px=${r.n} centre=(${r.cx.toFixed(0)},${r.cy.toFixed(0)}) extent=${r.extent[0]}x${r.extent[1]}`;
}

(async () => {
  console.log('# objective-chevron audio beacon');
  console.log('# signature: periwinkle RGB(73,112,254) (+-8 exact, compactness-gated)');
  console.log('# bearing 0 = up/straight ahead; + clockwise (right); - left');
  if (SHOT_PATH) console.log('# analysing existing frame: ' + SHOT_PATH);
  if (process.env.PSP_BEACON_DEBUG) console.log('# DEBUG PY=' + PY + ' TMP=' + TMP);
  for (;;) {
    let r;
    try { r = sampleOnce(); } catch (e) { console.log('# sample error: ' + e.message); }
    if (r) {
      console.log(render(r));
      if (DUMP && r.dump) console.log('# crop -> ' + r.dump + '   (LOOK AT IT)');
    }
    if (!INTERVAL) break;
    await new Promise((res) => setTimeout(res, INTERVAL));
  }
})();
