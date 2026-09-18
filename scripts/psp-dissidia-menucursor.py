#!/usr/bin/env python3
"""psp-dissidia-menucursor.py -- find how Dissidia's menus reference their text (cursor/selection).

WHY
    The text is located (Rule 81 in the skill): pause menu labels as UTF-16LE at 0x09D16A68+, each
    with a 1-byte format prefix. But a screen reader also needs the SELECTION, and a menu is normally
    (text pointer, count, selected index). A pointer scan of the surrounding region found 0 u32s
    pointing into 0x09D16A68-0x09D18000, so the reference is not a plain absolute pointer.

    Two possibilities to distinguish:
      A. the item table is a list of POINTERS stored somewhere else (search all readable RAM)
      B. the "prefix" byte before each string is the item's ID, and the engine finds text by INDEX,
         so the table is a strided array keyed by index.

    This script tests A over the whole mapped span, then characterises the string block's stride and
    the prefix bytes to test B.

USAGE
    python scripts/psp-dissidia-menucursor.py [--block 0x09D16A00 --end 0x09D17900]
"""
import argparse, importlib.util, os, struct, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--block", type=lambda x: int(x, 0), default=0x09D15000)
    ap.add_argument("--end", type=lambda x: int(x, 0), default=0x09D19000)
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08800000)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x0A000000)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    # ---- A. search ALL readable RAM for absolute pointers into the string block
    print("\n=== A. absolute pointers into 0x%08X..0x%08X, across all readable RAM ==="
          % (a.block, a.end))
    ptrs = []
    spans = d.map_readable(a.lo, a.hi, step=0x100000)
    CH = 1 << 20
    for lo, hi in spans:
        pos = lo
        while pos < hi:
            b = d.read(pos, min(CH, hi - pos))
            if b:
                for i in range(0, len(b) - 3, 4):
                    v = struct.unpack_from("<I", b, i)[0]
                    if a.block <= v < a.end:
                        ptrs.append((pos + i, v))
            pos += CH
            print("   ...0x%08X" % pos, end="\r", flush=True)
    print()
    print("absolute pointers found: %d" % len(ptrs))
    for ad, v in ptrs[:24]:
        print("   @0x%08X -> 0x%08X" % (ad, v))

    # ---- B. characterise the string block: stride and prefix bytes
    print("\n=== B. string block structure ===")
    blk = d.read(a.block, a.end - a.block)
    print("read %d bytes" % len(blk))
    # UTF-16LE entries: high byte 0, low byte printable
    entries = []
    i = 0
    while i < len(blk) - 1:
        j = i
        s = []
        while j < len(blk) - 1 and blk[j + 1] == 0 and 32 <= blk[j] < 127:
            s.append(chr(blk[j])); j += 2
        if len(s) >= 3:
            entries.append((a.block + i, "".join(s), blk[i - 1] if i > 0 else 0))
            i = j
        else:
            i += 1
    print("entries: %d" % len(entries))
    print("\n%-12s %-5s %-6s %s" % ("address", "len", "prefix", "text"))
    for ad, s, p in entries[:40]:
        print("0x%08X  %-5d 0x%02X   %s" % (ad, len(s), p, s[:66]))

    # stride between consecutive entry starts
    starts = [e[0] for e in entries]
    gaps = collections.Counter(starts[i + 1] - starts[i] for i in range(len(starts) - 1))
    print("\ngap between consecutive entries (top 12): %s" % gaps.most_common(12))

    # prefix byte histogram
    pc = collections.Counter(e[2] for e in entries)
    print("prefix byte histogram: %s" % pc.most_common(12))

    # Is any prefix a plausible small index 0..N?  Report the set in order of appearance.
    seq = [e[2] for e in entries[:40]]
    print("prefix sequence (first 40): %s" % " ".join("%02X" % x for x in seq))

    d.close()


if __name__ == "__main__":
    main()
