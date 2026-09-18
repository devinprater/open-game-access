#!/usr/bin/env python3
"""psp-find-state-holders.py -- who else holds the CONVERTED-INPUT objects' pointers?

WHY (doc section 82)
    Four independent measurements (sections 73, 77, 81, 82) all failed to attach the menu to the input
    path: the pad object is unreferenced by the menu, the service table is never walked, the converted
    state has only pad-internal readers, and the input driver's 10 callers all score zero for menu
    indicators.

    That leaves the COPIED-STATE-BLOCK hypothesis: the pad layer writes a struct the menu also holds, so
    neither shares a static and neither reads the other's object.

    The converted-input objects at `pad + 0xC0` and `pad + 0x110` (stride 0x50) carry POINTER fields:
        +0x34 = 0x08BA46E8   (same in both ports)
        +0x40 = 0x09EDA3A0 / 0x09EDA400   (port0 holds the pad object itself!)
        +0x4C = 0x08BB2900 / 0x08BB2AC0
    One of those may be the shared state block. This script:
      1. reads each port's pointer fields live,
      2. searches ALL readable RAM for other 32-bit words holding the SAME values,
      3. reports where each holder lives and what surrounds it -- a holder inside a menu-side structure
         would be the consumer, and section 76 showed this method works.

USAGE
    python scripts/psp-find-state-holders.py --region 0x40000
"""
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
LO, HI = 0x08800000, 0x0A000000
PORT0, PORT1 = 0x00C0, 0x0110
# pointer field offsets to inspect, per port
PTR_OFFS = [0x34, 0x40, 0x4C]

FUNCS = [(0x0024aee0, 0x0024c200, "FUN_0024aee0 (RENDER LOOP)"),
         (0x0024932c, 0x00249700, "FUN_0024932c (node mgr)"),
         (0x0025468c, 0x00254750, "FUN_0025468c (def lookup)"),
         (0x002489d0, 0x00248c00, "FUN_002489d0 (menu mgr ctor)"),
         (0x002dbf50, 0x002dc400, "FUN_002dbf50 (boot)"),
         (0x0025595c, 0x00256198, "FUN_0025595c (render writer)")]


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", type=lambda x: int(x, 0), default=0x40000)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print("pad object 0x%08X" % PAD)

    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    # --- 1. collect the target pointer VALUES from both ports ---
    print()
    print("=== 1. the converted-input objects' pointer fields ===")
    targets = {}
    for label, base in (("port0 pad+0xC0", PAD + PORT0), ("port1 pad+0x110", PAD + PORT1)):
        print("  --- %s @ 0x%08X ---" % (label, base))
        for off in PTR_OFFS:
            v = u32(base + off)
            if v is None:
                continue
            tag = "PTR" if 0x08800000 <= v < 0x0A000000 else "value"
            print("     +0x%02X = 0x%08X %s" % (off, v, tag))
            if 0x08800000 <= v < 0x0A000000:
                targets[v] = "port%s +0x%02X" % (base - PAD, off)
    # also the driver's own address as a sanity target
    targets[PAD] = "the pad object itself"

    print()
    print("=== values to search for: %d ===" % len(targets))
    for v, lab in targets.items():
        print("   0x%08X  (%s)" % (v, lab))

    # --- 2. scan all RAM for holders ---
    print()
    print("=== 2. scanning all RAM for other holders ===")
    hits = {v: [] for v in targets}
    nblk = (HI - LO) // a.region
    for i in range(nblk):
        base = LO + i * a.region
        raw = c.read(base, a.region)
        if not raw or len(raw) != a.region:
            continue
        for j in range(0, a.region, 4):
            val = struct.unpack_from("<I", raw, j)[0]
            if val in hits:
                hits[val].append(base + j)
        print("  block %2d/%d" % (i + 1, nblk), end="\r", flush=True)
    print()

    # --- 3. report ---
    print()
    print("=== 3. HOLDERS ===")
    allhits = set()
    for v, lab in targets.items():
        addrs = hits[v]
        allhits.update(addrs)
        print()
        print("  0x%08X  (%s)  -> %d holder(s)" % (v, lab, len(addrs)))
        for ad in addrs[:25]:
            ctx = []
            for d in (-8, -4, 4, 8):
                x = u32(ad + d)
                if x is None:
                    continue
                ctx.append("%+d:%s" % (d, ("PTR 0x%08X" % x) if 0x08800000 <= x < 0x0A000000 else "0x%08X" % x))
            print("     0x%08X   %s" % (ad, "  ".join(ctx)))
        if len(addrs) > 25:
            print("     ... and %d more" % (len(addrs) - 25))

    # --- 4. clusters: an object holding several of these is a strong candidate ---
    print()
    print("=== 4. CLUSTERS (a structure holding 2+ of these values) ===")
    srt = sorted(allhits)
    clusters, cur = [], []
    for ad in srt:
        if cur and ad - cur[-1] <= 0x80:
            cur.append(ad)
        else:
            if len(cur) >= 2:
                clusters.append(cur)
            cur = [ad]
    if len(cur) >= 2:
        clusters.append(cur)
    if clusters:
        for cl in clusters:
            vals = []
            for ad in cl:
                vals.append("0x%08X@0x%08X" % (u32(ad) or 0, ad))
            print("   cluster 0x%08X..0x%08X  %d hit(s):" % (cl[0], cl[-1], len(cl)))
            for s in vals:
                print("      %s" % s)
    else:
        print("   none -- no structure holds more than one of these pointers")

    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
