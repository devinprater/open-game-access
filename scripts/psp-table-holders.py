#!/usr/bin/env python3
"""psp-table-holders.py -- who HOLDS the delivery table at 0x09FFF604 / 0x09FFF630?

WHY (doc section 83)
    Section 83 found a heap record table holding all three converted-input pointers together with
    same-subsystem function pointers:

        0x09FFF604 = 0x09EDA400   (port1 converted object)
        0x09FFF610 = 0x09EDA460   (port0 converted object)
        0x09FFF630 = 0x09EDA3A0   (the pad object)
        interleaved with fn pointers 0x088FB070, 0x088FB7CC, 0x088FB768, 0x088FB930

    It is a dispatch table, and whoever HOLDS IT is the dispatcher or the consumer. This is the same
    pointer search that found rec5 (s76) and the table itself (s83), run one level further out.

METHOD
    Pure reads -- no breakpoints, no input, so it cannot freeze the emulator. Scan all readable RAM for
    32-bit words equal to:
        * 0x09FFF604   (the port1 record)
        * 0x09FFF630   (the port0 record)
        * 0x09FFF5F0   (a little before, in case a base pointer points at the block start)
        * the table's code pointers 0x088FB070, 0x088FB7CC, 0x088FB768, 0x088FB930
    Then report where each holder lives and what surrounds it, in clusters.

USAGE
    python scripts/psp-table-holders.py --region 0x40000
"""
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LO, HI = 0x08800000, 0x0A000000

TARGETS = {
    0x09FFF604: "delivery-table record A (port1)",
    0x09FFF630: "delivery-table record B (pad object)",
    0x09FFF5F0: "near table start",
    0x09FFF610: "delivery-table record A+0xC",
    0x088FB070: "table fn ptr 1",
    0x088FB7CC: "table fn ptr 2",
    0x088FB768: "table fn ptr 3",
    0x088FB930: "table fn ptr 4",
}


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

    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    print("=== who HOLDS the delivery table (doc section 83) ===")
    print("targets:")
    for v, lab in TARGETS.items():
        print("   0x%08X  %s" % (v, lab))

    print()
    print("=== live check of the table block ===")
    for off in range(0, 0x50, 4):
        ad = 0x09FFF5F0 + off
        print("   0x%08X = 0x%08X" % (ad, u32(ad) or 0))

    print()
    print("=== scanning 0x%08X-0x%08X for holders ===" % (LO, HI))
    hits = {v: [] for v in TARGETS}
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

    print()
    print("=== holders ===")
    allhits = set()
    for v, lab in TARGETS.items():
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

    print()
    print("=== CLUSTERS ===")
    srt = sorted(allhits)
    clusters, cur = [], []
    for ad in srt:
        if cur and ad - cur[-1] <= 0x80:
            cur.append(ad)
        else:
            if cur:
                clusters.append(cur)
            cur = [ad]
    if cur:
        clusters.append(cur)
    for cl in clusters:
        print("   cluster 0x%08X..0x%08X  %d hit(s)" % (cl[0], cl[-1], len(cl)))
        for ad in cl:
            v = u32(ad) or 0
            print("      0x%08X holds 0x%08X  (%s)" % (ad, v, TARGETS.get(v, "?")))

    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
