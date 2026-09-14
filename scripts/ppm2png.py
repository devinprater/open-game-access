#!/usr/bin/env python3
"""ppm2png.py — convert PPM screenshots to PNG.

PPM is what the probe writes (no image library needed on the C++ side); PNG is
what I can actually LOOK at. Screenshots are not decoration here: a RAM diff is
meaningless without a picture of what the game was doing between snapshots.
"""
import struct, sys, zlib, os, glob

def ppm2png(src, dst, scale=1):
    with open(src, "rb") as f:
        if f.readline().strip() != b"P6":
            raise ValueError("not a P6 PPM: " + src)
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        f.readline()
        data = f.read(w * h * 3)
    if scale > 1:
        out = bytearray()
        for y in range(h):
            row = data[y * w * 3:(y + 1) * w * 3]
            for _ in range(scale):
                for x in range(w):
                    px = row[x * 3:x * 3 + 3]
                    out += px * scale
        data = bytes(out); w *= scale; h *= scale
    raw = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    with open(dst, "wb") as o:
        o.write(png)

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    scale = 2 if "-s2" in sys.argv else 1
    pairs = []
    if len(args) == 2 and not os.path.isdir(args[0]):
        pairs = [(args[0], args[1])]
    else:
        src = args[0] if args else "."
        dst = args[1] if len(args) > 1 else src
        os.makedirs(dst, exist_ok=True)
        for p in sorted(glob.glob(os.path.join(src, "*.ppm"))):
            pairs.append((p, os.path.join(dst, os.path.basename(p)[:-4] + ".png")))
    for s, d in pairs:
        try:
            ppm2png(s, d, scale)
            print("ok", d)
        except Exception as e:
            print("FAIL", s, e)
