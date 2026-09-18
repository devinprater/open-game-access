#!/usr/bin/env python3
"""psp-scroll-gated.py -- the SCROLL test WITH the input gate: is the D-pad delivered, and does it move?

THE CORRECTION THIS MAKES (doc sections 54 and 59)
    Section 54 ran this test without an input gate and concluded:

        "No control moved the display on every press: no scroll control on this screen.
         Transitions moved once; the D-pad and shoulders were inert."

    Section 59 then proved the input path is INTERMITTENT: the same button can be delivered once and
    silently dropped the next, and both look identical downstream. So "the D-pad was inert" cannot be
    distinguished from "the D-pad was never delivered" in that run.

    This version gates EVERY press at the pad button word (`pad + 0x00`, documented PSP bits) and:

      * counts a press only when its documented bit is observed WHILE HELD;
      * reports the per-DELIVERED-press pixel diff, so a delivered press that moves nothing is real
        evidence of an inert control;
      * reports controls that could not be delivered at all as NO VERDICT, never as inert.

    That yields the three-way distinction section 54 could not make:
        delivered + moved     -> the control acts on this screen
        delivered + no move   -> genuinely inert (valid negative)
        not delivered         -> no verdict (instrument)

USAGE
    python scripts/psp-scroll-gated.py --need 4
"""
import argparse
import importlib.util
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}
ORDER = ["down", "up", "left", "right", "l", "r", "circle", "cross", "triangle", "square", "start", "select"]


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
    a = cpu(c); time.sleep(1.2); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--need", type=int, default=4)
    ap.add_argument("--max-tries", type=int, default=30)
    ap.add_argument("--hold", type=int, default=40)
    ap.add_argument("--max-static-moves", type=int, default=10)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")

    def shot(tag):
        q = os.path.join(TMP, "sg-" + tag + ".png")
        if os.path.exists(q):
            try:
                os.remove(q)
            except OSError:
                pass
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
        return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None

    def diff(p1, p2):
        from PIL import Image
        import numpy as np
        if not p1 or not p2:
            return None
        x = np.asarray(Image.open(p1).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(p2).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print("pad object 0x%08X" % PAD)

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    # --- 1. reach a STATIC screen ---
    print()
    print("=== 1. hunting for a STATIC screen ===")
    static = False
    prev = None
    for i in range(a.max_static_moves):
        p1 = shot("a"); time.sleep(2.0); p2 = shot("b")
        d = diff(p1, p2)
        print("   move %d: no-press diff %s" % (i + 1, d))
        if d is not None and d <= 2000:
            static = True
            prev = p2
            break
        # nudge with a control to try to leave a scene
        ctl = ORDER[i % len(ORDER)]
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 2000 + i,
                                "button": ctl, "frames": 12}))
        time.sleep(1.6)
    if not static:
        print("   no static screen reached -- refusing (a per-press diff would be meaningless).")
        c.close(); pad.close()
        return 5
    print("   STATIC screen reached.")

    # --- 2. per-control, gated ---
    print()
    print("=== 2. per-control: press WITH THE GATE, measure each DELIVERED press ===")
    rows = []
    for btn in ORDER:
        want = BITS[btn]
        got, tries, diffs, churn = 0, 0, [], []
        while got < a.need and tries < a.max_tries:
            tries += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 3000 + tries,
                                    "button": btn, "frames": a.hold}))
            delivered = False
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    delivered = True
                    break
            cur = shot("p%d" % (tries % 8))
            if delivered:
                d = diff(prev, cur)
                if d is not None:
                    diffs.append(d)
                got += 1
                # measure no-press churn right after, to know if this screen still settles
                time.sleep(1.4)
                nxt = shot("n%d" % (tries % 8))
                dc = diff(cur, nxt)
                churn.append(dc)
                if dc is None or dc > 2000:
                    churn[-1] = dc
                prev = nxt or cur
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 3500 + tries,
                                    "button": btn, "frames": 2}))
            time.sleep(0.5)
        if got == 0:
            rows.append((btn, "NO VERDICT (0 delivered)", tries, diffs, churn))
        else:
            moved = sum(1 for d in diffs if d and d > 2000)
            rows.append((btn, "%d/%d delivered presses moved" % (moved, got), tries, diffs, churn))

    print()
    print("%-9s %-34s %s" % ("control", "delivered-press result", "per-delivered-press diff"))
    for btn, res, tries, diffs, churn in rows:
        print("  %-9s %-34s %s" % (btn, res, diffs))
    print()
    print("  (tries needed to obtain the delivered presses: %s)"
          % ", ".join("%s:%d" % (b, t) for b, _, t, _, _ in rows))

    print()
    print("=== verdict ===")
    acted = [b for b, r, _, _, _ in rows if r.startswith(tuple("123456789")) and not r.startswith("0/")]
    inert = [b for b, r, _, _, _ in rows if r.startswith("0/")]
    nv = [b for b, r, _, _, _ in rows if "NO VERDICT" in r]
    print("  acted on this screen (delivered AND moved): %s" % (acted or "none"))
    print("  genuinely inert (delivered, never moved):   %s" % (inert or "none"))
    print("  NO VERDICT (could not be delivered):        %s" % (nv or "none"))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
