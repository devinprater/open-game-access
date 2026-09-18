#!/usr/bin/env python3
"""psp-tt-indicators.py -- inventory the direction indicators the field draws.

WHY
    The audio beacon currently keys on ONE glyph: the periwinkle objective chevron RGB(73,112,254).
    But that chevron is not always on screen. If it is absent, the player gets no cue at all.

    The user's instruction: "if no arrow, track the enemies."

    So before writing an enemy tracker we must find out WHAT the game draws for enemies. This script
    does not guess: it burst-captures the field and reports, per frame,
      * how many pixels match the known objective chevron,
      * the dominant SATURATED colours outside the HUD box (candidates for enemy markers),
      * counts for a few specific candidate colours.
    and saves the frames so they can be looked at.

USAGE
    python scripts/psp-tt-indicators.py --frames 10 --gap 1.0
"""
import argparse, collections, os, subprocess, sys, time

from PIL import Image
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "psp-shot.py")
TMP = os.path.join(os.environ.get("LOCALAPPDATA", "/tmp"), "Temp", "psp-beacon")

OBJ_FILL = (73, 112, 254)          # objective chevron, known
SEA = (251, 125, 6)                # orange water, known -- excluded so it does not swamp the list

# HUD box, in fractions of the frame, to exclude when hunting for world-space markers.
HUD = (0.00, 0.06, 0.30, 0.34)


def capture(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    subprocess.run([sys.executable, SHOT, path], capture_output=True, text=True, timeout=60)
    return os.path.exists(path)


def analyse(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im).astype(np.int16)
    H, W = a.shape[0], a.shape[1]

    def count(col, tol=8):
        return int(((np.abs(a[:, :, 0] - col[0]) <= tol) &
                    (np.abs(a[:, :, 1] - col[1]) <= tol) &
                    (np.abs(a[:, :, 2] - col[2]) <= tol)).sum())

    n_obj = count(OBJ_FILL)

    # Saturated colours OUTSIDE the HUD: candidate enemy/marker glyphs.
    x0, y0, x1, y1 = int(HUD[0] * W), int(HUD[1] * H), int(HUD[2] * W), int(HUD[3] * H)
    m = np.ones((H, W), bool)
    m[y0:y1, x0:x1] = False
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = (mx - mn) > 90
    sel = sat & m
    cols = collections.Counter()
    ys, xs = np.nonzero(sel)
    for y, x in zip(ys[::7], xs[::7]):                     # subsample for speed
        cols[(int(r[y, x]) // 24 * 24, int(g[y, x]) // 24 * 24, int(b[y, x]) // 24 * 24)] += 1

    return n_obj, cols.most_common(6), (W, H)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=10)
    ap.add_argument("--gap", type=float, default=1.0)
    a = ap.parse_args()

    os.makedirs(TMP, exist_ok=True)
    print("# objective chevron = RGB%s exact" % (OBJ_FILL,))
    print("# listing saturated colours OUTSIDE the HUD box %s" % (HUD,))
    print()
    hits = 0
    for i in range(a.frames):
        p = os.path.join(TMP, "ind%02d.png" % i)
        if not capture(p):
            print("frame %02d: CAPTURE FAILED" % i); continue
        n, top, size = analyse(p)
        if n > 80:
            hits += 1
        flag = "CHEVRON" if n > 80 else "  --   "
        print("frame %02d [%s] obj=%-6d size=%s" % (i, flag, n, size))
        for c, k in top:
            print("        sat RGB%-16s %5d" % (str(c), k))
        time.sleep(a.gap)
    print()
    print("# chevron present in %d/%d frames" % (hits, a.frames))
    print("# frames saved to %s -- LOOK at one to see what else is drawn" % TMP)


if __name__ == "__main__":
    main()
