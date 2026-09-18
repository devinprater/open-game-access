#!/usr/bin/env python3
"""psp-dpad-reliability.py -- is the D-pad actually delivered, or is the debugger dropping it?

WHY THIS MATTERS ENORMOUSLY
    If `down` never reaches the game from the debugger, then EVERY "the D-pad is inert on the reachable
    screens" finding is an artefact of the instrument, not a property of the game. That would reopen the
    whole cursor hunt. Conversely, if `down` is delivered and the game simply does not react, then the
    game genuinely ignores it on those screens.

    Contradictory evidence so far:
      * the clean bit test: up -> 0x0010 (up bit), down -> 0x0000 (NOT delivered), l -> 0x0000, r -> 0x0000
      * a rapid follow-up test: down -> 0x0040 (which IS the down bit)

    So the same button read 0x0000 once and 0x0040 another time. That is a DELIVERY RACE, not a game
    property, and it must be separated before any D-pad conclusion is trusted.

METHOD
    For each D-pad button: press 3 separate times with a LONG hold, sample the pad word repeatedly DURING
    each hold, and allow a full settle (2 s) between attempts so holds never overlap. Report the observed
    bitmask per attempt and whether it matches the documented PSP bit:
        up 0x0010   right 0x0020   down 0x0040   left 0x0080

USAGE
    python scripts/psp-dpad-reliability.py --reps 3
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
DPAD = {"up": 0x0010, "right": 0x0020, "down": 0x0040, "left": 0x0080}


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
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--hold", type=int, default=90)
    ap.add_argument("--gap", type=float, default=2.0)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print()
    print("pad object 0x%08X   (live sceCtrl button word at +0x00)" % PAD)
    print()

    def word():
        return struct.unpack("<I", c.read(PAD, 4))[0]

    print("idle word (no press): 0x%04X" % word())
    print()
    print("%-6s %-8s %s" % ("btn", "expect", "observed bitmask per attempt (during hold) / after release"))
    summary = {}
    for btn, want in DPAD.items():
        rows = []
        for r in range(a.reps):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 500 + r,
                                    "button": btn, "frames": a.hold}))
            during = [word() for _ in range(7)]
            time.sleep(a.gap)
            after = [word() for _ in range(3)]
            rows.append((during, after))
        okruns = sum(1 for d, _ in rows if want in d)
        summary[btn] = (okruns, a.reps)
        print()
        print("  %s  (expect 0x%04X)" % (btn.upper(), want))
        for i, (d, af) in enumerate(rows, 1):
            print("    attempt %d  during: %s" % (i, ["0x%04X" % v for v in d]))
            print("              after : %s" % ["0x%04X" % v for v in af])
        print("    -> delivered in %d/%d attempts" % (okruns, a.reps))

    print()
    print("=== SUMMARY ===")
    for btn, (okruns, n) in summary.items():
        verdict = ("DELIVERED" if okruns == n else
                   "NEVER DELIVERED (instrument drops it)" if okruns == 0 else
                   "INTERMITTENT (%d/%d) -- a race" % (okruns, n))
        print("   %-6s %s" % (btn, verdict))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
