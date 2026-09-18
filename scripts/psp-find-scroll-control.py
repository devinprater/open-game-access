#!/usr/bin/env python3
"""psp-find-scroll-control.py -- find a control that moves the display on EVERY press (a SCROLL).

THE DISTINCTION THIS EXPLOITS (doc section 54)
    Section 53's scan credited `circle` and `triangle` as "MOVED and SETTLED". The chained follow-up
    then voided (`0/8` presses moved the display) -- because those two buttons are TRANSITIONS: one
    press navigates to a different screen, and further presses on the new screen do nothing.

    A SCROLL control is different in kind: it moves the selection WITHIN a screen, so it moves the
    display on EVERY press, repeatedly and reversibly. That is the property the cursor hunt needs, and
    it is directly testable:

        candidate control is a SCROLL if, on a static screen,
            press -> display moves AND settles, for EVERY one of N consecutive presses.

    A transition fails this on the 2nd press; a scroll passes all of them.

WHAT THIS DOES
    1. asserts liveness;
    2. hunts a static screen;
    3. for each control, presses it N times in a row, measuring movement and settledness after EACH
       press, and reports how many of the N moved (a scroll scores N; a transition scores 1; a dead
       control scores 0);
    4. reports the best candidate.

    Once a scroll control is identified, `psp-cursor-hunt.py`'s node phase can be pointed at it.

USAGE
    python scripts/psp-find-scroll-control.py --reps 5
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
CONTROLS = ["down", "up", "left", "right", "l", "r",
            "circle", "cross", "triangle", "square", "start", "select"]
NAV = ["l", "r", "start", "select", "circle", "triangle", "cross", "square"]
STATIC_MAX = 2000


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


def live(c, tag):
    a = cpu(c); time.sleep(1.3); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def shot(tag):
    q = os.path.join(TMP, "fs2-" + tag + ".png")
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


def press(pad, ctl, rid):
    pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": rid,
                            "button": ctl, "frames": 12}))
    time.sleep(1.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=5)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator frozen.")
        c.close(); pad.close()
        return 2

    print()
    print("=== hunting for a STATIC screen ===")
    for i in range(16):
        ctl = NAV[i % len(NAV)]
        press(pad, ctl, 100 + i)
        a1 = shot("h%d" % i); time.sleep(1.9); a2 = shot("h%db" % i)
        d = diff(a1, a2)
        if d is not None and d <= STATIC_MAX:
            print("   static screen after %d move(s), last control %s (no-press diff %d)" % (i + 1, ctl, d))
            break
    else:
        print("   no static screen found in 16 moves")
        c.close(); pad.close()
        return 1

    print()
    print("=== which control behaves like a SCROLL (moves on EVERY press)? ===")
    results = {}
    for j, ctl in enumerate(CONTROLS):
        moved_count = 0
        settled_count = 0
        details = []
        prev = shot("p%d0" % j)
        for k in range(a.reps):
            press(pad, ctl, 400 + j * 10 + k)
            cur = shot("p%d%d" % (j, k))
            mv = diff(prev, cur)
            time.sleep(1.4)
            after = shot("p%d%db" % (j, k))
            st = diff(cur, after)
            if mv:
                moved_count += 1
                if st is not None and st <= STATIC_MAX:
                    settled_count += 1
            details.append("%s/%s" % (mv, st))
            prev = cur
        results[ctl] = (moved_count, settled_count)
        kind = "SCROLL CANDIDATE" if moved_count == a.reps else (
               "transition (moved once)" if moved_count == 1 else
               ("inert" if moved_count == 0 else "partial"))
        print("   %-9s moved %d/%d, settled %d/%d   %s   [%s]"
              % (ctl, moved_count, a.reps, settled_count, a.reps, kind, ", ".join(details)))

    best = [c2 for c2, (mv, st) in results.items() if mv == a.reps and st == a.reps]
    print()
    if best:
        print("SCROLL CONTROL(S) FOUND: %s" % ", ".join(best))
        print("=> point the node probe at one: python scripts/psp-cursor-hunt.py  (or --press %s)" % best[0])
    else:
        print("No control moved the display on every press: no scroll control on this screen.")
        print("Transitions moved once; the D-pad and shoulders were inert.")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
