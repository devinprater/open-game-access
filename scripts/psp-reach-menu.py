#!/usr/bin/env python3
"""psp-reach-menu.py -- drive the game until the screen is a STATIC UI screen, then say so.

WHAT IT SOLVES
    Cursor work needs a static screen with UI colour, and the game does not stay in one (section 24
    drove into a menu, then a later check found it back on an animating scene). Rather than hand-roll
    that each time, this script tries a sequence of candidate buttons, and after each one measures:
        * two-frame pixel difference  (a menu is ~0%, a scene is >1%)
        * saturated-colour percentage (a scene is ~0%, UI is several %)
    and STOPS as soon as it has a static screen with UI colour, printing which button did it.

    Capture runs from THIS process via subprocess, which has silently failed before, so it verifies
    the file exists and parses before using it -- a missing capture is reported, never treated as
    "no change".

USAGE
    python scripts/psp-reach-menu.py [--buttons cross,circle,triangle,square,select,start,l,r]
"""
import argparse, importlib.util, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def shot(path):
    """Capture, then PROVE it produced a readable PNG. Returns None if not."""
    try:
        if os.path.exists(path):
            os.remove(path)
    except OSError:
        pass
    subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), path], capture_output=True)
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        return None
    try:
        from PIL import Image
        Image.open(path).load()
        return path
    except Exception:
        return None


def measure():
    """Return (frame_diff_pct, sat_pct) or None if capture failed."""
    from PIL import Image
    import numpy as np
    p1 = shot(os.path.join(TMP, "rm1.png"))
    time.sleep(1.5)
    p2 = shot(os.path.join(TMP, "rm2.png"))
    if not p1 or not p2:
        return None
    a = np.asarray(Image.open(p1).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(p2).convert("RGB")).astype(np.int16)
    diff = (np.abs(a - b).max(axis=2) > 16).mean() * 100
    sat = ((a.max(axis=2) - a.min(axis=2)) > 60).mean() * 100
    return round(float(diff), 3), round(float(sat), 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--buttons", default="cross,circle,triangle,square,select,start,l,r")
    ap.add_argument("--frames", type=int, default=14)
    ap.add_argument("--wait", type=float, default=1.8)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    m = measure()
    print("start: %s" % (m if m else "CAPTURE FAILED"))
    if m and m[0] < 0.5 and m[1] > 2.0:
        print("ALREADY a static UI screen")
        d.close()
        return

    for b in [x.strip() for x in a.buttons.split(",") if x.strip()]:
        d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":%d}'
                  % (b, a.frames))
        time.sleep(a.wait)
        m = measure()
        if not m:
            print("  %-9s CAPTURE FAILED" % b)
            continue
        diff, sat = m
        if diff < 0.5 and sat > 2.0:
            verdict = "STATIC+UI  <-- MENU"
        elif diff < 0.5:
            verdict = "static"
        else:
            verdict = "animating"
        print("  %-9s diff=%-7s sat=%-6s %s" % (b, diff, sat, verdict))
        if diff < 0.5 and sat > 2.0:
            print("\nREACHED a static UI screen via '%s' -- run the cursor test now." % b)
            d.close()
            return

    print("\nno static+UI screen reached with this sequence")
    d.close()


if __name__ == "__main__":
    main()
