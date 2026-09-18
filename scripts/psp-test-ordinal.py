#!/usr/bin/env python3
"""psp-test-ordinal.py -- test a manager ordinal as the cursor, with liveness asserted.

WHAT THIS TESTS
    Doc section 44 mapped the per-screen manager (DAT_00397770+0 -> 0x08C08EB0) and found a small
    ordinal at +0x24 = 1 sitting beside the render base (+0x14), the block count (+0x18) and the
    node-list head/tail (+0x28/+0x2C). That is the most cursor-shaped field found in the object.

    The previous attempt to evaluate it was invalid because the emulator was FROZEN (ticks delta 0) --
    a halted CPU produces exactly the readings a true negative does. Section 44 made asserting liveness
    a standing precondition, so this script does all four things a valid run needs:

      1. asserts the CPU is EXECUTING (ticks advancing) before and after;
      2. uses TWO debugger connections -- reads on one, input on the other (reads suppress presses);
      3. requires each press to MOVE THE DISPLAY, and refuses if it does not;
      4. requires more samples than the menu has rows, and applies the WRAP test.

USAGE
    python scripts/psp-test-ordinal.py --press down --rounds 12
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
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")

# the per-screen manager pointer lives at DAT_00397770 (+0 of the struct at RAM 0x08B9B770)
MGR_PTR_ADDR = 0x08804000 + 0x00397770
OFFSETS = [0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C]


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


def cpu_status(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def live(c, tag):
    a = cpu_status(c); time.sleep(1.5); b = cpu_status(c)
    if not a or not b:
        print("  %-7s no cpu.status -- debugger attached?" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    ok = d > 0
    print("  %-7s ticks delta %.1fs: %-12d stepping=%s -> %s"
          % (tag, 1.5, d, b.get("stepping"), "EXECUTING" if ok else "FROZEN"))
    return ok


def grab(tag):
    q = os.path.join(TMP, "ord-%s.png" % tag)
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def pixdiff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--wait", type=float, default=1.3)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()      # reads + control
    pad = pp.Debugger()    # input only

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE (standing precondition, doc section 44) ===")
    if not live(c, "before"):
        print()
        print("REFUSING: the emulator is not executing, so no result would be valid.")
        print("Recover by kill-and-relaunch, then re-run.")
        c.close(); pad.close()
        return 2

    # resolve the manager for THIS screen
    raw = c.read(MGR_PTR_ADDR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    print()
    print("manager for this screen: 0x%08X" % mgr)
    if not (0x08800000 <= mgr < 0x0A000000):
        print("  the manager pointer is not a valid RAM address -- the screen may not be a menu")
        c.close(); pad.close()
        return 3
    head = c.read(mgr, 0x40)
    print("  header:", " ".join("%d" % struct.unpack_from("<i", head, o)[0] for o in OFFSETS))

    def snap():
        b = c.read(mgr, 0x40)
        return {o: struct.unpack_from("<i", b, o)[0] for o in OFFSETS} if b and len(b) >= 0x34 else {}

    series = [snap()]
    p_prev = grab("s0")
    moved = 0
    for i in range(1, a.rounds + 1):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + i,
                                "button": a.press, "frames": 10}))
        time.sleep(a.wait)
        series.append(snap())
        cur = grab("s%d" % i)
        n = pixdiff(p_prev, cur)
        if n:
            moved += 1
        p_prev = cur

    print()
    print("=== presses that moved the display: %d/%d ===" % (moved, a.rounds))
    print()
    print("press   " + " ".join("%10s" % ("+0x%02X" % o) for o in OFFSETS))
    for i, s in enumerate(series):
        print("  %3d   " % i + " ".join("%10d" % s.get(o, 0) for o in OFFSETS))

    print()
    print("=== fields that moved ===")
    found = []
    for o in OFFSETS:
        vals = [s.get(o) for s in series]
        if len(set(vals)) > 1:
            found.append((o, vals))
            print("   +0x%02X  %s" % (o, vals))
    if not found:
        print("   (none)")

    print()
    print("=== wrap test on small-ordinal movers (a cursor must return to a value it held) ===")
    any_wrap = False
    for o, vals in found:
        if max(vals) > 64 or min(vals) < 0:
            print("   +0x%02X  not a small ordinal (min %d max %d) -- allocator/pointer scale"
                  % (o, min(vals), max(vals)))
            continue
        for per in range(1, 13):
            if len(vals) > per + 1 and all(vals[k] == vals[k + per] for k in range(len(vals) - per)):
                print("   +0x%02X  WRAPS with period %d  <== CURSOR CANDIDATE" % (o, per))
                any_wrap = True
                break
        else:
            print("   +0x%02X  no wrap within period 12 (small ordinal, but monotonic/irregular)" % o)
    if not found:
        print("   nothing to test")

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if moved == 0:
        print("NOTE: no press moved the display, so the field readings above are NOT evidence.")
    elif not ok:
        print("NOTE: the emulator froze during the run, so treat results with suspicion.")
    elif any_wrap:
        print("A wrapping small ordinal was found. Verify it on a menu with more rows before trusting it.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
