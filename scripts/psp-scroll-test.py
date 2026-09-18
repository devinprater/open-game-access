#!/usr/bin/env python3
"""psp-scroll-test.py -- does this menu actually scroll? Full-resolution, per-press.

WHY
    An earlier conclusion ("the pause menu does not respond to down") was drawn from a coarse 8x8 grid
    hash of luminance. A menu highlight is a SMALL, LOCALIZED change -- a few hundred pixels changing
    colour -- which a 64-cell grid can easily average away. So the earlier negative may have been an
    instrument artefact, not a property of the menu.

    This measures the exact number of changed pixels (and a bounding box) between consecutive captures,
    both with presses and with NO presses as a control. If a highlight moves, the changed-pixel count
    will be a small but distinctly non-zero number localised to the list area; if the screen is truly
    inert, presses will match the no-press baseline.

USAGE
    python scripts/psp-scroll-test.py --press down --rounds 4
"""
import argparse, importlib.util, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
S = os.path.join(TMP, "scrolltest")


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def grab(tag):
    os.makedirs(S, exist_ok=True)
    p = os.path.join(S, "%s.png" % tag)
    try:
        if os.path.exists(p):
            os.remove(p)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), p], capture_output=True)
    if not os.path.exists(p) or os.path.getsize(p) < 1000:
        return None
    return p


def diff(p1, p2):
    """Exact changed-pixel count and bounding box of the change."""
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(p1).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(p2).convert("RGB")).astype(np.int16)
    d = np.abs(a - b).max(axis=2) > 16
    n = int(d.sum())
    if n == 0:
        return 0, None
    ys, xs = np.nonzero(d)
    return n, (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--wait", type=float, default=1.2)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    # --- control: two captures with NO press ---
    print()
    print("=== CONTROL (no press) ===")
    c0 = grab("c0")
    time.sleep(a.wait + 1.5)
    c1 = grab("c1")
    if not c0 or not c1:
        print("  CAPTURE FAILED")
        return 1
    n, box = diff(c0, c1)
    print("  changed pixels: %d   bbox: %s" % (n, box))

    # --- with presses ---
    print()
    print("=== WITH PRESS (%s) ===" % a.press)
    prev = grab("p0")
    for i in range(1, a.rounds + 1):
        d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % a.press)
        time.sleep(a.wait)
        cur = grab("p%d" % i)
        if not prev or not cur:
            print("  round %d: CAPTURE FAILED" % i)
            prev = cur
            continue
        n, box = diff(prev, cur)
        verdict = ""
        if n == 0:
            verdict = "no change at all"
        elif n < 200000:
            verdict = "SMALL LOCALIZED CHANGE -> the highlight moved"
        else:
            verdict = "large change (scene/effect)"
        print("  round %d: changed pixels: %-9d bbox: %-28s %s" % (i, n, box, verdict))
        prev = cur

    print()
    print("Interpretation: if WITH PRESS matches the CONTROL (or is 0), this menu genuinely does not")
    print("scroll with that button. If it shows a small localized change, it DOES scroll and the")
    print("earlier 'does not scroll' conclusion was an artefact of a coarse grid hash.")
    d.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
