#!/usr/bin/env python3
"""psp-find-holders.py -- who HOLDS the pad service record or its data pointer?

WHY (doc section 75)
    Section 75 established:
      * the pad's service record is at 0x08B96B88 = {next=0x08B96B94, data=0x09EF72C4, fn=FUN_000f409c}
      * the record's `fn` is a virtual/lifecycle hook -- the menu reaches the pad through the RECORD,
        not through the pad object (which is why section 73 found 0 of 21 cross-references)
      * so a menu-side object that HOLDS this record (or its data pointer) is the consumer to find

    A pointer to a service record is exactly the kind of thing an object stores in a field. So: scan all
    readable RAM for 32-bit words equal to the record address, any interior address in the record, or the
    data pointer -- then report WHERE each hit lives and what structure surrounds it.

    This is a pure read; no watchpoints, no input. It is the cheapest form of the question.

USAGE
    python scripts/psp-find-holders.py
"""
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LO, HI = 0x08800000, 0x0A000000
REGION = 0x40000

# targets discovered in sections 74/75
TARGETS = {
    "service record base": 0x08B96B88,
    "record +0x00 (next)": 0x08B96B94,
    "record +0x04 (data)": 0x09EF72C4,   # NOTE: this is ALSO the value; search the value
    "record +0x08 (fn)":   0x088F809C,
    "pad object":          0x09EDA3A0,
}
# the pad descriptor's data pointer VALUE: the record's +0x04 field
PAD_DATA_VALUE = 0x09EF72C4
PAD_FN_VALUE = 0x088F809C


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", type=lambda x: int(x, 0), default=REGION)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== who HOLDS the pad service record (doc section 75) ===")
    print("targets:")
    for k, v in TARGETS.items():
        print("   %-22s 0x%08X" % (k, v))
    # note: the record's +0x04 field lives at 0x08B96B8C and holds 0x09EF72C4.
    # We search for POINTERS TO the record (0x08B96B88), to the record interior, and for the
    # data pointer VALUE (0x09EF72C4) which some object may hold directly.
    search_values = {
        0x08B96B88: "ptr to pad service record base",
        0x08B96B8C: "ptr to record +0x04",           # points at the data field
        0x09EF72C4: "the pad data pointer VALUE",
        0x088F809C: "the pad fn VALUE",
        0x09EDA3A0: "ptr to the pad object",
    }
    # read live values to confirm
    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    print()
    print("=== live confirmation of the record ===")
    for off in (0, 4, 8):
        print("   record +0x%X (addr 0x%08X) = 0x%08X" % (off, 0x08B96B88 + off,
                                                           u32(0x08B96B88 + off) or 0))

    print()
    print("=== scanning 0x%08X-0x%08X in 0x%X blocks for these values ===" % (LO, HI, a.region))
    hits = {v: [] for v in search_values}
    nblk = (HI - LO) // a.region
    for i in range(nblk):
        base = LO + i * a.region
        raw = c.read(base, a.region)
        if not raw or len(raw) != a.region:
            continue
        for j in range(0, a.region, 4):
            v = struct.unpack_from("<I", raw, j)[0]
            if v in hits:
                hits[v].append(base + j)
        print("  block %2d/%d" % (i + 1, nblk), end="\r", flush=True)
    print()

    print()
    print("=== results ===")
    for v, label in search_values.items():
        addrs = hits[v]
        print()
        print("  %s  0x%08X  ->  %d hit(s)" % (label, v, len(addrs)))
        for ad in addrs[:20]:
            # classify the neighbourhood: what structure surrounds this hit?
            ctx = []
            for d in (-8, -4, 4, 8):
                x = u32(ad + d)
                if x is None:
                    continue
                if 0x08800000 <= x < 0x0A000000:
                    ctx.append("%+d:PTR 0x%08X" % (d, x))
                else:
                    ctx.append("%+d:0x%08X" % (d, x))
            print("     0x%08X   %s" % (ad, "  ".join(ctx)))
        if len(addrs) > 20:
            print("     ... and %d more" % (len(addrs) - 20))

    # group: are any hits inside a tight cluster (i.e. an object's field block)?
    allhits = sorted({ad for vv in hits.values() for ad in vv})
    print()
    print("=== cluster analysis (an object storing several of these is a strong consumer) ===")
    clusters, cur = [], []
    for ad in allhits:
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
            print("   cluster at 0x%08X: %d hit(s) -- %s" % (cl[0], len(cl), [hex(x) for x in cl]))
    else:
        print("   no cluster holds more than one of these values")

    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
