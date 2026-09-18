#!/usr/bin/env python3
"""psp-iso-ls.py -- list a PSP ISO's contents by walking the ISO9660 directory records.

WHY A HAND-WRITTEN READER
    The project rule (from the Steins;Gate work): for an ISO, write a tiny reader rather than fight
    path translation and third-party tools. It keeps everything reproducible from the repo and avoids
    a dependency that may not be installed.

USAGE
    python scripts/psp-iso-ls.py ISO [--path PSP_GAME] [--depth 3] [--sizes]
"""
import argparse, os, struct, sys

SECTOR = 2048


def walk(f, extent, size, depth, maxdepth, prefix, sizes, out):
    f.seek(extent * SECTOR)
    data = f.read(size)
    off = 0
    while off < len(data):
        L = data[off]
        if L == 0:
            # advance to the next sector boundary within this directory record block
            off = ((off // SECTOR) + 1) * SECTOR
            if off >= len(data):
                break
            continue
        rec = data[off:off + L]
        if len(rec) < 34:
            break
        ext = struct.unpack_from("<I", rec, 2)[0]
        sz = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        raw = rec[33:33 + nlen]
        try:
            name = raw.decode("latin1")
        except Exception:
            name = repr(raw)
        isdir = bool(flags & 2)
        if name not in ("\x00", "\x01") and name not in (".", ".."):
            label = prefix + "/" + name if prefix else name
            line = "%-58s %s %10d" % (label, "DIR " if isdir else "file", sz)
            out.append(line)
            print(line)
            if isdir and depth < maxdepth:
                walk(f, ext, sz, depth + 1, maxdepth, label, sizes, out)
        off += L


def find_path(f, path):
    """Resolve a slash path inside the ISO to (extent, size)."""
    extent, size = 22, 2048            # root
    for part in [p for p in path.split("/") if p]:
        f.seek(extent * SECTOR)
        data = f.read(size)
        off = 0
        found = None
        while off < len(data):
            L = data[off]
            if L == 0:
                off = ((off // SECTOR) + 1) * SECTOR
                if off >= len(data):
                    break
                continue
            rec = data[off:off + L]
            if len(rec) < 34:
                break
            nlen = rec[32]
            name = rec[33:33 + nlen].decode("latin1")
            if name.upper() == part.upper():
                found = (struct.unpack_from("<I", rec, 2)[0],
                         struct.unpack_from("<I", rec, 10)[0],
                         rec[25])
                break
            off += L
        if not found:
            return None
        extent, size = found[0], found[1]
    return extent, size

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--path", default="")
    ap.add_argument("--depth", type=int, default=2)
    a = ap.parse_args()

    if not os.path.exists(a.iso):
        sys.exit("no such ISO: %s" % a.iso)

    f = open(a.iso, "rb")
    f.seek(16 * SECTOR)
    vd = f.read(SECTOR)
    if vd[1:6] != b"CD001":
        sys.exit("not an ISO9660 image (id=%r)" % vd[1:6])

    if a.path:
        loc = find_path(f, a.path)
        if not loc:
            sys.exit("path not found: %s" % a.path)
        extent, size = loc
        print("=== %s  (extent %d, %d bytes) ===" % (a.path, extent, size))
    else:
        root = vd[156:156 + 34]
        extent = struct.unpack_from("<I", root, 2)[0]
        size = struct.unpack_from("<I", root, 10)[0]
        print("=== /  (root) ===")

    out = []
    walk(f, extent, size, 0, a.depth, a.path, True, out)
    f.close()


if __name__ == "__main__":
    main()
