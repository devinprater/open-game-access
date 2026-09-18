#!/usr/bin/env python3
"""psp-dissidia-unpack.py -- read Dissidia's PACKAGE.BIN via PACKAGE_INFO.BIN.

THE PAIR (measured)
    PACKAGE.BIN      660,183,040 bytes. Header: 02000000 01000000 75730000 05000000 "ULUS10437"
                     -> self-identifies with the game id, so it is the payload.
    PACKAGE_INFO.BIN      68,908 bytes. Header: 18 02 | 09 20 | "packm" | 16 00 00 00 | 00000000
                     -> an INDEX, with the ASCII magic "packm".

THE RECORD LAYOUT (measured, not assumed)
    From 0x10 the index holds records of THREE u32 words each -- 5741 of them:

        w0  cumulative END offset  (strictly ascending: 2071746, 2493824, 4295189, ...)
        w1  per-entry LENGTH       (157949, 180213, 157363, ...)
        w2  a SECOND offset into PACKAGE.BIN

    Verified by EXTRACTING at w2 -- the bytes there are real game data:
        rec[ 0] @26956      float triplets (3.0, 4.5, 0.05 ...)
        rec[ 1] @41356      readable ASCII: "T200_2P", "p_eht200"
        rec[ 3] @16588      floats (64.0, 64.0, 0.2, 0.2)
        rec[13] @17356      floats (6.0, 10.0, 0.05)
    w2 values also cluster just under 536,870,912 (0x20000000 = half the file), which is why the
    archive looks two-part: many entries address the second half.

    The `end - length` chain does NOT reproduce consecutive starts (0/1999 exact, only 7 within one
    sector), so the w0/w1 chain groups entries DIFFERENTLY from how w2 addresses them. Both are real;
    which one carries the names is still open.

USAGE
    python scripts/psp-dissidia-unpack.py --info I --package P --list
    python scripts/psp-dissidia-unpack.py --info I --package P --at 41356 --len 256
    python scripts/psp-dissidia-unpack.py --info I --package P --strings --limit 40
"""
import argparse, os, re, struct, sys

MAGIC = b"packm"
REC_START = 0x10


def records(ib):
    out = []
    i = REC_START
    while i + 12 <= len(ib):
        w0, w1, w2 = struct.unpack_from("<III", ib, i)
        out.append({"n": len(out), "at": i, "end": w0, "length": w1, "offset": w2,
                    "start": w0 - w1 if w0 >= w1 else None})
        i += 12
    return out


def scan_strings(pkg, lo, hi, minlen=5, limit=60):
    with open(pkg, "rb") as f:
        f.seek(lo)
        buf = f.read(hi - lo)
    found = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, buf):
        found.append((lo + m.start(), m.group().decode("latin1")))
        if len(found) >= limit:
            break
    return found


def utf16_runs(buf, minlen=4, limit=60):
    """UTF-16LE runs -- the sibling title keeps its text in UTF-16LE, so check here too."""
    out = []
    i = 0
    n = len(buf)
    while i < n - 1 and len(out) < limit:
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
    ap.add_argument("--package", required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--at", type=int, help="dump payload bytes from this absolute offset")
    ap.add_argument("--len", type=int, default=256)
    ap.add_argument("--strings", action="store_true", help="scan the payload for readable text")
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--save", help="write the --at range to this file")
    a = ap.parse_args()

    ib = open(a.info, "rb").read()
    pkg_size = os.path.getsize(a.package)
    m = ib.find(MAGIC)
    recs = records(ib)
    print("info %d bytes | package %d bytes | magic %r at 0x%X | %d record(s)"
          % (len(ib), pkg_size, MAGIC.decode(), m, len(recs)))

    if a.list:
        print("\n#  n   end         length      offset(=payload addr)   start=end-len")
        for r in recs[:a.limit]:
            print("  %4d  %-11d %-11d %-22d %s"
                  % (r["n"], r["end"], r["length"], r["offset"], r["start"]))

    if a.at is not None:
        n = max(0, min(a.len, pkg_size - a.at))
        with open(a.package, "rb") as f:
            f.seek(a.at)
            d = f.read(n)
        print("\n=== payload @%d (%d bytes) ===" % (a.at, len(d)))
        print("hex  : %s" % d[:64].hex())
        print("ascii: %r" % "".join(chr(b) if 32 <= b < 127 else "." for b in d[:96]))
        if len(d) >= 16:
            print("floats: %s" % " ".join("%.4g" % x for x in struct.unpack_from("<4f", d, 0)))
        if a.save:
            with open(a.save, "wb") as o:
                o.write(d)
            print("saved -> %s" % a.save)

    if a.strings:
        hi = min(64 << 20, pkg_size)
        print("\n=== readable ASCII runs in the payload's first %d MiB ===" % (hi >> 20))
        for off, s in scan_strings(a.package, 0, hi, limit=a.limit):
            print("   @%-10d %s" % (off, s[:96]))
        with open(a.package, "rb") as f:
            buf = f.read(hi)
        u = utf16_runs(buf, limit=a.limit)
        if u:
            print("\n=== UTF-16LE runs in the same range ===")
            for off, s in u:
                print("   @%-10d %s" % (off, s[:96]))


if __name__ == "__main__":
    main()
