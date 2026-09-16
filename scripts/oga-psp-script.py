#!/usr/bin/env python3
"""
oga-psp-script.py — locate a visual novel's script block and anything that tracks the
CURRENT line.

The reader problem for a VN is two-part:
  (a) where is the text?            -> a string scan finds the block
  (b) which line is on screen?      -> must be some pointer or index

This tool answers both, and is explicit about (b) failing. ⛔ A reader that guesses the
line from table order announces the WRONG line with full confidence, which is worse for
a blind player than saying nothing. So (b) is reported as found-or-not-found, never
inferred.

The strongest signal for (b) is a WORD INSIDE THE SCRIPT BLOCK'S ADDRESS RANGE sitting
in a different, small region of RAM — i.e. a pointer to the current line. A bare index
is much weaker (any counter fits), so the tool ranks pointers first.

Usage:
  oga-psp-script.py DUMP.bin [--lo 0x08AEA000] [--hi 0x08AF3000] [--base 0x08800000]
  oga-psp-script.py --compare A.bin B.bin [--lo ..] [--hi ..]
"""
import argparse
import struct
import sys

RAM_BASE = 0x08800000
RAM_SIZE = 0x01800000


def parse_addr(s):
    return int(s, 0)


def find_speaker_runs(buf, lo, hi):
    """Script strings carry a 1-char speaker code then the line. Report the block."""
    import re
    block = buf[lo - RAM_BASE:hi - RAM_BASE]
    # ⛔ RETURN ADDRESSES, NOT FILE OFFSETS. The window starts at RAM address `lo`,
    # whose file offset is (lo - base). Returning `m.start() + (lo - base)` produced
    # offsets like 0x002EAED6 which were then printed with an address prefix — wrong
    # by the RAM base, and it reads as "the addresses don't match the screen" rather
    # than "the arithmetic is wrong".
    runs = [(lo + m.start(), m.group())
            for m in re.finditer(rb"[\x20-\x7e]{12,}", block)]
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", nargs="?")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"))
    ap.add_argument("--lo", default="0x08AEA000")
    ap.add_argument("--hi", default="0x08AF3000")
    ap.add_argument("--base", default=hex(RAM_BASE))
    ap.add_argument("--limit", type=int, default=30)
    args = ap.parse_args()

    base = parse_addr(args.base)
    lo, hi = parse_addr(args.lo), parse_addr(args.hi)

    if not (base <= lo < hi <= base + RAM_SIZE):
        print(f"!! script window 0x{lo:08X}-0x{hi:08X} outside dump 0x{base:08X}-"
              f"0x{base + RAM_SIZE:08X}", file=sys.stderr)
        return 2

    if args.compare:
        A = open(args.compare[0], "rb").read()
        B = open(args.compare[1], "rb").read()
        if len(A) != len(B):
            print("!! size mismatch", file=sys.stderr); return 2
        print(f"=== comparing {args.compare[0]} vs {args.compare[1]} ===")
        # which 4-byte words hold a value inside the script window?
        def ptrs(buf):
            out = {}
            for off in range(0, len(buf) - 4, 4):
                v = struct.unpack_from("<I", buf, off)[0]
                if lo <= v < hi:
                    out[off] = v
            return out
        pa, pb = ptrs(A), ptrs(B)
        print(f"  A: {len(pa)} words point into the script window")
        print(f"  B: {len(pb)} words point into the script window")
        common = set(pa) & set(pb)
        moved = [off for off in common if pa[off] != pb[off]]
        print(f"  present in both: {len(common)};  CHANGED between A and B: {len(moved)}")
        if moved:
            print("\n  === pointers that MOVED with the screen (strong candidates) ===")
            for off in sorted(moved)[: args.limit]:
                print(f"    @0x{base + off:08X}   0x{pa[off]:08X} -> 0x{pb[off]:08X}")
        else:
            print("\n  no pointer into the script window changed between these two dumps.")
            print("  Either the line did not advance, or the current line is tracked some")
            print("  other way (an index, a copy, or nothing at all).")
        return 0

    if not args.dump:
        print("need DUMP.bin or --compare A B", file=sys.stderr); return 2

    buf = open(args.dump, "rb").read()
    if len(buf) != RAM_SIZE:
        print(f"!! dump is {len(buf)} bytes, expected {RAM_SIZE}; pass --base for a "
              f"narrower region", file=sys.stderr)
    print(f"dump : {args.dump}")
    print(f"script window: 0x{lo:08X} .. 0x{hi:08X}")
    print()

    runs = find_speaker_runs(buf, lo, hi)
    print(f"=== {len(runs)} text runs in the window; first {args.limit} ===")
    for off, raw in runs[: args.limit]:
        try:
            t = raw.decode("latin1")
        except Exception:
            t = repr(raw)
        print(f"  0x{off:08X}  {t[:84]!r}")
    print()

    # pointers into the window from OUTSIDE it — the current-line candidate
    print("=== words pointing INTO the window from elsewhere ===")
    hits = []
    for off in range(0, len(buf) - 4, 4):
        v = struct.unpack_from("<I", buf, off)[0]
        if lo <= v < hi:
            a = base + off
            if not (lo <= a < hi):                      # exclude the block itself
                hits.append((a, v))
    print(f"  {len(hits)} found")
    for a, v in hits[: args.limit]:
        print(f"    @0x{a:08X} -> 0x{v:08X}")
    if not hits:
        print("  NONE. The script is resident but nothing points at the current line")
        print("  from outside the block. A reader cannot be built on pointers alone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
