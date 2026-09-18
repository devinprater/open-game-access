#!/usr/bin/env python3
"""psp-tt-glyph.py -- identify the objective indicator's POINTING DIRECTION.

⚠️ The `narrow-end` method in this file is WRONG -- validated 0/8 against known rotations.
It is kept only as a record of a failed approach. The `sparse-perp` method validated 8/8 and
is the one to rely on. See scripts/psp-tt-glyph-validate.py and the project doc.

THE GLYPH (measured, not assumed)
    A double chevron: a large `^` plus a smaller filled triangle beneath it. It ROTATES to point
    along the objective bearing -- the user saw it as a "double right arrow >>" when the objective
    was to the right, and pointing up when the objective was ahead.
        FILL     RGB(73,112,254) periwinkle, flat-shaded
        OUTLINE  white glow
    Reference frame: centre (1282,~150) of 1706x1066, extent 106x85, pointing UP.

WHY THE OLD BEARING WAS WRONG
    It used the principal axis (which runs through the WINGS) and then looked for the "sparse
    extreme" along the perpendicular. Validating while flying left and right gave ~0 deg both ways,
    which is not usable.

    It also mis-identifies the shape: the glyph is NOT a single chevron, it is a BIG chevron plus a
    small triangle, and the small triangle sits at the BASE side.

THE METHOD USED HERE -- narrow-end = pointing end
    A chevron is CONCAVE at its base and NARROW at its apex. So, over a set of candidate angles:
      * project every glyph pixel onto the candidate axis,
      * measure how many pixels sit in the outer band at each end,
      * the POINTING end is the one with FEWER pixels (the apex), the base is the dense, wide end.
    The candidate angle whose sparse end best matches wins. This is shape-agnostic: it works for a
    chevron that has been rotated to any bearing.

USAGE
    python scripts/psp-tt-glyph.py --shot FILE      # report bearing for one PNG
    python scripts/psp-tt-glyph.py --shot FILE --dump
    python scripts/psp-tt-glyph.py --sweep DIR      # analyse every PNG in DIR that has a glyph
"""
import argparse, glob, math, os, sys

import numpy as np
from PIL import Image

FILL = (73, 112, 254)
TOL = 8
MIN_PX = 80
MAX_EXTENT = 260          # a glyph, not terrain; the double chevron measures ~106x85
SAMPLES = [0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180,
           -165, -150, -135, -120, -105, -90, -75, -60, -45, -30, -15]


def glyph_pixels(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im).astype(np.int16)
    m = (np.abs(a[:, :, 0] - FILL[0]) <= TOL) & \
        (np.abs(a[:, :, 1] - FILL[1]) <= TOL) & \
        (np.abs(a[:, :, 2] - FILL[2]) <= TOL)
    ys, xs = np.nonzero(m)
    if len(xs) == 0:
        return None, 0, (im.size[0], im.size[1]), "no fill pixels"
    if len(xs) < MIN_PX:
        return None, len(xs), (im.size[0], im.size[1]), "only %d px" % len(xs)
    w, h = int(xs.max() - xs.min()), int(ys.max() - ys.min())
    if w > MAX_EXTENT or h > MAX_EXTENT:
        return None, len(xs), (im.size[0], im.size[1]), "extent %dx%d too large" % (w, h)
    return (xs.astype(np.float64), ys.astype(np.float64)), len(xs), (im.size[0], im.size[1]), None


def bearing(xs, ys):
    """Return (bearing_deg, score) where bearing 0 = up, + = clockwise (right).

    For each candidate angle, project onto the axis and count pixels in each end band. The pointing
    end is the SPARSE one; the winner is the angle with the largest sparseness ratio, i.e. where the
    asymmetry between ends is greatest and clearest.
    """
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    best = (None, -1.0, None)
    for deg in SAMPLES:
        t = math.radians(deg)
        ax, ay = math.sin(t), -math.cos(t)          # 0 deg -> (0,-1) = up; + goes clockwise
        proj = dx * ax + dy * ay
        lo, hi = float(proj.min()), float(proj.max())
        if hi - lo < 1:
            continue
        lo_band = int((proj <= lo + (hi - lo) * 0.22).sum())
        hi_band = int((proj >= hi - (hi - lo) * 0.22).sum())
        sparse, dense = (lo_band, hi_band) if lo_band <= hi_band else (hi_band, lo_band)
        if dense == 0:
            continue
        # sparseness: apex end should be much thinner than the base end
        ratio = 1.0 - (sparse / float(dense))
        if ratio > best[1]:
            best = (deg, ratio, (sparse, dense))
    return best


def report(path, dump=False):
    pts, n, size, why = glyph_pixels(path)
    if pts is None:
        print("%-46s NO GLYPH  (%s)" % (os.path.basename(path), why))
        return None
    xs, ys = pts
    deg, ratio, ends = bearing(xs, ys)
    if deg is None:
        print("%-46s ambiguous" % os.path.basename(path)); return None
    cx, cy = float(xs.mean()), float(ys.mean())
    side = "RIGHT" if deg > 8 else "LEFT" if deg < -8 else "AHEAD"
    print("%-46s bearing=%+7.1f deg  %-5s  conf=%.2f  px=%-5d centre=(%.0f,%.0f)  ends=%s"
          % (os.path.basename(path), deg, side, ratio, n, cx, cy, ends))
    if dump:
        im = Image.open(path).convert("RGB")
        im.crop((max(0, int(cx) - 90), max(0, int(cy) - 90),
                 int(cx) + 90, int(cy) + 90)).resize((270, 270)).save(path + ".glyph.png")
        print("    crop -> %s.glyph.png" % path)
    return deg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot")
    ap.add_argument("--sweep")
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args()
    if a.shot:
        report(a.shot, a.dump)
    elif a.sweep:
        files = sorted(glob.glob(os.path.join(a.sweep, "*.png")))
        got = []
        for f in files:
            # A capture interrupted mid-write leaves a truncated PNG; skip those rather than dying,
            # since a live beacon is writing into this directory while we sweep it.
            try:
                d = report(f, a.dump)
            except Exception as e:
                print("%-46s unreadable (%s)" % (os.path.basename(f), type(e).__name__))
                continue
            if d is not None:
                got.append(d)
        if got:
            print("\n# %d glyph frame(s); bearings seen: %s"
                  % (len(got), ", ".join("%+.0f" % d for d in got[:20])))
    else:
        ap.error("give --shot FILE or --sweep DIR")


if __name__ == "__main__":
    main()
