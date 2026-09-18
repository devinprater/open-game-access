#!/usr/bin/env python3
"""psp-tt-glyph-validate.py -- validate a bearing estimator against KNOWN rotations.

WHY
    Two bearing methods were tried on field frames and neither could be trusted:
      * "sparse extreme along the principal perpendicular" read ~0 deg on every frame;
      * "narrow-end scan over candidate angles" read +45/+135 on three frames that LOOK identical.
    Both were used without ever being tested against a glyph whose direction was known.

    The fix is the discipline this project keeps re-learning: TEST THE INSTRUMENT on a
    known-positive / known-negative pair before trusting its output.

METHOD
    Take the real up-pointing glyph, rotate it by a known angle, and check whether each estimator
    returns that angle. Rotation is exact, so any error is the estimator's.

USAGE
    python scripts/psp-tt-glyph-validate.py --glyph FILE
"""
import argparse, math, os, sys

import numpy as np
from PIL import Image

FILL = (73, 112, 254)
TOL = 8


def fill_mask(arr):
    a = arr.astype(np.int16)
    return ((np.abs(a[:, :, 0] - FILL[0]) <= TOL) &
            (np.abs(a[:, :, 1] - FILL[1]) <= TOL) &
            (np.abs(a[:, :, 2] - FILL[2]) <= TOL))


def method_sparse_perp(xs, ys):
    """Old method: principal axis (through the wings); apex = the sparse extreme along the
    perpendicular. Returns bearing in degrees, 0 = up, + = clockwise."""
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    n = len(xs)
    sxx = float((dx * dx).mean()); syy = float((dy * dy).mean()); sxy = float((dx * dy).mean())
    th = 0.5 * math.atan2(2 * sxy, sxx - syy)
    pvx, pvy = -math.sin(th), math.cos(th)      # perpendicular to the wings = pointing axis
    pr = dx * pvx + dy * pvy
    lo, hi = float(pr.min()), float(pr.max())

    def near(v, tol=12.0):
        return int((np.abs(pr - v) < tol).sum())

    av = lo if near(lo) < near(hi) else hi
    k = int(np.argmin(np.abs(pr - av)))
    # orient the sign: pvx/pvy has a 180-degree ambiguity, so pick the end that is sparse
    bx, by = xs[k] - cx, ys[k] - cy
    return math.degrees(math.atan2(bx, -by))


def method_narrow_end(xs, ys, step=5):
    """New method: scan candidate angles; the pointing end is the one with fewer pixels."""
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    best = (None, -1.0)
    for deg in range(-180, 180, step):
        t = math.radians(deg)
        ax, ay = math.sin(t), -math.cos(t)
        pr = dx * ax + dy * ay
        lo, hi = float(pr.min()), float(pr.max())
        if hi - lo < 1:
            continue
        lo_band = int((pr <= lo + (hi - lo) * 0.22).sum())
        hi_band = int((pr >= hi - (hi - lo) * 0.22).sum())
        sparse, dense = (lo_band, hi_band) if lo_band <= hi_band else (hi_band, lo_band)
        if dense == 0:
            continue
        ratio = 1.0 - sparse / float(dense)
        if ratio > best[1]:
            best = (deg, ratio)
    return best[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glyph", required=True, help="a PNG containing the glyph (up-pointing)")
    a = ap.parse_args()

    im = Image.open(a.glyph).convert("RGB")
    m = fill_mask(np.asarray(im))
    ys, xs = np.nonzero(m)
    if len(xs) < 80:
        sys.exit("no glyph found in %s (%d px)" % (a.glyph, len(xs)))
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    tile = im.crop((x0, y0, x1 + 1, y1 + 1))

    print("# validating bearing estimators on KNOWN rotations of the real glyph")
    print("# tile %dx%d, %d fill px  (expected: 0 for the original orientation)" % (tile.width, tile.height, len(xs)))
    print()
    print("%-8s %-18s %-18s" % ("rotate", "sparse-perp", "narrow-end"))
    print("%-8s %-18s %-18s" % ("------", "-----------", "----------"))

    for deg in (0, 45, 90, 135, 180, -45, -90, -135):
        # PIL rotates counter-clockwise for positive angles; screen bearings here are clockwise,
        # so rotate by -deg to obtain a screen bearing of +deg.
        rot = tile.rotate(-deg, expand=True, resample=Image.NEAREST, fillcolor=(0, 0, 0))
        rm = fill_mask(np.asarray(rot))
        rys, rxs = np.nonzero(rm)
        if len(rxs) < 40:
            print("%-8d %-18s %-18s" % (deg, "(too few px)", "(too few px)"))
            continue
        want = ((deg + 180) % 360) - 180
        s1 = method_sparse_perp(rxs.astype(float), rys.astype(float))
        s2 = method_narrow_end(rxs.astype(float), rys.astype(float))
        f = lambda v: "n/a" if v is None else "%+7.1f" % (((v + 180) % 360) - 180)
        ok = lambda v: "" if v is None else ("  OK" if abs((((v - want) + 180) % 360) - 180) < 20 else "  WRONG")
        print("%-8d %-18s %-18s" % (deg, f(s1) + ok(s1), f(s2) + ok(s2)))

    print()
    print("# want = the rotation applied. A usable estimator tracks it across the sweep.")


if __name__ == "__main__":
    main()
