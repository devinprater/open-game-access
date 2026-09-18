#!/usr/bin/env python3
"""psp-find-api-holders.py -- map the function-pointer table at 0x08BA46F4 and find its holders.

WHY (doc section 88)
    FUN_000f71c8 -- the guarded INPUT QUERY wrapper -- has ZERO callers and is held in exactly one place:

        0x08BA46F4   -8:0x00000000  -4:0x00000000  +4:0x00000000  +8:PTR 0x088FB224

    `+8` being another code address in the same 0x088FBxxx band says this is a run of FUNCTION POINTERS --
    the pad region's API table. The menu obtains its input query from this table, which is why no
    cross-reference search ever found the menu reading input (sections 73-84).

THIS SCRIPT
    1. maps the table: read a wide window around 0x08BA46F4 and classify every word (code pointer /
       data pointer / zero / scalar), to find the table's extent and stride;
    2. searches ALL RAM for holders of the table region's addresses -- the same pointer search that found
       rec5 (s76), the delivery table (s83), and the query function (s88);
    3. reports clusters, since a structure holding several table entries is a strong consumer candidate.

    Pure reads: cannot freeze the emulator.

USAGE
    python scripts/psp-find-api-holders.py --region 0x40000
"""
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LO, HI = 0x08800000, 0x0A000000
CODE_LO, CODE_HI = 0x08804000, 0x08804000 + 0x003A6860   # the RX segment
TBL_CENTER = 0x08BA46F4


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", type=lambda x: int(x, 0), default=0x40000)
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x100)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()

    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def kind(v):
        if v is None:
            return "?"
        if CODE_LO <= v < CODE_HI:
            return "CODE vaddr 0x%08X" % (v - CODE_LO)
        if 0x08800000 <= v < 0x0A000000:
            return "DATA 0x%08X" % v
        if v == 0:
            return "zero"
        return "0x%08X" % v

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== 1. THE TABLE AROUND 0x%08X (span 0x%X) ===" % (TBL_CENTER, a.span))
    lo = TBL_CENTER - a.span // 2
    raw = c.read(lo, a.span)
    if not raw or len(raw) != a.span:
        print("   read failed")
        c.close()
        return 1
    tgt = []
    for off in range(0, a.span, 4):
        ad = lo + off
        v = struct.unpack_from("<I", raw, off)[0]
        mark = "  <== the holder of FUN_000f71c8" if ad == TBL_CENTER else ""
        print("   0x%08X = 0x%08X  %-22s%s" % (ad, v, kind(v), mark))
        if CODE_LO <= v < CODE_HI or 0x08800000 <= v < 0x0A000000:
            tgt.append(ad)
    print()
    print("   pointers found in the window: %d" % len(tgt))

    # --- 2. search for holders of the table's addresses ---
    search = {TBL_CENTER: "the FUN_000f71c8 slot"}
    # also search the first few pointer slots as table-base candidates
    for ad in tgt[:8]:
        search[ad] = "table slot 0x%08X" % ad
    # and for the VALUES the slots contain (a holder may store the fn directly)
    for off in range(0, a.span, 4):
        v = struct.unpack_from("<I", raw, off)[0]
        if CODE_LO <= v < CODE_HI:
            search.setdefault(v, "fn value 0x%08X (vaddr 0x%08X)" % (v, v - 0x08804000))

    print()
    print("=== 2. scanning all RAM for holders of %d targets ===" % len(search))
    hits = {v: [] for v in search}
    n = (HI - LO) // a.region
    for i in range(n):
        base = LO + i * a.region
        buf = c.read(base, a.region)
        if not buf or len(buf) != a.region:
            continue
        for j in range(0, a.region, 4):
            val = struct.unpack_from("<I", buf, j)[0]
            if val in hits:
                hits[val].append(base + j)
        print("   block %2d/%d" % (i + 1, n), end="\r", flush=True)
    print()

    print()
    print("=== 3. HOLDERS ===")
    allhits = set()
    for v, lab in search.items():
        ad = hits[v]
        allhits.update(ad)
        print()
        print("  0x%08X  (%s)  -> %d holder(s)" % (v, lab, len(ad)))
        for x in ad[:15]:
            ctx = []
            for d in (-8, -4, 4, 8):
                y = u32(x + d)
                ctx.append("%+d:%s" % (d, kind(y)))
            print("     0x%08X  %s" % (x, "  ".join(ctx)))
        if len(ad) > 15:
            print("     ... and %d more" % (len(ad) - 15))

    print()
    print("=== 4. CLUSTERS ===")
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
            print("      0x%08X" % ad)

    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
