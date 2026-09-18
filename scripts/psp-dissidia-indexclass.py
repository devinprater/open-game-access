#!/usr/bin/env python3
"""psp-dissidia-indexclass.py -- classify what every PACKAGE_INFO record points at.

STATE OF THE QUESTION
    PACKAGE_INFO.BIN is EXACTLY a flat array: 16-byte header + 5741 x 12 bytes = 68908 = file size.
    Each record is three u32 words:

        w0  strictly ascending 5740/5740, max 4,294,062,453 (just under 2^32)  -- NOT a byte offset
        w1  a size (sum = 1,067,318,536 bytes = 1017.9 MiB)
        w2  an offset into PACKAGE.BIN: in range for 4994/5741 (87%, vs ~15% by chance)

    So w2 is the usable field and w0 is something else. Rather than theorise further, this script
    CLASSIFIES the bytes at every w2 -- archive magic, texture magic, readable text, floats, zeros --
    and reports the tally. That identifies the index empirically instead of by guesswork.

USAGE
    python scripts/psp-dissidia-indexclass.py --info I --package P
    python scripts/psp-dissidia-indexclass.py --info I --package P --show text --limit 40
"""
import argparse, collections, os, re, struct, sys

MAGICS = [
    (b"MPK ", "MPK named archive"),
    (b"ARC\x01", "ARC container"),
    (b"MIG.00.1PSP", "GIM texture"),
    (b"\x7fELF", "ELF"),
    (b"packm", "packm index"),
    (b"PSF", "PARAM.SFO"),
    (b"\x89PNG", "PNG"),
    (b"RIFF", "RIFF"),
]


def classify(d):
    """Return a short label for a 64-byte sample."""
    for m, label in MAGICS:
        if d.startswith(m) or (m in d and d.find(m) < 8):
            return label
    if not d or d.count(0) > len(d) * 0.85:
        return "zeros/empty"
    # GIM containers often start with a small header then MIG.00.1PSP
    # readable single-byte ASCII?
    asc = sum(1 for c in d if 32 <= c < 127) / len(d)
    u16 = 0
    for i in range(0, len(d) - 1, 2):
        if d[i + 1] == 0 and 32 <= d[i] < 127:
            u16 += 1
    if u16 / (len(d) // 2) > 0.6:
        return "UTF-16LE text"
    if asc > 0.85:
        return "ASCII text"
    # float-looking?  a run of finite plausible floats
    if len(d) >= 16:
        try:
            fl = struct.unpack_from("<4f", d, 0)
            if all(-1e6 < x < 1e6 for x in fl) and any(abs(x) > 1e-6 for x in fl):
                return "floats/params"
        except Exception:
            pass
    return "binary"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--info", required=True)
    ap.add_argument("--package", required=True)
    ap.add_argument("--show", help="only print records of this class")
    ap.add_argument("--limit", type=int, default=30)
    a = ap.parse_args()

    ib = open(a.info, "rb").read()
    pkg_size = os.path.getsize(a.package)
    N = (len(ib) - 0x10) // 12
    print("PACKAGE_INFO %d bytes -> %d records; PACKAGE.BIN %d bytes" % (len(ib), N, pkg_size))

    tally = collections.Counter()
    rows = []
    with open(a.package, "rb") as f:
        for i in range(N):
            w0, w1, w2 = struct.unpack_from("<III", ib, 0x10 + i * 12)
            in_range = 0 <= w2 < pkg_size
            if not in_range:
                tally["OUT OF RANGE"] += 1
                rows.append((i, w0, w1, w2, "OUT OF RANGE", b""))
                continue
            f.seek(w2)
            d = f.read(64)
            label = classify(d)
            tally[label] += 1
            rows.append((i, w0, w1, w2, label, d))

    print("\n=== classification of the bytes at w2 ===")
    for k, v in tally.most_common():
        print("   %-22s %5d  (%.1f%%)" % (k, v, 100.0 * v / N))

    if a.show:
        print("\n=== records classified %r (first %d) ===" % (a.show, a.limit))
        n = 0
        for i, w0, w1, w2, label, d in rows:
            if label != a.show:
                continue
            txt = "".join(chr(c) if 32 <= c < 127 else "." for c in d[:44])
            print("   [%4d] off=%-10d size=%-9d %s" % (i, w2, w1, txt))
            n += 1
            if n >= a.limit:
                break


if __name__ == "__main__":
    main()
