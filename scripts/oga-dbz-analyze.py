#!/usr/bin/env python3
"""Offline analyzer for a DBZ: Attack of the Saiyans RAM dump.

WHY THIS EXISTS
---------------
Every question about this game's structures was costing a full probe run: ~5
minutes to boot a 128 MB ROM and drive 12,000 frames of input, and each run could
answer exactly one question because the formatting was compiled in.

The probe now writes raw RAM with `DBZ_DUMP=<file>`. This script reads that dump
and answers questions in milliseconds, so structural work stops fighting the
emulator's runtime and becomes ordinary data analysis.

Usage:
    oga-dbz-analyze.py <dump.bin> [--rec 0x020CD754] [--stride 0x24C] [--n 8]

Dump format: one '# ...' header line, then raw bytes starting at the base named
in that header.
"""
import argparse
import struct
import sys
from collections import Counter


def load(path):
    raw = open(path, "rb").read()
    nl = raw.find(b"\n")
    header = raw[:nl].decode("ascii", "replace")
    base = 0
    for tok in header.split():
        if tok.startswith("base="):
            base = int(tok.split("=", 1)[1], 0)
    return base, raw[nl + 1:], header


def u8(d, a):
    return d[a] if 0 <= a < len(d) else None


def u16(d, a):
    return struct.unpack_from("<H", d, a)[0] if 0 <= a <= len(d) - 2 else None


def u32(d, a):
    return struct.unpack_from("<I", d, a)[0] if 0 <= a <= len(d) - 4 else None


def cstr(d, a, maxlen=24):
    if not (0 <= a < len(d)):
        return None
    e = a
    while e < len(d) and e - a < maxlen and d[e] != 0:
        e += 1
    seg = d[a:e]
    if not seg or not all(32 <= c < 127 for c in seg):
        return None
    return seg.decode("ascii")


def cmd_records(d, base, rec, stride, n, nameoff):
    """Dump each record as labelled rows plus a column profile."""
    print(f"=== {n} records, base 0x{rec:08X}, stride 0x{stride:X} ===")
    # ⛔ NAME ADDRESS IS NOT THE RECORD BASE. The measured facts are the NAME
    # addresses (0x020CD774 for Goku). The record base is name - 0x20. Passing the
    # name address as `--rec` shifts every field by 0x20 and makes ASCII read as
    # u16 garbage, which looks like "the structure is not there" rather than an
    # argument mistake. Default to the TRUE record base and label both.
    nameoff = nameoff

    for r in range(n):
        b = rec - base + r * stride
        nm = cstr(d, b + nameoff)
        print(f"\n--- rec{r}  ram 0x{rec + r*stride:08X}  name={nm!r} ---")
        for o in range(0, stride, 16):
            row = d[b + o:b + o + 16]
            if not any(row):
                continue
            hexs = " ".join(f"{x:02X}" for x in row)
            asc = "".join(chr(x) if 32 <= x < 127 else "." for x in row)
            print(f"  +{o:03X}  {hexs}  |{asc}|")

        # u16 column profile: which offsets hold NON-ZERO and DIFFER per record
        print("  u16 fields:", end="")
        shown = 0
        for o in range(0, stride - 1, 2):
            v = u16(d, b + o)
            if v:
                print(f" +{o:03X}={v}", end="")
                shown += 1
            if shown >= 14:
                break
        print()

    # which u16 offsets vary ACROSS records (a per-character stat would)
    print(f"\n=== u16 offsets that DIFFER across all {n} records ===")
    for o in range(0, stride - 1, 2):
        vals = [u16(d, rec - base + r * stride + o) for r in range(n)]
        if all(v is not None for v in vals) and len(set(vals)) == n and all(vals):
            print(f"  +{o:03X}  {vals}")

    # u32 version
    print(f"\n=== u32 offsets that DIFFER across all {n} records ===")
    for o in range(0, stride - 3, 4):
        vals = [u32(d, rec - base + r * stride + o) for r in range(n)]
        if all(v is not None for v in vals) and len(set(vals)) == n and all(vals):
            print(f"  +{o:03X}  {vals}")


def cmd_strings(d, base):
    """All ASCII runs in the dump — names are the best structure oracle."""
    print("=== ASCII strings (len>=4) ===")
    n = 0
    a = 0
    while a < len(d):
        if 32 <= d[a] < 127:
            s = a
            while a < len(d) and 32 <= d[a] < 127:
                a += 1
            if a - s >= 4:
                print(f"  0x{base + s:08X}  {d[s:a].decode('ascii','replace')!r}")
                n += 1
        else:
            a += 1
    print(f"  -- {n} string(s)")


def cmd_refs(d, base, target):
    """Who points at a given address (as a 4-byte little-endian word)?"""
    pat = struct.pack("<I", target)
    print(f"=== references to 0x{target:08X} ===")
    hits = 0
    i = d.find(pat)
    while i != -1:
        if i % 4 == 0:
            print(f"  0x{base + i:08X}")
            hits += 1
        i = d.find(pat, i + 1)
    print(f"  -- {hits} aligned reference(s)")


def cmd_scan(d, base, stride, n, lo, hi):
    """Find bases whose value repeats across stride-spaced records."""
    print(f"=== scanning 0x{base+lo:08X}..0x{base+hi:08X} for stride 0x{stride:X} ===")
    found = []
    for a in range(lo, min(hi, len(d) - stride * n - 4)):
        v0 = u32(d, a)
        if not v0:
            continue
        vals = [u32(d, a + i * stride) for i in range(n)]
        if all(vals) and len(set(vals)) == n:
            found.append((a, vals))
    print(f"  {len(found)} candidate(s)")
    for a, vals in found[:20]:
        print(f"    0x{base + a:08X}  {vals}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--rec", type=lambda s: int(s, 0), default=0x020CD754)
    ap.add_argument("--stride", type=lambda s: int(s, 0), default=0x24C)
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--strings", action="store_true")
    ap.add_argument("--refs", type=lambda s: int(s, 0))
    ap.add_argument("--scan", nargs=2, type=lambda s: int(s, 0))
    ap.add_argument("--nameoff", type=lambda s: int(s, 0), default=0x20,
                    help="offset of the name field inside each record")
    args = ap.parse_args()

    base, d, header = load(args.dump)
    print(f"dump: {args.dump}")
    print(f"  {header}")
    print(f"  {len(d):,} bytes of ram from 0x{base:08X}\n")

    if args.strings:
        cmd_strings(d, base)
    elif args.refs is not None:
        cmd_refs(d, base, args.refs)
    elif args.scan:
        cmd_scan(d, base, args.stride, args.n, args.scan[0], args.scan[1])
    else:
        cmd_records(d, base, args.rec, args.stride, args.n, args.nameoff)


if __name__ == "__main__":
    main()
