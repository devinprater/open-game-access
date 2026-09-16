#!/usr/bin/env python3
"""
oga-iso-extract.py — minimal ISO9660 reader, for pulling a PSP game's files out.

WHY NOT 7z/unzip: this needs to run under MSYS where native tools do not accept the
bash-style paths, and passing Windows paths into a bash pipeline invites silent
"file not found" from a path that was translated twice. A 60-line ISO9660 reader is
smaller than the debugging.

Usage:
  oga-iso-extract.py ISO list [PATH]
  oga-iso-extract.py ISO cat  PATH          (write to stdout)
  oga-iso-extract.py ISO save PATH OUTDIR
"""
import argparse
import os
import sys

SECTOR = 2048


class ISO:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.pvd = self._sector(16)
        if self.pvd[1:6] != b"CD001":
            raise ValueError("not an ISO9660 image (no CD001 at sector 16)")
        root = self.pvd[156:156 + 34]
        self.root_lba = int.from_bytes(root[2:6], "little")
        self.root_size = int.from_bytes(root[10:14], "little")

    def _sector(self, lba):
        self.f.seek(lba * SECTOR)
        return self.f.read(SECTOR)

    def _read_extent(self, lba, size):
        self.f.seek(lba * SECTOR)
        return self.f.read(size)

    def entries(self, lba, size):
        """Yield (name, is_dir, lba, size) for a directory extent."""
        data = self._read_extent(lba, size)
        off = 0
        while off < len(data):
            rec_len = data[off]
            if rec_len == 0:
                # move to the next sector boundary
                off = ((off // SECTOR) + 1) * SECTOR
                continue
            rec = data[off:off + rec_len]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            elba = int.from_bytes(rec[2:6], "little")
            esize = int.from_bytes(rec[10:14], "little")
            flags = rec[25]
            if name_len == 1 and name[0] in (0, 1):
                pass                                    # '.' and '..'
            else:
                nm = name.decode("latin1")
                # strip the ISO version suffix (;1)
                if ";" in nm:
                    nm = nm.split(";")[0]
                yield nm, bool(flags & 0x02), elba, esize
            off += rec_len

    def walk(self, lba=None, size=None, prefix=""):
        if lba is None:
            lba, size = self.root_lba, self.root_size
        for name, is_dir, elba, esize in self.entries(lba, size):
            path = f"{prefix}/{name}" if prefix else name
            if is_dir:
                yield from self.walk(elba, esize, path)
            else:
                yield path, elba, esize

    def find(self, wanted):
        """Case-insensitive lookup."""
        w = wanted.strip("/").upper()
        for path, lba, size in self.walk():
            if path.upper() == w:
                return path, lba, size
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("cmd", choices=["list", "cat", "save"])
    ap.add_argument("path", nargs="?")
    ap.add_argument("outdir", nargs="?")
    ap.add_argument("--limit", type=int, default=200)
    args = ap.parse_args()

    iso = ISO(args.iso)

    if args.cmd == "list":
        n = 0
        for path, lba, size in iso.walk():
            if args.path and not path.upper().startswith(args.path.strip("/").upper()):
                continue
            print(f"  {size:>12,}  LBA {lba:>7}  {path}")
            n += 1
            if n >= args.limit:
                print(f"  ... (limit {args.limit})")
                break
        print(f"  {n} entr{'y' if n == 1 else 'ies'}")
        return 0

    if not args.path:
        print("need PATH", file=sys.stderr); return 2
    hit = iso.find(args.path)
    if not hit:
        print(f"!! not found: {args.path}", file=sys.stderr); return 1
    path, lba, size = hit
    data = iso._read_extent(lba, size)

    if args.cmd == "cat":
        sys.stdout.buffer.write(data)
        return 0

    out = os.path.join(args.outdir or ".", os.path.basename(path))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"  {path} -> {out}  ({size:,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
