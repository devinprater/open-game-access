#!/usr/bin/env python3
"""Count the blue movement-overlay tiles in a DS framebuffer dump and compare with the
computed reachable count.

Why programmatic rather than eyeballing: "36 squares reachable" is a claim that needs a
number to check against, and a human estimating a blue blob from a 256x192 image is not
that number. This counts tiles.

The blue overlay is drawn as a translucent tint over the terrain, so the test is
"is this tile blue-dominant" rather than an exact colour match — the tint composites
over whatever is underneath, so no single RGB value identifies it.

Tile size and map origin are supplied because the DS map view scrolls: the overlay is
not anchored at (0,0), so the origin must be given per screenshot or inferred from the
grid. Usage:
    count-blue-tiles.py <image.ppm|png> [tile_px] [origin_x] [origin_y]
"""
import sys
import collections


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"!! not a P6 PPM: {path}")
    # Parse the header: P6 <w> <h> <maxval>\n
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, _ = fields
    px = data[pos:pos + w * h * 3]
    return w, h, px


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    tile = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    ox = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    oy = int(sys.argv[4]) if len(sys.argv) > 4 else 0

    w, h, px = read_ppm(path)
    print(f"image {w}x{h}, tile={tile}px, origin=({ox},{oy})")

    cols = (w - ox) // tile
    rows = (h - oy) // tile
    print(f"grid {cols}x{rows} tiles")

    blue_tiles = 0
    hist = collections.Counter()
    for ty in range(rows):
        for tx in range(cols):
            # Average a small patch at the tile's centre: edges carry the terrain
            # border lines, which are not part of the tint.
            sx = ox + tx * tile + tile // 2
            sy = oy + ty * tile + tile // 2
            r = g = b = n = 0
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    X, Y = sx + dx, sy + dy
                    if not (0 <= X < w and 0 <= Y < h):
                        continue
                    i = (Y * w + X) * 3
                    r += px[i]; g += px[i + 1]; b += px[i + 2]; n += 1
            if not n:
                continue
            r //= n; g //= n; b //= n
            # Blue-dominant: blue clearly exceeds both red and green.
            if b > r + 20 and b > g + 20:
                blue_tiles += 1
                hist[(r // 32 * 32, g // 32 * 32, b // 32 * 32)] += 1

    print(f"\nBLUE-DOMINANT TILES: {blue_tiles}")
    if hist:
        print("dominant blue colours (r,g,b bucket -> tiles):")
        for colour, count in hist.most_common(6):
            print(f"  {colour}  {count}")


if __name__ == "__main__":
    main()
