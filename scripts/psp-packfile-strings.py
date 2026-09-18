#!/usr/bin/env python
"""psp-packfile-strings.py — read the UTF-16 string sequence at an offset in PACKFILE.BIN.

WHY: the roster names were located in PACKFILE.BIN (e.g. 'Kid Gohan' at 0x4FADC, 4-byte aligned).
Reading the CONTIGUOUS string sequence there reveals the roster block and, crucially, whatever
INDEX sits immediately before it -- archives typically store {id, offset, length} entries, and those
ids are the message ids the ELF's id table (0x08A75538: 505,506,...) refers to.

Usage:
    python scripts/psp-packfile-strings.py <PACKFILE.BIN> 0x4FADC --count 60
    python scripts/psp-packfile-strings.py <PACKFILE.BIN> 0x4FADC --before 400
"""
import argparse
import struct
import sys


def read_utf16(data, off, maxchars=200):
    out = []
    o = off
    while o < len(data) - 1 and len(out) < maxchars:
        c = struct.unpack_from("<H", data, o)[0]
        if c == 0:
            break
        out.append(chr(c) if 32 <= c < 0x2500 or c == 0x0A else "?")
        o += 2
    return "".join(out), o + 2      # +2 to step past the terminator


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packfile")
    ap.add_argument("offset", help="hex or decimal offset")
    ap.add_argument("--count", type=int, default=40, help="strings to read forward")
    ap.add_argument("--before", type=int, default=0, help="also hex-dump this many bytes before")
    ap.add_argument("--stride-check", action="store_true",
                    help="report the gap between consecutive strings")
    args = ap.parse_args()

    data = open(args.packfile, "rb").read()
    off = int(args.offset, 0)
    print(f"# {args.packfile}  {len(data):,} bytes")
    print(f"# reading forward from 0x{off:X}")

    if args.before:
        print()
        print(f"=== {args.before} bytes BEFORE the string (u32 words, aligned down) ===")
        start = max(0, off - args.before)
        start -= start % 4
        for o in range(start, off, 4):
            v = struct.unpack_from("<I", data, o)[0]
            note = ""
            if 0x08800000 <= v < 0x0A000000:
                note = f"  -> ptr 0x{v:08X}"
            elif 0 < v < 0x10000:
                note = f"  (small: {v})"
            print(f"  0x{o:08X}  {v:>12}  0x{v:08X}{note}")

    print()
    print("=== UTF-16 strings forward from here ===")
    o = off
    prev_end = None
    for i in range(args.count):
        if o >= len(data) - 2:
            break
        s, end = read_utf16(data, o)
        if not s:
            # skip a run of nulls
            o += 2
            continue
        gap = ""
        if prev_end is not None and args.stride_check:
            gap = f"   (gap {o - prev_end} bytes)"
        print(f"  [{i:3d}] 0x{o:08X}  {s!r}{gap}")
        prev_end = end
        o = end
    return 0


if __name__ == "__main__":
    sys.exit(main())
