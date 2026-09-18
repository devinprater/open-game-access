#!/usr/bin/env python3
"""psp-dissidia-seqtext.py -- read the UTF-16LE text out of Dissidia's "sequence" resources.

DISCOVERY
    menu_pk_loading_seq_0.bin (7248 bytes, entropy only 2.881 -- very structured) begins:

        80 80 80 ff | 74 00 70 00 75 00 71 00 | 00 00 ... | ff ff ff ff ...
                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^  this is UTF-16LE "tpuq"

        ... 89 00 68 00 8a 00 69 00 | 00 00 ...

    So the sequence resources carry UTF-16LE strings. Note the glyph codes at/above 0x80 (0x89, 0x8a)
    which are NOT ASCII -- consistent with a font that maps codes above 0x7F, exactly like the "SYSTEM
    FONT T2/T3" and `sceLibFont` strings in the EBOOT.

    These files also contain colour bytes (0x80 80 80 ff, 0x78 64 b0 ff, 0x65 59 99 ff are RGBA)
    and float pairs, so they are layout+text records rather than plain string tables.

WHAT THIS SCRIPT DOES
    Dumps every UTF-16LE run (including codes above 0x7F) so the actual menu strings become visible,
    and reports the distinct code values used, which is the font's code space.

USAGE
    python scripts/psp-dissidia-seqtext.py FILE [--min-len 2] [--all-runs]
"""
import argparse, collections, os, re, struct, sys


def utf16_all_runs(buf, minlen=2, terminator=0x0000):
    """Runs of u16 where the LOW byte is printable-or-defined and the HIGH byte is 0."""
    out = []
    i = 0
    n = len(buf)
    while i + 1 < n:
        j = i
        s = []
        while j + 1 < n:
            lo, hi = buf[j], buf[j + 1]
            if hi != 0:
                break
            if lo == 0:
                break
            s.append(lo)
            j += 2
        if len(s) >= minlen:
            out.append((i, s))
            i = j + 2
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--min-len", type=int, default=2)
    ap.add_argument("--all-runs", action="store_true")
    ap.add_argument("--limit", type=int, default=120)
    a = ap.parse_args()

    d = open(a.path, "rb").read()
    print("=== %s : %d bytes ===" % (os.path.basename(a.path), len(d)))

    runs = utf16_all_runs(d, a.min_len)
    print("UTF-16LE runs: %d" % len(runs))

    codes = collections.Counter()
    for _, s in runs:
        for c in s:
            codes[c] += 1
    print("distinct code points used: %d" % len(codes))

    def render(s):
        return "".join(chr(c) if 32 <= c < 127 else ("\\x%02X" % c) for c in s)

    print("\n=== runs ===")
    for i, (off, s) in enumerate(runs[:a.limit]):
        raw = "".join(chr(c) if 32 <= c < 127 else "." for c in s)
        print("  @%-6d len=%-4d  %-46s  %s" % (off, len(s), raw[:46], render(s)[:46]))

    print("\n=== most common code points ===")
    for c, k in codes.most_common(24):
        ch = chr(c) if 32 <= c < 127 else "."
        print("   0x%02X  %-4s %5d" % (c, repr(ch), k))


if __name__ == "__main__":
    main()
