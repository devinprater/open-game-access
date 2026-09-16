#!/usr/bin/env python3
"""
oga-psp-analyze.py — find game text in a PSP user-RAM dump.

Built for Steins;Gate: My Darling's Embrace (ULJM06040) under PPSSPP, but the method
is general to visual novels: the script is resident in RAM, and a reader needs (a) the
text itself and (b) whatever says WHICH line is on screen. This tool attacks (a) and
reports honestly when (b) is absent.

⛔ OFFSET ARITHMETIC. PSP user RAM is 0x08800000..0x09FFFFFF (24 MiB = 0x1800000).
A full dump of that region has file offset (address - 0x08800000). If you dumped a
NARROWER region, pass --base, or every address printed is wrong by a constant — which
reads as "the addresses don't match the screen" rather than "the base is wrong".

Usage:
  oga-psp-analyze.py DUMP.bin [--base 0x08800000] [--keyword NAME]... [--min 20]
                              [--prose-limit 40] [--table]
"""
import argparse
import re
import sys

RAM_BASE_DEFAULT = 0x08800000

# Character names appear in the script, the phone UI, and the save data. Finding them
# is a good way to confirm the text region is the one we think it is.
DEFAULT_KEYWORDS = [
    "Okabe", "Kurisu", "Mayuri", "Daru", "Suzuha", "Faris", "Luka", "Moeka",
    "Rintaro", "El Psy", "Lab", "IBN", "Amadeus",
]


def decode_shift_jis_runs(buf, base, min_bytes=0x20):
    """Runs that look like Shift-JIS. The base game is Japanese, so some text will be."""
    out = []
    i, n = 0, len(buf)
    start = None
    while i < n:
        b = buf[i]
        is_lead = 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xEF
        is_ascii = 0x20 <= b <= 0x7E
        if is_lead and i + 1 < n:
            i += 2
            if start is None:
                start = i - 2
            continue
        if is_ascii and b != 0:
            if start is None:
                start = i
            i += 1
            continue
        if start is not None and i - start >= min_bytes:
            out.append((start, buf[start:i]))
        start = None
        i += 1
    return out


def find_ascii_runs(buf, minlen):
    return [(m.start(), m.group()) for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, buf)]


def prose_score(s):
    """0..1 — how much this looks like sentences rather than a symbol table."""
    if not s:
        return 0.0
    letters = sum(c.isalpha() or c == " " for c in s)
    spaces = s.count(" ")
    return (letters / len(s)) * (1.0 if spaces else 0.35)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--base", default=hex(RAM_BASE_DEFAULT))
    ap.add_argument("--keyword", action="append", default=[])
    ap.add_argument("--min", type=int, default=24, help="min ASCII run length for prose")
    ap.add_argument("--prose-limit", type=int, default=40)
    ap.add_argument("--table", action="store_true", help="cluster strings into a table view")
    ap.add_argument("--sjis", action="store_true", help="also scan Shift-JIS runs")
    args = ap.parse_args()

    base = int(args.base, 0)
    buf = open(args.dump, "rb").read()
    print(f"dump    : {args.dump}")
    print(f"size    : {len(buf)} bytes (0x{len(buf):X})")
    print(f"base    : 0x{base:08X}  -> top 0x{base + len(buf):08X}")
    print()

    # ---- keywords ----------------------------------------------------------
    keywords = args.keyword or DEFAULT_KEYWORDS
    print("=== keyword hits ===")
    for kw in keywords:
        needle = kw.encode("latin1")
        hits = []
        idx = buf.find(needle)
        while idx >= 0 and len(hits) < 8:
            hits.append(idx)
            idx = buf.find(needle, idx + 1)
        if hits:
            addrs = " ".join(f"0x{base + h:08X}" for h in hits)
            print(f"  {kw:<12} {len(hits)}{'+' if len(hits) == 8 else ''}  {addrs}")
    print()

    # ---- prose -------------------------------------------------------------
    runs = find_ascii_runs(buf, args.min)
    scored = []
    for off, raw in runs:
        try:
            s = raw.decode("latin1")
        except Exception:
            continue
        sc = prose_score(s)
        if sc >= 0.80 and s.count(" ") >= 3:
            scored.append((sc, off, s))
    scored.sort(key=lambda t: (-t[0], t[1]))
    print(f"=== prose-like ASCII runs (min {args.min}, score>=0.80): {len(scored)} ===")
    for sc, off, s in scored[: args.prose_limit]:
        print(f"  0x{base + off:08X}  [{sc:.2f}] {s[:96]!r}")
    print()

    # ---- longest runs ------------------------------------------------------
    longest = sorted(runs, key=lambda t: -len(t[1]))[:15]
    print("=== longest ASCII runs ===")
    for off, raw in longest:
        print(f"  0x{base + off:08X}  {len(raw):5d}  {raw[:80]!r}")
    print()

    # ---- optional table clustering ----------------------------------------
    if args.table:
        # A visual-novel script is a dense cluster of strings. Find the densest window
        # of string STARTS: that is where the script block lives, as distinct from the
        # SDK strings, which are sparse and near the top of RAM.
        starts = sorted(off for off, _ in runs)
        print("=== densest string clusters (256 KiB windows) ===")
        win = 0x40000
        best = []
        j = 0
        for i, o in enumerate(starts):
            while starts[j] < o - win:
                j += 1
            best.append((i - j + 1, o))
        best.sort(key=lambda t: -t[0])
        seen = []
        for count, o in best:
            if any(abs(o - s) < win for s in seen):
                continue
            seen.append(o)
            print(f"  0x{base + o:08X} .. 0x{base + o + win:08X}  {count} strings")
            if len(seen) >= 8:
                break
        print()

    # ---- optional Shift-JIS -------------------------------------------------
    if args.sjis:
        sj = decode_shift_jis_runs(buf, base)
        big = sorted(sj, key=lambda t: -len(t[1]))[:15]
        print(f"=== Shift-JIS-looking runs: {len(sj)}; longest ===")
        for off, raw in big:
            try:
                text = raw.decode("shift_jis", "replace")
            except Exception:
                text = raw.decode("latin1")
            print(f"  0x{base + off:08X}  {len(raw):5d}  {text[:60]!r}")
        print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
