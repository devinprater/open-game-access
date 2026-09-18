#!/usr/bin/env python3
"""psp-dissidia-arc.py -- decode Dissidia's "ARC" containers.

WHY
    The EBOOT's own path strings name the text resources:
        text/JP/mess_pk_loading_0.bin
        text/JP/menu/loading_movie.bin
        general_archive/field/JP/menu_lang.bin
        general_archive/field/JP/field_lang.bin
    These are NOT inside any MPK archive (a survey of all 388 found no lang/mess/text entries), and
    1008 "ARC\\x01" magics exist in the payload. So the text lives in ARC containers.

MEASURED ARC SHAPE (payload region around the first ARC at 14736)
    41 52 43 01   "ARC\\x01"
    08 00 00 00   ...
    then a table of records, each four-character TAG + u32 + u32:
        spec | 0x90     | 0x26f8
        sklp | 0x2788   | 0x13c
        nekp | 0x28c4   | 0x4e8
        scrb | 0x2dac   | ...
    with offsets that appear relative to the ARC header.

    This script reads that table, resolves the offsets, lists the tags, and extracts by index so the
    contents can identify themselves.

USAGE
    python scripts/psp-dissidia-arc.py --package PACKAGE.BIN --at 14736 --list
    python scripts/psp-dissidia-arc.py --package PACKAGE.BIN --at 14736 --extract 0 1 --out DIR
"""
import argparse, os, struct, sys


def read_arc(f, at, max_entries=4096):
    """Header is 16 bytes: magic, count, 2 reserved words. Records are 16 bytes:
    (u32 flag, 4-char tag, u32 offset, u32 size)."""
    f.seek(at)
    head = f.read(16)
    if head[:4] != b"ARC\x01":
        return None
    n = struct.unpack_from("<I", head, 4)[0]
    if not (0 < n < max_entries):
        return None
    f.seek(at + 16)
    recs = []
    for i in range(n):
        raw = f.read(16)
        if len(raw) < 16:
            break
        flag, tag, off, size = struct.unpack("<I4sII", raw)
        printable = sum(1 for c in tag if 32 <= c < 127)
        recs.append({"flag": flag, "tag": tag, "a": off, "b": size, "printable": printable})
    score = sum(r["printable"] for r in recs) / (4.0 * max(1, len(recs)))
    return {"at": at, "count": n, "recs": recs, "score": score, "records_at": at + 16}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True)
    ap.add_argument("--at", type=int, required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--extract", type=int, nargs="*")
    ap.add_argument("--out", default=".")
    ap.add_argument("--limit", type=int, default=60)
    a = ap.parse_args()

    size = os.path.getsize(a.package)
    f = open(a.package, "rb")
    arc = read_arc(f, a.at)
    if not arc:
        sys.exit("no ARC header at %d" % a.at)

    print("=== ARC @%d  records at %d, %d entries, tag-printability %.2f ==="
          % (a.at, arc["records_at"], arc["count"], arc["score"]))
    print("%-4s %-6s %-5s %-12s %-12s %s" % ("n", "tag", "flag", "offset", "size", "data address"))
    for i, r in enumerate(arc["recs"][:a.limit]):
        tg = r["tag"].decode("latin1", "replace")
        print("%-4d %-6s %-5d %-12d %-12d %d"
              % (i, tg, r["flag"], r["a"], r["b"], arc["at"] + r["a"]))

    if a.extract:
        os.makedirs(a.out, exist_ok=True)
        # resolve the payload base for this ARC: offsets look relative to the ARC header
        for i in a.extract:
            if i >= len(arc["recs"]):
                print("  %d out of range" % i); continue
            r = arc["recs"][i]
            for label, off, ln in (("rel-arc", arc["at"] + r["a"], r["b"]),
                                   ("absolute", r["a"], r["b"])):
                if 0 <= off < size and 0 <= ln <= size - off and ln > 0:
                    f.seek(off)
                    d = f.read(min(ln, 48))
                    dst = os.path.join(a.out, "%02d_%s.bin"
                                       % (i, r["tag"].decode("latin1", "replace").strip() or "tag"))
                    f.seek(off)
                    with open(dst, "wb") as o:
                        left = ln
                        while left > 0:
                            ch = f.read(min(1 << 20, left))
                            if not ch:
                                break
                            o.write(ch); left -= len(ch)
                    print("  [%2d] %s off=%-10d len=%-9d first=%s -> %s"
                          % (i, label, off, ln, d[:16].hex(), os.path.basename(dst)))
                    break
            else:
                print("  [%2d] no in-range interpretation" % i)
    f.close()


if __name__ == "__main__":
    main()
