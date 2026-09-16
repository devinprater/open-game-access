#!/usr/bin/env python3
"""
oga-psp-diff.py — find which RAM regions change between two PSP dumps.

WHY: a visual-novel reader needs a location that tracks game state. The cheapest test
of "is there anything readable at all" is to ask WHICH BYTES MOVED between two known
different screens. Regions that move are candidates; regions that never move can be
ruled out wholesale.

⛔ THIS IS A SCREEN, NOT AN ANSWER. A region that changes between two steps can still
be:
  - animation (background art, particle effects, a blinking cursor),
  - audio or decompression scratch (very noisy, large, and worthless),
  - a frame counter or RNG.
So the tool reports each block's change *density* and its *value range*, and the
caller still has to verify against the screen. A block that is 100% changed is almost
always scratch or art, not state.

Usage:
  oga-psp-diff.py A.bin B.bin [--block 4096] [--base 0x08800000] [--top 40]
                               [--stable]      (invert: show UNCHANGED blocks)
"""
import argparse
import sys

RAM_BASE = 0x08800000


def blocks(buf, size):
    return [buf[i:i + size] for i in range(0, len(buf), size)]


def classify(a, b):
    """Return (changed_bytes, max_delta, distinct_values_estimate)."""
    n = min(len(a), len(b))
    changed = 0
    maxd = 0
    for i in range(n):
        d = a[i] ^ b[i]
        if d:
            changed += 1
            if d > maxd:
                maxd = d
    return changed, maxd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--block", type=int, default=4096)
    ap.add_argument("--base", default=hex(RAM_BASE))
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--stable", action="store_true",
                    help="show the blocks that did NOT change")
    args = ap.parse_args()

    base = int(args.base, 0)
    A = open(args.a, "rb").read()
    B = open(args.b, "rb").read()
    if len(A) != len(B):
        print(f"!! size mismatch: {len(A)} vs {len(B)}", file=sys.stderr)
        return 2

    print(f"A: {args.a}")
    print(f"B: {args.b}")
    print(f"both {len(A)} bytes; block size {args.block}; base 0x{base:08X}")
    print()

    rows = []
    for i, (ba, bb) in enumerate(zip(blocks(A, args.block), blocks(B, args.block))):
        changed, maxd = classify(ba, bb)
        rows.append((changed, i * args.block, maxd))

    total_changed = sum(r[0] for r in rows)
    print(f"total changed bytes: {total_changed} of {len(A)} "
          f"({100.0 * total_changed / len(A):.2f}%)")
    print()

    rows.sort(key=lambda r: -r[0])
    if args.stable:
        rows = [r for r in rows if r[0] == 0]
        print(f"=== UNCHANGED blocks ({len(rows)}) ===")
    else:
        rows = [r for r in rows if r[0] > 0]
        print(f"=== most-changed blocks (of {len(rows)} that moved) ===")

    for changed, off, maxd in rows[: args.top]:
        pct = 100.0 * changed / args.block
        # a block that is 100% changed with a large max delta is scratch or art
        hint = ""
        if pct > 95 and maxd > 128:
            hint = "  <- likely ART or SCRATCH"
        elif pct < 5 and maxd <= 16:
            hint = "  <- small, low-delta: candidate STATE"
        print(f"  0x{base + off:08X}  {changed:6d}/{args.block} ({pct:5.1f}%) maxd={maxd:3d}{hint}")

    # contiguity: a real state structure is a narrow run, not scattered singles
    moved = sorted(off for c, off, _ in rows if c > 0)
    if not args.stable and moved:
        runs = []
        start = prev = moved[0]
        for off in moved[1:]:
            if off == prev + args.block:
                prev = off
            else:
                runs.append((start, prev))
                start = prev = off
        runs.append((start, prev))
        print()
        print(f"=== contiguous moved runs: {len(runs)} ===")
        for s, e in runs[:12]:
            print(f"  0x{base + s:08X} .. 0x{base + e + args.block:08X}  "
                  f"({(e - s) // args.block + 1} blocks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
