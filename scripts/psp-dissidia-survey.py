#!/usr/bin/env python3
"""psp-dissidia-survey.py -- survey EVERY "MPK " archive in PACKAGE.BIN and list their entry names.

WHY
    388 MPK archives live inside the payload. One of them holds the menu/text resources. Reading
    every archive's name table and grepping it is far cheaper than guessing which region to open --
    and it turns "find the text archive" into a lookup.

USAGE
    python scripts/psp-dissidia-survey.py --package PACKAGE.BIN [--grep text,msg,help,word] [--all]
"""
import argparse, os, re, struct, sys


def find_magics(path, magic, chunk=16 << 20):
    hits = []
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        pos = 0
        while pos < size:
            f.seek(pos)
            buf = f.read(chunk + 8)
            if not buf:
                break
            st = 0
            while True:
                i = buf.find(magic, st)
                if i < 0:
                    break
                hits.append(pos + i)
                st = i + 1
            pos += chunk
    return hits


def read_mpk(f, at):
    f.seek(at)
    head = f.read(16)
    if head[:4] != b"MPK ":
        return None
    n = struct.unpack_from("<I", head, 8)[0]
    if not (0 < n < 5000):
        return None
    recs = []
    for i in range(n):
        raw = f.read(16)
        if len(raw) < 16:
            return None
        name_rel, data_rel, size, unk = struct.unpack("<IIII", raw)
        recs.append({"name_rel": name_rel, "data_rel": data_rel, "size": size})
    names_at = at + 16 + n * 16
    for r in recs:
        f.seek(at + r["name_rel"])
        buf = b""
        while len(buf) < 200:
            c = f.read(1)
            if not c or c == b"\x00":
                break
            buf += c
        r["name"] = buf.decode("latin1", "replace")
        r["offset"] = names_at + r["data_rel"]
    return {"at": at, "count": n, "entries": recs, "names_at": names_at}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True)
    ap.add_argument("--grep", default="")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--limit", type=int, default=60)
    a = ap.parse_args()

    hits = find_magics(a.package, b"MPK ")
    print("MPK archives found: %d" % len(hits))

    patterns = [p.strip().lower() for p in a.grep.split(",") if p.strip()]
    f = open(a.package, "rb")
    good = []
    for at in hits:
        m = read_mpk(f, at)
        if not m:
            continue
        good.append(m)
    f.close()
    print("decoded: %d" % len(good))

    if patterns:
        print("\n=== archives whose entry names match %s ===" % patterns)
        for m in good:
            matched = [e for e in m["entries"]
                       if any(p in e["name"].lower() for p in patterns)]
            if matched:
                print("\n  MPK @%d  (%d entries)" % (m["at"], m["count"]))
                for e in matched[:a.limit]:
                    print("      %-44s %9d" % (e["name"], e["size"]))
    elif a.all:
        for m in good:
            print("\n=== MPK @%d (%d entries) ===" % (m["at"], m["count"]))
            for e in m["entries"][:a.limit]:
                print("   %-46s %9d" % (e["name"], e["size"]))
    else:
        # summary: distinct names by extension
        from collections import Counter
        ext = Counter()
        names = set()
        for m in good:
            for e in m["entries"]:
                nm = e["name"]
                names.add(nm)
                ext[os.path.splitext(nm)[1].lower() or "(none)"] += 1
        print("\n=== extensions across all archives ===")
        for k, v in ext.most_common(30):
            print("   %-10s %d" % (k, v))
        print("\ntotal distinct entry names: %d" % len(names))


if __name__ == "__main__":
    main()
