#!/usr/bin/env python3
"""psp-dissidia-fontscan.py -- find text stored as FONT CODES rather than ASCII.

WHY THIS SHAPE
    Dissidia's menu labels are textures and its *_help.bin bodies are parameter records, so the game
    does not store UI text as plain UTF-16LE the way Tag Team did. But it DOES ship a font
    (`libfont.prx`, `SYSTEM FONT T2/T3`, `sceLibFont`), and `menu_pk_loading_seq_0.bin` contains
    UTF-16LE whose codes go above 0x7F (0x89, 0x8A). That is the signature of a FONT CODE SPACE.

    A string table in font codes looks like: long runs of u16 values, high byte often 0, code values
    confined to a modest range, and NUL separators between entries. This script searches the whole
    payload for exactly that signature and ranks regions by how string-table-like they are, then
    reports the code range used.

    It is deliberately a DIFFERENT test from the earlier prose scan: prose requires spaces and
    letters, font-code text does not satisfy that and would have been missed.

USAGE
    python scripts/psp-dissidia-fontscan.py --package PACKAGE.BIN [--max-code 2048] [--min-run 12]
"""
import argparse, collections, os, struct, sys


def scan(buf, base, min_run, max_code, results, max_results=400):
    i = 0
    n = len(buf)
    while i + 1 < n:
        j = i
        codes = []
        while j + 1 < n:
            lo, hi = buf[j], buf[j + 1]
            if hi != 0 or lo == 0:
                break
            if lo > max_code:
                break
            codes.append(lo)
            j += 2
        if len(codes) >= min_run:
            results.append((base + i, len(codes), codes))
            if len(results) >= max_results:
                return
            i = j + 2
        else:
            i += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True)
    ap.add_argument("--max-code", type=int, default=2048)
    ap.add_argument("--min-run", type=int, default=12)
    ap.add_argument("--chunk-mib", type=int, default=16)
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()

    size = os.path.getsize(a.package)
    results = []
    CH = a.chunk_mib << 20
    pos = 0
    with open(a.package, "rb") as f:
        while pos < size and len(results) < 400:
            f.seek(pos)
            buf = f.read(CH + 64)
            if not buf:
                break
            scan(buf, pos, a.min_run, a.max_code, results)
            pos += CH
            print("   ...%.0f%%" % (100.0 * pos / size), end="\r", flush=True)
    print()

    print("runs of >= %d u16 font codes found: %d" % (a.min_run, len(results)))
    if not results:
        print("none -- text is not stored as a low-value u16 code stream anywhere in the payload")
        return

    results.sort(key=lambda r: -r[1])
    print("\n=== longest runs ===")
    for off, ln, codes in results[:a.top]:
        txt = "".join(chr(c) if 32 <= c < 127 else ("\\x%02X" % c) for c in codes)
        print("  @%-11d len=%-5d  %s" % (off, ln, txt[:78]))

    allc = collections.Counter()
    for _, _, codes in results:
        for c in codes:
            allc[c] += 1
    print("\n=== code space used across all runs (top 24) ===")
    for c, k in allc.most_common(24):
        print("   0x%04X  %-4s %6d" % (c, repr(chr(c)) if 32 <= c < 127 else ".", k))
    print("\ndistinct codes: %d" % len(allc))


if __name__ == "__main__":
    main()
