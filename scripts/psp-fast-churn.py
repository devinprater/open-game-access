#!/usr/bin/env python3
"""psp-fast-churn.py -- per-candidate churn at a FAST poll (the fix from doc section 64).

THE FLAW THIS FIXES
    Section 63 nominated 4 candidate words by full-RAM diff. The churn control for that scan was taken at
    the SAME coarse cadence as the scan itself: each 24 MB read takes ~8.8 s, so a control comparison spans
    ~17 s. One candidate (0x08C0BD7C) cycles with a ~5 s period, so across a 17 s window it returned to a
    similar value and was scored "unchanged" -- it passed the churn filter and looked like a cursor.

    **A long read window aliases against a fast cycle.** The fix is to measure churn PER CANDIDATE at a
    rate well above the candidate's expected period: a single-address read is ~1 ms, so polling 40 times
    at 0.25 s is cheap and cannot alias a 5 s cycle.

PROTOCOL (per candidate)
    1. NO-INPUT phase: poll the word 40x at 0.25 s (~10 s) -> how many distinct values, and the series.
    2. Decide:
         * >1 distinct value with NO input  -> CHURNING, reject (whatever the press series looked like);
         * exactly 1 value with NO input    -> CLEAN; then a press response is attributable.
    3. Press phase (gated): poll during verified-delivered presses -> does it move?
    4. Verdict per candidate: CURSOR CANDIDATE only if clean AND responds.

USAGE
    python scripts/psp-fast-churn.py --addrs 0x08BB4130,0x08BB42D8,0x08BB4EE8,0x08C0BD7C --button down
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
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}


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


def show(u):
    f = struct.unpack("<f", struct.pack("<I", u))[0]
    if 0x08800000 <= u < 0x0A000000:
        return "PTR"
    if -1e6 < f < 1e6 and f == f:
        return "f=%.4g" % f
    return "0x%08X" % u


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addrs", default="0x08BB4130,0x08BB42D8,0x08BB4EE8,0x08C0BD7C")
    ap.add_argument("--button", default="down")
    ap.add_argument("--churn-samples", type=int, default=40)
    ap.add_argument("--churn-interval", type=float, default=0.25)
    ap.add_argument("--presses", type=int, default=6)
    a = ap.parse_args()

    addrs = [int(x, 0) for x in a.addrs.split(",") if x.strip()]
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
    want = BITS[a.button]

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(addr):
        b = c.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    print()
    print("=== PHASE 1: NO-INPUT churn, %d samples at %.2fs (well below a 5s cycle) ==="
          % (a.churn_samples, a.churn_interval))
    churn = {}
    for ad in addrs:
        vals = []
        for _ in range(a.churn_samples):
            vals.append(u32(ad))
            time.sleep(a.churn_interval)
        uniq = len(set(vals))
        churn[ad] = (uniq, vals)
        verdict = "CHURNING -- reject" if uniq > 1 else "clean"
        print("  0x%08X  %d distinct value(s)  %-20s %s"
              % (ad, uniq, show(vals[0]), verdict))
        if uniq > 1:
            print("      series: %s" % [show(v) for v in vals[:16]])

    print()
    print("=== PHASE 2: press phase, GATED (only verified-delivered '%s' presses counted) ===" % a.button)
    press_series = {ad: [] for ad in addrs}
    got, tries = 0, 0
    while got < a.presses and tries < 40:
        tries += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 8000 + tries,
                                "button": a.button, "frames": 55}))
        delivered = False
        for _ in range(3):
            w = padword()
            if w is not None and (w & want) == want:
                delivered = True
                break
        if delivered:
            for ad in addrs:
                press_series[ad].append(u32(ad))
            got += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 8800 + tries,
                                "button": a.button, "frames": 2}))
        time.sleep(0.6)
    print("  delivered %d/%d (tried %d)" % (got, a.presses, tries))

    print()
    print("=== VERDICT per candidate ===")
    print("  %-12s %-10s %-12s %s" % ("address", "churn", "press resp", "verdict"))
    for ad in addrs:
        uniq_churn, _ = churn[ad]
        pv = press_series[ad]
        responds = len(set(pv)) > 1
        if uniq_churn > 1:
            v = "REJECT (churns with no input)"
        elif responds:
            v = "** CURSOR CANDIDATE **"
        else:
            v = "clean but inert under this button"
        print("  0x%08X  %-10s %-12s %s"
              % (ad, "%d vals" % uniq_churn, "yes" if responds else "no", v))
        if responds:
            print("       press series: %s" % [show(x) for x in pv])

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
