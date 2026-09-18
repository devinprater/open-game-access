#!/usr/bin/env python3
"""psp-dissidia-mpk.py -- decode Dissidia's "MPK " archives inside PACKAGE.BIN.

MEASURED STRUCTURE (from a raw hex dump at payload offset 14336):
    4d 50 4b 20   "MPK " magic
    06 06 07 20   four version/flag bytes
    0c 00 00 00   entry count = 12
    then 12 entry records of 16 bytes each, and the fields are:
        w0 = offset of this entry's NAME, relative to the MPK HEADER
        w1 = offset of the FILE DATA, relative to the name block
        w2 = size in bytes
        w3 = 0

    Verified twice over:  at + w0[0] = 14336 + 208 = 14544, where "battle.bin" begins, and the
    successive w0 gaps (11, 13, 11, 13, 17, 14, ...) are exactly name length + 1 -- i.e. w0 is a
    cumulative name-table offset. And names_at + w1[0] = 14544 + 400 = 14944, which is where the
    data begins, with each entry's (offset + size) landing on the next entry's offset.

    then a NUL-separated NAME block, one name per entry, in entry order:
        battle.bin / objentry.bin / system.bin / mapentry.bin / manual_param.bin /
        ptcommand.bin / voice_priority_parameter.bin / resident.frr / snd_menu.scd /
        snd_common.scd / bgm_entry.bin / se_override.bin

    A nested "ARC" archive follows at the payload right after the names, with its own
    four-character tags (spec/sklp/nekp/scrb) and (offset, size) pairs.

WHY THIS MATTERS
    A named archive is the difference between guessing at blobs and knowing what each blob IS.
    The Tag Team lesson applies directly: read the container's own labels before inventing anything.

USAGE
    python scripts/psp-dissidia-mpk.py --package PACKAGE.BIN --at 14332 --list
    python scripts/psp-dissidia-mpk.py --package PACKAGE.BIN --at 14332 --extract 0 2 --out DIR
"""
import argparse, os, struct, sys


def read_mpk(f, at):
    """Returns the entry table with ABSOLUTE payload offsets already resolved."""
    f.seek(at)
    head = f.read(16)
    if head[:4] != b"MPK ":
        return None
    n = struct.unpack_from("<I", head, 8)[0]
    if not (0 < n < 100000):
        return None
    recs = []
    for i in range(n):
        raw = f.read(16)
        if len(raw) < 16:
            return None
        name_rel, data_rel, size, unk = struct.unpack("<IIII", raw)
        recs.append({"name_rel": name_rel, "data_rel": data_rel, "size": size, "unk": unk})
    names_at = at + 16 + n * 16
    names = []
    for r in recs:
        # name_rel is relative to the MPK HEADER (verified: at + 208 = 14544,
        # which is exactly where "battle.bin" begins), not to names_at.
        f.seek(at + r["name_rel"])
        buf = b""
        while len(buf) < 256:
            c = f.read(1)
            if not c or c == b"\x00":
                break
            buf += c
        names.append(buf.decode("latin1", "replace"))
    for r, nm in zip(recs, names):
        r["name"] = nm
        r["offset"] = names_at + r["data_rel"]      # ABSOLUTE payload address
    return {"count": n, "records": recs, "names": names, "names_at": names_at}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True)
    ap.add_argument("--at", type=int, default=14332)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--extract", type=int, nargs="*")
    ap.add_argument("--out", default=".")
    ap.add_argument("--limit", type=int, default=200)
    a = ap.parse_args()

    size = os.path.getsize(a.package)
    f = open(a.package, "rb")
    mpk = read_mpk(f, a.at)
    if not mpk:
        sys.exit("no MPK header at %d" % a.at)
    print('=== MPK at %d: %d entries, names at %d ===' % (a.at, mpk["count"], mpk["names_at"]))
    print()
    print("%-4s %-40s %-12s %-12s" % ("n", "name", "offset", "size"))
    for i, r in enumerate(mpk["records"][:a.limit]):
        nm = r["name"]
        flag = ""
        if not (0 <= r["offset"] < size):
            flag = "  <- offset OUT OF RANGE"
        if r["size"] and not (0 <= r["offset"] + r["size"] <= size):
            flag += "  <- size OUT OF RANGE"
        print("%-4d %-40s %-12d %-12d%s" % (i, nm, r["offset"], r["size"], flag))

    if a.extract:
        os.makedirs(a.out, exist_ok=True)
        for i in a.extract:
            if i >= len(mpk["records"]):
                print("  entry %d out of range" % i); continue
            r = mpk["records"][i]
            nm = r["name"]
            dst = os.path.join(a.out, nm.replace("/", "_"))
            f.seek(r["offset"])
            left = r["size"]
            with open(dst, "wb") as o:
                while left > 0:
                    ch = f.read(min(1 << 20, left))
                    if not ch:
                        break
                    o.write(ch); left -= len(ch)
            print("  [%2d] %-40s %9d bytes -> %s" % (i, nm, os.path.getsize(dst), dst))
    f.close()


if __name__ == "__main__":
    main()
