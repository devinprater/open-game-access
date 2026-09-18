#!/usr/bin/env python3
"""psp-dissidia-package.py -- inspect Dissidia's PACKAGE.BIN / PACKAGE_INFO.BIN archive pair.

WHY THIS FIRST
    Dissidia ships its data as PACKAGE.BIN (660 MB) plus PACKAGE_INFO.BIN (69 KB). An archive that
    comes with a separate INDEX is the best possible starting point: the index almost always names
    the entries, which turns "reverse engineer a container" into "read a table of names". That is the
    same lesson as the engine's own debug strings in the Tag Team work -- read the game's own labels
    before inventing anything.

WHAT IT DOES
    * reports both file sizes and the first bytes of each
    * scans the index for readable strings (ASCII and UTF-16LE) so entry names become visible
    * prints the first N words of the index as both u32 and plausible offset/length pairs

USAGE
    python scripts/psp-dissidia-package.py --info FILE --package FILE
"""
import argparse, os, re, struct, sys


def read(path, n=None, off=0):
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(n) if n is not None else f.read()


def strings_in(buf, minlen=4):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, buf):
        out.append((m.start(), m.group().decode("latin1")))
    return out


def utf16_in(buf, minlen=4):
    out = []
    i = 0
    n = len(buf)
    while i < n - 1:
        j = i
        s = []
        while j < n - 1 and 0x20 <= buf[j] <= 0x7E and buf[j + 1] == 0:
            s.append(chr(buf[j])); j += 2
        if len(s) >= minlen:
            out.append((i, "".join(s)))
            i = j
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--info", required=True)
    ap.add_argument("--package")
    ap.add_argument("--limit", type=int, default=60)
    a = ap.parse_args()

    ib = read(a.info)
    print("=== %s ===" % os.path.basename(a.info))
    print("size: %d bytes" % len(ib))
    print("first 64 bytes hex: %s" % ib[:64].hex())

    w = struct.unpack_from("<16I", ib, 0)
    print("first 16 words u32: %s" % " ".join(str(x) for x in w))
    print()

    asc = strings_in(ib, 4)
    print("ASCII strings: %d" % len(asc))
    for off, s in asc[:a.limit]:
        print("   @0x%06X  %s" % (off, s[:100]))

    u16 = utf16_in(ib, 4)
    if u16:
        print()
        print("UTF-16LE strings: %d" % len(u16))
        for off, s in u16[:a.limit]:
            print("   @0x%06X  %s" % (off, s[:100]))

    if a.package:
        pb = read(a.package, 64)
        print()
        print("=== %s ===" % os.path.basename(a.package))
        print("size: %d bytes" % os.path.getsize(a.package))
        print("first 64 bytes hex: %s" % pb.hex())
        pw = struct.unpack_from("<16I", pb, 0)
        print("first 16 words u32: %s" % " ".join(str(x) for x in pw))
        # look for a plausible file count / offset table in the first words
        for i, v in enumerate(pw[:8]):
            if 0 < v < 2000000:
                print("   word[%d] = %-10d  plausible entry count" % (i, v))
            elif 0x100 <= v < 0x70000000:
                print("   word[%d] = 0x%08X  plausible offset/size" % (i, v))


if __name__ == "__main__":
    main()
