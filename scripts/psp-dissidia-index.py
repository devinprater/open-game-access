#!/usr/bin/env python3
"""psp-dissidia-index.py -- decode Dissidia's PACKAGE_INFO.BIN ("packm") entry index.

WHAT WE KNOW SO FAR (measured, not assumed)
    PACKAGE.BIN begins:
        02 00 00 00 | 01 00 00 00 | 75 73 00 00 | 05 00 00 00 | "ULUS10437" ...
    i.e. it self-identifies with the game ID. So PACKAGE.BIN is the data payload.

    PACKAGE_INFO.BIN (68,908 bytes) begins:
        18 02 | 09 20 | "packm" | 16 00 00 00 | 00 00 00 00 | ...
    `"packm"` is an ASCII magic. An index that NAMES its content is the fastest possible route into
    a container -- the same lesson as reading the engine's own debug strings.

WHAT THIS SCRIPT DOES
    * locates the "packm" magic and reports the bytes around it
    * tries the obvious layout: a count followed by fixed-size records, and reports which stride
      makes the records' offset/length fields monotonic and in-range for the 660 MB payload
    * dumps the resulting table so entries can be identified by extracting and inspecting them

USAGE
    python scripts/psp-dissidia-index.py --info FILE --package FILE [--stride N] [--count N]
"""
import argparse, os, struct, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--info", required=True)
    ap.add_argument("--package")
    ap.add_argument("--stride", type=int)
    ap.add_argument("--count", type=int)
    ap.add_argument("--limit", type=int, default=40)
    a = ap.parse_args()

    ib = open(a.info, "rb").read()
    pkg_size = os.path.getsize(a.package) if a.package else None

    print("info size: %d" % len(ib))
    if pkg_size:
        print("package size: %d" % pkg_size)

    # 1) find the magic and show context
    m = ib.find(b"packm")
    print('\n"packm" magic at 0x%X' % m)
    print("  bytes 0x00-0x40: %s" % ib[:0x40].hex())
    if m > 0:
        print("  bytes before magic: %s" % ib[max(0, m - 16):m].hex())
    print("  bytes after magic : %s" % ib[m + 5:m + 5 + 32].hex())

    # 2) candidate header words
    print("\nfirst 24 u32:")
    for i in range(0, min(96, len(ib)), 4):
        v = struct.unpack_from("<I", ib, i)[0]
        note = ""
        if pkg_size and 0 < v < pkg_size:
            note = "  <-- plausible offset into PACKAGE.BIN"
        elif 0 < v < 100000:
            note = "  (small: count/flag)"
        print("   +0x%03X  %10d  0x%08X%s" % (i, v, v, note))

    # 3) if a stride and count are given, dump the table
    if a.stride and a.count:
        print("\n=== table: %d records of %d bytes from 0x0 ===" % (a.count, a.stride))
        for i in range(min(a.count, a.limit)):
            base = i * a.stride
            if base + a.stride > len(ib):
                break
            words = struct.unpack_from("<%dI" % (a.stride // 4), ib, base)
            print("  [%3d] " % i + " ".join("%10d" % w for w in words[:4]))
            if pkg_size:
                off, ln = words[0], words[1]
                ok = "  offset OK" if 0 <= off < pkg_size else "  offset OUT OF RANGE"
                ok2 = "  len OK" if pkg_size and 0 <= ln <= pkg_size - off else "  len OUT OF RANGE"
                print("        off=%d len=%d%s%s" % (off, ln, ok, ok2))

    # 4) heuristic: find the stride that makes a (off,len) pair plausible for ALL records
    if pkg_size and not a.stride:
        print("\n=== searching for a plausible record stride (off,len monotonic, in range) ===")
        best = []
        for stride in (8, 12, 16, 20, 24, 32):
            for start in (0, 4, 8, 16, 24, 32, 0x40, 0x80):
                n = (len(ib) - start) // stride
                if n < 10:
                    continue
                good = 0
                prev_off = -1
                mono = 0
                for i in range(n):
                    off, ln = struct.unpack_from("<II", ib, start + i * stride)
                    if 0 <= off < pkg_size and 0 <= ln <= pkg_size - off:
                        good += 1
                        if off >= prev_off:
                            mono += 1
                        prev_off = off
                if good > n * 0.9 and mono > n * 0.9:
                    best.append((stride, start, n, good, mono))
        if best:
            for stride, start, n, good, mono in best[:10]:
                print("   stride %-3d start 0x%-3X records %-6d valid %-6d monotonic %-6d"
                      % (stride, start, n, good, mono))
        else:
            print("   none found -- the index is probably not a simple (off,len) table")


if __name__ == "__main__":
    main()
