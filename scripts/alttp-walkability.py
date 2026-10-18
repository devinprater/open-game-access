#!/usr/bin/env python3
"""Screen walkability and path, from the game's own attribute ids.

Reads the probe's attrs= line, the cartridge's two collision tables, and the
decompilation's passable-tile list, then prints a walkability grid and, when
asked, a path to one edge of the screen.

Usage:
  alttp-walkability.py ROM zelda3-src PROBE-OUTPUT [START_COL START_ROW EDGE]
"""
import re
import sys
import os


def lo(addr):
    return ((addr & 0x7F0000) >> 1) | (addr & 0x7FFF)


def load_rom(path):
    data = open(path, "rb").read()
    header = 512 if len(data) % 1024 == 512 else 0
    return data, header


def table(data, header, addr, n, word=False):
    off = lo(addr) + header
    if word:
        return [data[off + i * 2] | (data[off + i * 2 + 1] << 8) for i in range(n)]
    return list(data[off:off + n])


def load_nothing(src):
    text = open(src, encoding="utf-8", errors="replace").read()
    m = re.search(r"(case 0x[0-9a-f]{2}:.*?)\s*//\s*TileBehavior_NothingOW", text, re.S)
    if not m:
        raise SystemExit("could not find TileBehavior_NothingOW in %s" % src)
    return set(int(v, 16) for v in re.findall(r"case 0x([0-9a-f]{2}):", m.group(1)))


def build_grid(rom, src, dump):
    data, header = load_rom(rom)
    map16 = table(data, header, 0x8F8000, 3752 * 4, word=True)
    map8 = table(data, header, 0x8E9459, 512)
    nothing = load_nothing(os.path.join(src, "src", "tile_detect.c"))

    text = open(dump, encoding="latin-1").read()
    attrs = None
    for line in text.splitlines():
        if line.startswith("attrs="):
            attrs = [int(v, 16) for v in line[6:].split(",")]
    if not attrs or len(attrs) != 1024:
        raise SystemExit("no 32x32 attrs= line in %s" % dump)

    grid = []
    for ry in range(32):
        row = []
        for rx in range(32):
            a = attrs[ry * 32 + rx]
            walk = False
            for q in range(4):
                t = a * 4 + q
                idx = map16[t] & 0x1FF if t < len(map16) else 0
                if map8[idx] in nothing:
                    walk = True
                    break
            row.append(walk)
        grid.append(row)
    return grid


def find_path(grid, start, edge):
    goals = ([(31, r) for r in range(32)] if edge == "east" else
             [(c, 0) for c in range(32)])
    goals = [g for g in goals if grid[g[1]][g[0]]]
    prev = {start: None}
    q = [start]
    end = None
    while q and end is None:
        cur = q.pop(0)
        if cur in goals:
            end = cur
            break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cur[0] + dx, cur[1] + dy
            if 0 <= nx < 32 and 0 <= ny < 32 and grid[ny][nx] and (nx, ny) not in prev:
                prev[(nx, ny)] = cur
                q.append((nx, ny))
    if end is None:
        return None
    path = []
    cur = end
    while cur:
        path.append(cur)
        cur = prev[cur]
    path.reverse()
    return path


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    rom, src, dump = sys.argv[1:4]
    grid = build_grid(rom, src, dump)
    print("walkable cells: %d of 1024" % sum(sum(1 for v in r if v) for r in grid))
    for ry in range(32):
        print("".join("." if v else "#" for v in grid[ry]))
    if len(sys.argv) >= 7:
        start = (int(sys.argv[4]), int(sys.argv[5]))
        edge = sys.argv[6]
        path = find_path(grid, start, edge)
        if not path:
            print("no path from %s to the %s edge" % (start, edge))
            return 1
        print("path to the %s edge: %d cells, %s -> %s" % (edge, len(path), path[0], path[-1]))
        print("waypoints=" + " ".join("%d,%d" % p for p in path[::2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
