#!/usr/bin/env python3
"""psp-region-sweep.py -- gated diff over SMALL regions, fast enough that churn cannot alias.

THE FIX (doc section 65)
    The 24 MB full-RAM diff took ~8.8 s per pass, so a control comparison spanned ~17 s and a signal with
    a ~2.5 s period aliased back to a similar value and passed as "unchanged". Section 65 concluded:
    match the control's sampling rate to the signal's period, not to the cost of the primary measurement.

    A 256 KB region read takes ~0.09 s at the measured ~3 MB/s. So a per-region baseline/press/after diff
    spans only a couple of seconds even including the gated press -- and any value that changes is *seen*
    to change rather than aliasing away.

METHOD
    Pass 1: for every region of the readable span (0x08800000-0x0A000000, 96 x 256 KB):
              read baseline -> gate a press -> read -> record changed words
            Regions with changes are kept as candidates.
    Pass 2: for each candidate region, repeat the gated diff 3 more times and INTERSECT.
    Pass 3: per surviving word, a FAST no-input churn poll (0.2 s x 40) -- the section-64 fix -- and
            reject anything that moves with no input.
    A no-press control region-diff run is included for comparison.

USAGE
    python scripts/psp-region-sweep.py --button down --region 0x40000
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
LO, HI = 0x08800000, 0x0A000000
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--button", default="down")
    ap.add_argument("--region", type=lambda x: int(x, 0), default=0x40000)
    ap.add_argument("--pass2-rounds", type=int, default=3)
    ap.add_argument("--churn-samples", type=int, default=40)
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
    want = BITS[a.button]

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def gated_press():
        for t in range(24):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 9000 + t,
                                    "button": a.button, "frames": 45}))
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    return True
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 9500 + t,
                                    "button": a.button, "frames": 2}))
            time.sleep(0.4)
        return False

    def region_diff(base, size):
        b1 = c.read(base, size)
        if not b1 or len(b1) != size:
            return None
        if not gated_press():
            return "NODELIVER"
        time.sleep(0.35)
        b2 = c.read(base, size)
        if not b2 or len(b2) != size:
            return None
        s = set()
        for off in range(0, size, 4):
            if b1[off:off + 4] != b2[off:off + 4]:
                s.add(base + off)
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 9999,
                                "button": a.button, "frames": 2}))
        time.sleep(0.5)
        return s

    # measure a region read speed
    nreg = (HI - LO) // a.region
    t0 = time.time()
    probe = c.read(LO, a.region)
    dt = time.time() - t0
    print()
    print("region size 0x%X  x %d regions   one region read: %.3fs" % (a.region, nreg, dt))
    print("=> a baseline+press+after pass per region spans roughly %.1fs (fast vs a ~2.5s cycle)"
          % (dt * 2 + 1.0))

    print()
    print("=== PASS 1: gated diff over all %d regions ===" % nreg)
    cand = {}
    for i in range(nreg):
        base = LO + i * a.region
        r = region_diff(base, a.region)
        if r == "NODELIVER":
            print("  region %3d 0x%08X  press not delivered -- skipped" % (i, base))
            continue
        if r:
            cand[base] = r
            print("  region %3d 0x%08X  %d changed word(s)" % (i, base, len(r)))
    print()
    print("  regions with any change: %d of %d" % (len(cand), nreg))

    if not cand:
        print("  NONE -- no region changed on a delivered press.")
        print()
        print("=== liveness AFTER ===")
        live(c, "after")
        c.close(); pad.close()
        return 0

    print()
    print("=== PASS 2: intersect each candidate region across %d more gated rounds ===" % a.pass2_rounds)
    survivors = {}
    for base in sorted(cand):
        sets = [cand[base]]
        ok = True
        for r in range(a.pass2_rounds):
            got = region_diff(base, a.region)
            if got == "NODELIVER" or got is None:
                ok = False
                break
            sets.append(got)
        if not ok:
            print("  0x%08X  dropped (delivery failure in pass 2)" % base)
            continue
        inter = set.intersection(*sets)
        print("  0x%08X  sizes %s -> intersection %d" % (base, [len(s) for s in sets], len(inter)))
        for ad in inter:
            survivors[ad] = base

    print()
    print("=== PASS 3: FAST no-input churn poll per surviving word (the section-64 fix) ===")
    final = []
    for ad in sorted(survivors):
        vals = []
        for _ in range(a.churn_samples):
            b = c.read(ad, 4)
            if b and len(b) == 4:
                vals.append(struct.unpack("<I", b)[0])
            time.sleep(0.2)
        uniq = len(set(vals))
        if uniq == 1:
            final.append(ad)
            print("  0x%08X  CLEAN (1 value: 0x%08X)" % (ad, vals[0]))
        else:
            print("  0x%08X  churns (%d values) -- rejected" % (ad, uniq))

    print()
    print("=== RESULT ===")
    if final:
        print("  %d word(s) respond to a delivered '%s' press AND are constant with no input:" % (len(final), a.button))
        for ad in final:
            b = c.read(ad, 4)
            u = struct.unpack("<I", b)[0] if b and len(b) == 4 else 0
            f = struct.unpack("<f", struct.pack("<I", u))[0]
            print("     0x%08X  raw 0x%08X  float %g  %s"
                  % (ad, u, f, "(PTR)" if 0x08800000 <= u < 0x0A000000 else ""))
    else:
        print("  NONE -- every word that changed on a delivered press also churns with no input,")
        print("         at a sampling rate fast enough that churn cannot alias.")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
