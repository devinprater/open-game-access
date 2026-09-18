#!/usr/bin/env python
"""psp-packfile-find.py — locate known UTF-16 strings inside a PSP game's PACKFILE.BIN.

WHY: this closes character identity. Established (see the doc):
  - the game stores MESSAGE IDS, not string pointers
  - the id tables are in the ELF (0x08A75538: 505 506 507 ...), but the TEXT is NOT
  - the roster names observed in RAM at 0x08C85C82 are outside every ELF PT_LOAD segment,
    so the text is loaded at runtime -- from PSP_GAME/USRDIR/PACKFILE.BIN (626 MB)

If the roster names exist in PACKFILE.BIN, that file links the id tables to the text, and the
character identity can be resolved without a running game.

IMPORTANT (a lesson already paid for): a UTF-16 scan over CODE produces convincing false positives,
because MIPS instructions decode as plausible CJK. So this script searches for KNOWN strings --
literal text we have already SEEN in RAM (e.g. "Goku", "Frieza", "Super Saiyan 3") -- rather than
guessing at what "text-looking" data is.

Usage:
    python scripts/psp-packfile-find.py <PACKFILE.BIN> Goku Frieza "Super Saiyan 3"
    python scripts/psp-packfile-find.py <PACKFILE.BIN> --utf16-scan --limit 40
"""
import argparse
import struct
import sys


def find_all(data, needle, limit=20):
    hits = []
    i = data.find(needle)
    while i >= 0 and len(hits) < limit:
        hits.append(i)
        i = data.find(needle, i + 1)
    return hits


def context_words(data, off, before=6, after=10):
    """Show u32 words around an offset, marking pointers.

    ⚠️ ALIGNMENT MATTERS. A UTF-16 string offset is frequently NOT 4-byte aligned, so reading u32s
    starting at the string offset yields byte-shifted garbage that looks like noise. This helper
    therefore aligns DOWN to a 4-byte boundary first, and reports the alignment delta so a misaligned
    hit is obvious rather than mysterious.
    """
    aligned = off - (off % 4)
    delta = off - aligned
    out = [f"(string at 0x{off:X}, {delta} byte(s) past the 4-byte boundary)"]
    for k in range(-before, after + 1):
        o = aligned + k * 4
        if 0 <= o <= len(data) - 4:
            v = struct.unpack_from("<I", data, o)[0]
            if 0x08800000 <= v < 0x0A000000:
                out.append(f"[{k:+d}]0x{v:08X}")
            else:
                out.append(f"[{k:+d}]{v}")
    return " ".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packfile")
    ap.add_argument("strings", nargs="*", help="known UTF-16LE strings to locate")
    ap.add_argument("--utf16-scan", action="store_true",
                    help="scan for UTF-16 runs of readable ASCII/Latin text only")
    ap.add_argument("--limit", type=int, default=30)
    args = ap.parse_args()

    data = open(args.packfile, "rb").read()
    print(f"# {args.packfile}: {len(data):,} bytes")
    print()

    for s in args.strings:
        enc = s.encode("utf-16-le")
        hits = find_all(data, enc, limit=args.limit)
        print(f"=== '{s}' (utf-16le): {len(hits)} hit(s) ===")
        for i, off in enumerate(hits[:10]):
            print(f"  @0x{off:08X}  ctx: {context_words(data, off)}")
        if hits:
            # also try ASCII, since some engines store labels as 8-bit
            asc = find_all(data, s.encode("ascii"), limit=5)
            print(f"  (ascii form: {len(asc)} hit(s))")
        print()

    if args.utf16_scan:
        print("=== UTF-16 runs consisting ONLY of printable ASCII (so: real text, not code) ===")
        n = 0
        i = 0
        L = len(data)
        while i < L - 4 and n < args.limit:
            # attempt a run of >=8 chars each being printable ASCII in the high byte position
            j = i
            chars = []
            while j < L - 1:
                c = struct.unpack_from("<H", data, j)[0]
                if 32 <= c < 127:
                    chars.append(chr(c))
                    j += 2
                else:
                    break
            if len(chars) >= 8:
                print(f"  0x{i:08X}  {''.join(chars)[:80]!r}")
                n += 1
                i = j + 2
            else:
                i += 2
        print(f"  (found {n})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
