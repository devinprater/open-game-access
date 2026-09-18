#!/usr/bin/env python
"""psp-msgids.py — count the message strings in PACKFILE.BIN and test the ELF id table against them.

WHY: this is the final join for character identity.
  - the ELF has a message-id table at 0x08A75538:  505 506 507 508 509 510 171 172 169 170 173 174
  - PACKFILE.BIN has a contiguous null-terminated UTF-16 string table (message strings)
  - IF the strings are indexed sequentially from a known base, then id N -> the Nth string, and the
    roster names fall out with their ids.

This script:
  1. finds the start of the contiguous string run,
  2. enumerates strings with their ordinal index and file offset,
  3. prints the strings at the indices suggested by the ELF id table (505, 506, 171, 169, 162, ...)
     so the hypothesis "id == ordinal" can be CONFIRMED OR FALSIFIED by reading real text.

Usage:
    python scripts/psp-msgids.py <PACKFILE.BIN> --start 0x4FADC --count 900
    python scripts/psp-msgids.py <PACKFILE.BIN> --start 0x4FADC --ids 505 506 507
"""
import argparse
import struct
import sys


def read_utf16(data, off, maxchars=400):
    out = []
    o = off
    while o < len(data) - 1 and len(out) < maxchars:
        c = struct.unpack_from("<H", data, o)[0]
        if c == 0:
            break
        out.append(chr(c) if (32 <= c < 0x2500 or c == 0x0A) else "?")
        o += 2
    return "".join(out), o + 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packfile")
    ap.add_argument("--start", required=True, help="offset where the string run begins")
    ap.add_argument("--count", type=int, default=600)
    ap.add_argument("--ids", nargs="*", type=int, default=[])
    ap.add_argument("--max-gap", type=int, default=8,
                    help="max bytes of padding tolerated between strings before stopping")
    args = ap.parse_args()

    data = open(args.packfile, "rb").read()
    off = int(args.start, 0)
    print(f"# {args.packfile}  {len(data):,} bytes")
    print(f"# enumerating message strings from 0x{off:X} (ordinal 0 at that offset)")
    print()

    strings = []
    o = off
    while len(strings) < args.count and o < len(data) - 2:
        s, end = read_utf16(data, o)
        if s:
            strings.append((len(strings), o, s))
            o = end
        else:
            # allow a little padding, otherwise we have left the table
            o += 2
            if len(strings) and o - strings[-1][1] > args.max_gap + len(strings[-1][2]) * 2 + 64:
                print(f"# stopped: gap too large at 0x{o:X}")
                break

    print(f"# enumerated {len(strings)} strings")
    print()

    if args.ids:
        print("=== ELF id-table values resolved against this ordinal numbering ===")
        print("   (hypothesis: message id == ordinal index into this table)")
        for want in args.ids:
            hit = next((s for s in strings if s[0] == want), None)
            if hit:
                print(f"   id {want:5d} -> 0x{hit[1]:08X}  {hit[2]!r}")
            else:
                print(f"   id {want:5d} -> OUT OF RANGE (table has {len(strings)})")
        print()

    # show where the roster names land, since that ties the table to the game
    print("=== notable strings with their ordinal ===")
    notable = {"Goku", "Kid Gohan", "Teen Gohan", "Gohan", "Piccolo", "Krillin", "Yamcha",
               "Tien", "Vegeta", "Frieza", "Cell", "Trunks", "Super Saiyan 3"}
    for i, so, s in strings:
        if s in notable:
            print(f"   [{i:4d}] 0x{so:08X}  {s!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
