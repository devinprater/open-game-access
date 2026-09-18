#!/usr/bin/env python3
"""
oga-cso-extract.py — decompress a PSP CSO/ZSO image, then read it as ISO9660.

WHY NOT 7z: 7z reports "Cannot open the file as archive" on a raw CSO — the format is a
PSP-specific block-compressed ISO, not a general archive. The CSO container is simple
enough to read directly: a 24-byte header, a block index, then each block stored either
raw or deflate'd.

Format (little-endian):
  0x00  magic      'CISO'   (ZISO uses 0x5A49534F)
  0x04  header_size  (u32, usually 24)
  0x08  total_bytes  (u64) uncompressed size
  0x10  block_size   (u32 = log2 of the block size, usually 2048 -> 11)
  0x14  version      (u8)
  0x15  align        (u8)
  then (total_blocks + 1) u32 index entries at header_size.

Each index entry is (block_offset << align) | flag:
  flag 0x80000000 = plain (stored uncompressed)
  flag 0x3FFFFFFF (with align) = deflate-compressed block

Usage:
  oga-cso-extract.py IMAGE.iso  OUTDIR          decompress to OUTDIR/image.iso
  oga-cso-extract.py IMAGE.cso  --info          print header info only
"""
import argparse
import os
import struct
import sys
import zlib

SECTOR = 2048
MAGIC_CSO = 0x4F534943
MAGIC_ZSO = 0x4F53495A


def decompress(src, dst, quiet=False):
    with open(src, "rb") as f:
        hdr = f.read(24)
        magic, hdr_size = struct.unpack_from("<II", hdr, 0)
        total, = struct.unpack_from("<Q", hdr, 8)
        raw_block, = struct.unpack_from("<I", hdr, 16)
        version, align = struct.unpack_from("<BB", hdr, 20)

        if magic not in (MAGIC_CSO, MAGIC_ZSO):
            raise SystemExit(f"not a CSO/ZSO image (magic 0x{magic:08X})")

        # ⛔ THE FIELD AT 0x10 IS THE BLOCK SIZE (e.g. 2048), NOT A log2 SHIFT.
        # Assuming a shift here gave `1 << 2048` — an astronomically large block count —
        # and the index read produced garbage, surfacing as
        # "zlib.error: incorrect header check" rather than as an arithmetic bug.
        # Accept either convention so a genuinely shifted file still works.
        block_size = raw_block if raw_block >= 512 else (1 << raw_block)
        nblocks = (total + block_size - 1) // block_size

        f.seek(hdr_size)
        idx = struct.unpack_from("<%dI" % (nblocks + 1), f.read(4 * (nblocks + 1)))

        if not quiet:
            print(f"  magic        : {'CISO' if magic == MAGIC_CSO else 'ZISO'}")
            print(f"  uncompressed : {total:,} bytes ({total / 1e9:.2f} GB)")
            print(f"  block size   : {block_size}")
            print(f"  blocks       : {nblocks}")
            print(f"  version      : {version}  align: {align}")

        align_mask = (1 << align) - 1 if align else 0
        with open(dst, "wb") as out:
            written = 0
            for i in range(nblocks):
                pos = (idx[i] & ~0x80000000) if align == 0 else (idx[i] & ~align_mask)
                nxt = (idx[i + 1] & ~0x80000000) if align == 0 else (idx[i + 1] & ~align_mask)
                pos <<= align
                nxt <<= align
                size = nxt - pos
                f.seek(pos)
                chunk = f.read(size)
                if idx[i] & 0x80000000:
                    block = chunk                      # stored plain
                else:
                    # Some images store raw-deflate (no zlib header); try both.
                    try:
                        block = zlib.decompress(chunk)     # zlib-wrapped
                    except zlib.error:
                        block = zlib.decompressobj(-15).decompress(chunk)  # raw deflate
                out.write(block[:block_size])
                written += min(block_size, total - written)
                if not quiet and i % 20000 == 0 and i:
                    print(f"    ...{i}/{nblocks} blocks")
        if not quiet:
            print(f"  wrote {written:,} bytes -> {dst}")
    return dst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("outdir", nargs="?")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--out", default=None, help="explicit output .iso path")
    a = ap.parse_args()

    with open(a.image, "rb") as f:
        magic, = struct.unpack_from("<I", f.read(4), 0)
    if magic not in (MAGIC_CSO, MAGIC_ZSO):
        print(f"  {a.image} is not a CSO/ZSO (magic 0x{magic:08X}) — it is already an ISO")
        return 0 if not a.info else 0

    if a.info:
        # header only
        with open(a.image, "rb") as f:
            h = f.read(24)
        total, = struct.unpack_from("<Q", h, 8)
        bs, = struct.unpack_from("<I", h, 16)
        print(f"  {os.path.basename(a.image)}")
        print(f"  uncompressed : {total:,} bytes ({total / 1e9:.2f} GB)")
        print(f"  block size   : {1 << bs}")
        return 0

    if a.outdir is None:
        raise SystemExit("need an OUTDIR (or --out FILE.iso)")
    os.makedirs(a.outdir, exist_ok=True)
    dst = a.out or os.path.join(a.outdir, os.path.splitext(os.path.basename(a.image))[0] + ".iso")
    decompress(a.image, dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
