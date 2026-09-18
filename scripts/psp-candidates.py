#!/usr/bin/env python3
"""psp-candidates.py -- characterize section 63's 4 surviving words, and probe the SECOND list head.

WHAT THIS DOES
    Section 63's gated full-RAM diff reduced 9,000+ changed words to exactly 4 that respond to a VERIFIED
    menu-direction press on a screen where that press demonstrably moves the display:

        0x08BB4130  -65536       (0xFFFF0000)
        0x08BB42D8  -65536
        0x08BB4EE8  -65536
        0x08C0BD7C  1073741824   (0x40000000 = 2.0f)

    This script:
      1. dumps the neighbourhood of each candidate, and classifies nearby words (pointer / small int /
         float) so we can tell what structure each lives in;
      2. tests each candidate AGAIN with the gate, per delivered press, so a churning artefact cannot
         survive;
      3. probes the SECOND render-list head at M + 0x34 (identified by the second-agent trace, but never
         walked here) with the same gate, since section 61's walk used M + 0x28 only.

USAGE
    python scripts/psp-candidates.py --button down --need 5
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
MGR_PTR = 0x08804000 + 0x00397770
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}
CANDIDATES = [0x08BB4130, 0x08BB42D8, 0x08BB4EE8, 0x08C0BD7C]


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


def classify(v, unsigned):
    """Describe a 32-bit word: pointer, small int, float, or other."""
    if 0x08800000 <= unsigned < 0x0A000000:
        return "PTR 0x%08X" % unsigned
    f = struct.unpack("<f", struct.pack("<I", unsigned))[0]
    if -1e6 < f < 1e6 and (abs(f) > 1e-6 or unsigned == 0):
        if abs(f) < 1e4 and (f == f):
            return "f=%.4g" % f
    return "0x%08X" % unsigned


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--button", default="down")
    ap.add_argument("--need", type=int, default=5)
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x40)
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
    print("pad 0x%08X   button '%s' bit 0x%04X" % (PAD, a.button, want))

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(addr):
        b = c.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    # --- 1. characterize the candidates ---
    print()
    print("=== 1. NEIGHBOURHOODS of section 63's survivors ===")
    for base in CANDIDATES:
        print()
        print("  --- 0x%08X (span 0x%X) ---" % (base, a.span))
        lo = base - a.span // 2
        raw = c.read(lo, a.span)
        if not raw or len(raw) != a.span:
            print("    read failed")
            continue
        for off in range(0, a.span, 4):
            addr = lo + off
            v = struct.unpack_from("<i", raw, off)[0]
            uv = struct.unpack_from("<I", raw, off)[0]
            mark = "  <== CANDIDATE" if addr == base else ""
            print("    +0x%02X 0x%08X  %-12s %s" % (off, addr, classify(v, uv), mark))

    # --- 2. re-test the candidates under gated presses ---
    print()
    print("=== 2. re-test each candidate under %d DELIVERED '%s' presses ===" % (a.need, a.button))
    base_vals = {ad: u32(ad) for ad in CANDIDATES}
    print("   baseline: %s" % {("0x%08X" % k): v for k, v in base_vals.items()})
    got, tries, series = 0, 0, {ad: [] for ad in CANDIDATES}
    while got < a.need and tries < 40:
        tries += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 7000 + tries,
                                "button": a.button, "frames": 55}))
        delivered = False
        for _ in range(3):
            w = padword()
            if w is not None and (w & want) == want:
                delivered = True
                break
        if delivered:
            for ad in CANDIDATES:
                series[ad].append(u32(ad))
            got += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 7600 + tries,
                                "button": a.button, "frames": 2}))
        time.sleep(0.65)
    print("   delivered %d/%d (tried %d)" % (got, a.need, tries))
    for ad in CANDIDATES:
        vals = series[ad]
        move = len(set(vals)) > 1
        print("   0x%08X  %s  %s" % (ad, vals, "<== RESPONDS" if move else "constant"))

    # --- 3. walk the SECOND list head at M + 0x34 ---
    print()
    print("=== 3. the SECOND render-list head at M + 0x34 ===")
    M = u32(MGR_PTR)
    print("   manager 0x%08X" % M)
    hdr = c.read(M, 0x40)
    if hdr and len(hdr) == 0x40:
        for off in (0x28, 0x2C, 0x34, 0x38, 0x3C):
            print("   M+0x%02X = 0x%08X  (%s)" % (off, struct.unpack_from("<I", hdr, off)[0],
                                                  classify(0, struct.unpack_from("<I", hdr, off)[0])))
        for label, off in (("+0x28 list", 0x28), ("+0x34 list", 0x34)):
            head = struct.unpack_from("<I", hdr, off)[0]
            print()
            print("   walking M%s  head 0x%08X" % (label, head))
            p, i, seen = head, 0, set()
            while p and p not in seen and i < 24 and 0x08800000 <= p < 0x0A000000:
                seen.add(p)
                nd = c.read(p, 0x40)
                if not nd or len(nd) < 0x40:
                    break
                q = struct.unpack_from("<I", nd, 0x10)[0]
                sel = None
                if q and 0x08800000 <= q < 0x0A000000:
                    qb = c.read(q + 4, 2)
                    if qb and len(qb) == 2:
                        sel = struct.unpack("<h", qb)[0]
                idx = struct.unpack_from("<h", nd, 0x0C)[0]
                w20 = struct.unpack_from("<I", nd, 0x20)[0]
                print("      n%-2d 0x%08X def=0x%08X idx=%-6d w20=0x%08X SEL=%s"
                      % (i, p, struct.unpack_from("<I", nd, 0x0C)[0], idx, w20, sel))
                nxt = struct.unpack_from("<i", nd, 0x24)[0]
                if nxt == head:
                    break
                p = nxt
                i += 1
            if i == 0:
                print("      (empty)")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
