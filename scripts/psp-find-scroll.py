#!/usr/bin/env python3
"""psp-find-scroll.py -- find a STATIC screen on which some control actually moves the highlight.

WHY THIS EXISTS (doc section 49)
    The cursor hunt's harness is sound but has produced four consecutive correct refusals because the
    screens reached by blind input are INERT: static (good) but no button moves the highlight (useless).
    Section 49 concluded the blocker is reachability of a suitable screen. Rather than guessing which
    button or which screen, this automates the search:

        for each candidate screen:
            confirm the CPU is executing          (ticks delta)
            confirm the screen is STATIC          (no-press frame diff ~ 0)
            for each candidate control:
                press it; if the display changes AND the screen stays static afterwards,
                report SUCCESS and stop                  <- a scrollable static menu
            otherwise navigate BACK (circle/start) and try the next screen

    This is the strongest automated move available: it cannot READ the screen, but it can measure
    scrollability exactly, and scrollability is the property the cursor test needs.

USAGE
    python scripts/psp-find-scroll.py --max-back 6
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
STATIC_MAX = 2000          # px of no-press difference still counted as "static"
CONTROLS = ["down", "up", "right", "left", "l", "r"]


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def drain(ws, sec):
    out = []
    end = time.time() + sec
    while time.time() < end:
        try:
            ws.settimeout(max(0.02, end - time.time()))
            out.append(json.loads(ws.recv()))
        except Exception:
            pass
    return out


def cpu(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def executing(c):
    a = cpu(c); time.sleep(1.2); b = cpu(c)
    return bool(a and b and b.get("ticks", 0) > a.get("ticks", 0))


def grab(tag):
    q = os.path.join(TMP, "fs-%s.png" % tag)
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def diff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def static_now(tag):
    """Two captures with no press; returns the difference (small => static)."""
    a = grab(tag + "a")
    time.sleep(2.2)
    b = grab(tag + "b")
    return diff(a, b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-back", type=int, default=6, help="how many screens to walk back through")
    ap.add_argument("--wait", type=float, default=1.4)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("looking for a STATIC screen where some control MOVES the highlight")
    print()

    for screen in range(a.max_back + 1):
        if not executing(c):
            print("  emulator not executing -- stop. Kill and relaunch.")
            c.close(); pad.close()
            return 2

        d0 = static_now("s%d" % screen)
        print("  screen %d: no-press diff %s px -> %s"
              % (screen, d0, "STATIC" if d0 is not None and d0 <= STATIC_MAX else "ANIMATING"))

        if d0 is not None and d0 <= STATIC_MAX:
            # static: try each control and see which one moves something
            for ctl in CONTROLS:
                before = grab("b%s" % ctl)
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 10,
                                        "button": ctl, "frames": 12}))
                time.sleep(a.wait)
                after = grab("a%s" % ctl)
                n = diff(before, after)
                if n:
                    # confirm the screen stayed static (so the change was a highlight, not a scene)
                    if executing(c):
                        after2 = grab("a2%s" % ctl)
                        n2 = diff(after, after2)
                        print("     %-6s -> %7d px  %s" % (ctl, n,
                              "SCROLLS (and still static)" if (n2 is not None and n2 <= STATIC_MAX)
                              else "changed but not static after"))
                        if n2 is not None and n2 <= STATIC_MAX:
                            print()
                            print("  FOUND: screen %d is STATIC and '%s' moves the highlight." % (screen, ctl))
                            print("  Run the node/struct probes now with --press %s" % ctl)
                            c.close(); pad.close()
                            return 0
                    else:
                        print("     %-6s -> %7d px (emulator stopped)" % (ctl, n))
                else:
                    print("     %-6s -> no movement" % ctl)
            print("     no control moves the highlight on this screen; navigating back")
        else:
            print("     animating -- waiting rather than pressing")

        # navigate back one level and try again
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 20,
                                "button": "circle", "frames": 12}))
        time.sleep(2.2)
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 21,
                                "button": "start", "frames": 12}))
        time.sleep(2.5)

    print()
    print("  no scrolling static screen found in %d steps." % (a.max_back + 1))
    c.close(); pad.close()
    return 1


if __name__ == "__main__":
    sys.exit(main())
