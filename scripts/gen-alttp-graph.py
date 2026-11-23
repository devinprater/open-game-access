#!/usr/bin/env python3
"""Generate Core/alttp_graph.cpp — the overworld walking graph.

The graph is derived, offline, from the same ground truth the room database
came from: the zelda3 decompilation's data plus the one ROM dump the adapter
supports (payload CRC 777aac2f, asserted before anything is derived). For
each of the 128 grid screens (8x8 per world) the generator replays the
game's own tile assembly (src/overworld.c): the screen's two compressed
streams are decompressed with the decomp's own algorithm (assets/util.py),
each map32 tile expands to four map16 tiles (assets/map32_to_map16.txt),
and a map16 tile counts as enterable when any of its four map8 subtiles
carries a behaviour in the game's walkable set (TileBehavior_NothingOW in
src/tile_detect.c) — the criterion scripts/alttp-walkability.py established.

What ships is only derived facts: per screen, the area that owns it and the
list of border gaps — places where Link can walk (or drop) into the next
screen. No tile data, layout or text is copied.

Two honesty rules from the design consult are baked in here:
  * Regions. Each screen's walkable tiles are flood-filled into regions,
    and every gap records the region on both sides, so a route can never
    silently cross a screen through two disconnected patches of ground.
  * Width. A gap needs a run of at least two adjacent enterable border
    positions on both sides; a one-subtile crack is not a way through
    for Link's collision box.

Ledge tiles (TileBehavior_Ledge_* in src/tile_detect.c) on a border, with
walkable ground on the far side, make a one-way gap in the drop direction:
the ledge cell's gap list carries it, the neighbour's does not.

Usage:
  python3 scripts/gen-alttp-graph.py [--rom PATH] [--src ~/z3work/zelda3]
"""
import argparse
import os
import re
import sys
import zlib
from collections import deque

SCREEN_W = 32          # map16 tiles per screen edge
CELLS = 64             # 8x8 cells per world
WORLDS = 2             # light, dark


def lo(addr):
    """SNES LoROM address -> file offset (the walkability script's mapping)."""
    return ((addr & 0x7F0000) >> 1) | (addr & 0x7FFF)


class Rom:
    def __init__(self, path):
        data = open(path, "rb").read()
        if len(data) % 1024 == 512:
            data = data[512:]
        crc = zlib.crc32(data) & 0xFFFFFFFF
        if crc != 0x777AAC2F:
            raise SystemExit("ROM payload CRC is %08x, not 777aac2f — refusing "
                             "to derive a graph from an unsupported dump" % crc)
        self.data = data

    def byte(self, ea):
        return self.data[lo(ea)]

    def bytes(self, ea, n):
        out = bytearray()
        a = ea
        for _ in range(n):
            out.append(self.byte(a))
            a += 1
            if (a & 0xFFFF) == 0:
                a += 0x8000
        return bytes(out)

    def get24(self, ea):
        b = self.bytes(ea, 3)
        return b[0] | (b[1] << 8) | (b[2] << 16)

    def words(self, ea, n):
        out = []
        for i in range(n):
            b = self.bytes(ea + i * 2, 2)
            out.append(b[0] | (b[1] << 8))
        return out


def decomp(rom, ea):
    """The decomp's own decompressor (assets/util.py decomp()), ported."""
    result = bytearray()
    state = {"a": ea}

    def nxt():
        b = rom.byte(state["a"])
        state["a"] += 1
        if (state["a"] & 0xFFFF) == 0:
            state["a"] += 0x8000
        return b

    while True:
        b = nxt()
        if b == 0xFF:
            return bytes(result)
        if (b & 0xE0) != 0xE0:
            lx = b & 0x1F
            cmd = b & 0xE0
        else:
            cmd = (b << 3) & 0xE0
            lx = ((b & 3) << 8) | nxt()
        lx += 1
        if cmd == 0x00:                       # literal
            for _ in range(lx):
                result.append(nxt())
        elif cmd & 0x80:                      # copy
            offs = (nxt() << 8) | nxt()
            for _ in range(lx):
                result.append(result[offs])
                offs += 1
        elif (cmd & 0x40) == 0:               # memset
            v = nxt()
            result.extend([v] * lx)
        elif (cmd & 0x20) == 0:               # memset16
            b1, b2 = nxt(), nxt()
            while lx:
                result.append(b1)
                if lx == 1:
                    break
                result.append(b2)
                lx -= 2
        else:                                 # incr
            v = nxt()
            for _ in range(lx):
                result.append(v)
                v = (v + 1) & 0xFF


def load_behaviours(src):
    """Walkable set + ledge families from src/tile_detect.c."""
    text = open(os.path.join(src, "src", "tile_detect.c"),
                encoding="utf-8", errors="replace").read()
    m = re.search(r"(case 0x[0-9a-f]{2}:.*?)\s*//\s*TileBehavior_NothingOW",
                  text, re.S)
    if not m:
        raise SystemExit("could not find TileBehavior_NothingOW")
    walkable = set(int(v, 16) for v in re.findall(r"case 0x([0-9a-f]{2}):", m.group(1)))

    # NothingOW alone fragments the world (measured on the Link's-house
    # screen: 478 walkable tiles in 15 regions, and Kakariko unreachable).
    # The behaviours Link actually walks on are wider: grass he wades
    # through, diggable dirt, shallow water, the ground under an overlay
    # tile (bridges, canopies), slopes and stairs. With these added, the
    # same screen is 756 walkable tiles in ONE region, and the grid itself
    # was verified against the live game's tile-attribute RAM (1022 of
    # 1024 tiles identical; the two differences are tiles Link disturbed).
    groups = {}
    for mm in re.finditer(r"((?:case 0x[0-9a-f]{2}:?\s*)+)//\s*TileBehavior_(\w+)",
                          text):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", mm.group(1))]
        groups.setdefault(mm.group(2), set()).update(vals)
    for name in ("ThickGrass", "DiggableGround", "ShallowWater", "Warp",
                 "RupeeTile", "Entrance", "OverlayMask_0C", "OverlayMask_1C",
                 "SpikeFloor", "PalaceIce", "GanonIce", "Slope", "SlopeOuter",
                 "NorthSingleLayerStairs", "NorthSwapLayerStairs"):
        walkable |= groups.get(name, set())

    ledges = {}
    for mm in re.finditer(r"((?:case 0x[0-9a-f]{2}:?\s*)+)//\s*TileBehavior_Ledge_(\w+)",
                          text):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", mm.group(1))]
        ledges.setdefault(mm.group(2), set()).update(vals)
    # The steering mask's exclusions (docs/design/auto-walk.md SS5):
    # spike floors hurt, warp tiles teleport, entrance tiles are door
    # thresholds. All three are IN the walkable set above (Link can
    # stand on each), so the speech table cannot express the steering
    # rule; the generator emits a second bitmap for it below.
    blocked = set()
    for name in ("SpikeFloor", "Warp", "Entrance"):
        blocked |= groups.get(name, set())
    return walkable, ledges, blocked


def load_map32_table(src):
    table = {}
    for line in open(os.path.join(src, "assets", "map32_to_map16.txt"),
                     encoding="utf-8", errors="replace"):
        m = re.match(r"\s*(\d+):\s*([0-9,\s]+)$", line)
        if m:
            table[int(m.group(1))] = [int(v) for v in m.group(2).split(",")]
    return table


def screen_tiles(rom, screen, m32tab, map16tab, map8attr, walkable,
                 ledge_ns, ledge_ew):
    """(walkable, ledge) 32x32 grids at map16 granularity for one screen.
    ledge is an axis bitmask: 1 = a N/S ledge subtile, 2 = an E/W one."""
    hi = decomp(rom, rom.get24(0x82F94D + screen * 3))
    lo_b = decomp(rom, rom.get24(0x82FB2D + screen * 3))
    # The game copies exactly 256 tiles out of the decompressed buffer
    # (Overworld_DecompressAndDrawOneQuadrant); one stream in the retail
    # dump decompresses to 258 and its tail is ignored by the game too.
    if len(hi) < 256 or len(lo_b) < 256:
        raise SystemExit("screen %d decompressed to %d/%d bytes, expected >= 256"
                         % (screen, len(hi), len(lo_b)))
    hi, lo_b = hi[:256], lo_b[:256]
    walk = [[False] * SCREEN_W for _ in range(SCREEN_W)]
    ledge = [[0] * SCREEN_W for _ in range(SCREEN_W)]
    for i in range(256):
        m32 = (hi[i] << 8) | lo_b[i]
        parts = m32tab.get(m32)
        if parts is None or len(parts) != 4:
            continue
        mx, my = (i % 16) * 2, (i // 16) * 2
        for k, m16 in enumerate(parts):
            x, y = mx + (k & 1), my + (k >> 1)
            base = m16 * 4
            for q in range(4):
                if base + q >= len(map16tab):
                    continue
                attr = map8attr[map16tab[base + q] & 0x1FF]
                if attr in walkable:
                    walk[y][x] = True
                if attr in ledge_ns:
                    ledge[y][x] |= 1
                if attr in ledge_ew:
                    ledge[y][x] |= 2
    return walk, ledge


def flood_regions(walk):
    """Region id per tile (walkable tiles only), -1 elsewhere."""
    region = [[-1] * SCREEN_W for _ in range(SCREEN_W)]
    nxt = 0
    for y in range(SCREEN_W):
        for x in range(SCREEN_W):
            if not walk[y][x] or region[y][x] >= 0:
                continue
            q = deque([(x, y)])
            region[y][x] = nxt
            while q:
                cx, cy = q.popleft()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < SCREEN_W and 0 <= ny < SCREEN_W \
                            and walk[ny][nx] and region[ny][nx] < 0:
                        region[ny][nx] = nxt
                        q.append((nx, ny))
            nxt += 1
    return region, nxt


# side: 0=N, 1=S, 2=W, 3=E. Border cell accessors at offset i (0..31).
def border_pos(side, i):
    return [(i, 0), (i, 31), (0, i), (31, i)][side]


def inside_pos(side, i):
    return [(i, 1), (i, 30), (1, i), (30, i)][side]


DELTA = [-8, 8, -1, 1]
OPPOSITE = [1, 0, 3, 2]


def _hop_landing(get, x, y, dx, dy, axis):
    """From walkable (x, y), hop direction (dx, dy): the tile just ahead must
    be a ledge tile on this axis; the landing is the first walkable tile
    within six steps past it. get(x, y) -> (walk, ledgeflags, region) or
    None outside the field. Returns the landing (x, y) or None."""
    lx, ly = x + dx, y + dy
    t = get(lx, ly)
    if t is None or t[0] or not (t[1] & axis):
        return None
    for k in range(2, 8):
        vx, vy = x + dx * k, y + dy * k
        t = get(vx, vy)
        if t is None:
            return None
        if t[0]:
            return (vx, vy)
    return None


def find_gaps(screen, grids):
    """All border gaps of one screen. grids: screen -> (walk, ledge, region).

    Three crossing kinds: walking (both border tiles enterable), ledge hops
    whose arc crosses the border (takeoff and landing may be several tiles
    from the border itself -- the Pyramid plateau's south cliff has its
    ledge tiles on the lower screen's first row and lands four rows in),
    and either kind re-keyed through the screen's intra-cell ledge closure:
    a gap whose takeoff region is reachable from another region of the same
    screen by a hop is emitted for that region too, so a router that knows
    only (screen, region) states stays exact."""
    walk, ledge, region = grids[screen]
    world_base = (screen // CELLS) * CELLS
    cell = screen % CELLS
    row, col = divmod(cell, 8)

    def here(x, y):
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_W:
            return (walk[y][x], ledge[y][x], region[y][x])
        return None

    # Intra-cell ledge closure: region -> set of regions reachable by hops.
    hop_edges = set()
    for y in range(SCREEN_W):
        for x in range(SCREEN_W):
            if not walk[y][x]:
                continue
            for dx, dy, axis in ((0, -1, 1), (0, 1, 1), (-1, 0, 2), (1, 0, 2)):
                land = _hop_landing(here, x, y, dx, dy, axis)
                if land is not None:
                    hop_edges.add((region[y][x], region[land[1]][land[0]]))
    reach = {}
    all_regions = {region[y][x] for y in range(SCREEN_W) for x in range(SCREEN_W)
                   if walk[y][x]}
    for r in all_regions:
        seen = {r}
        q = deque([r])
        while q:
            cur = q.popleft()
            for a, b in hop_edges:
                if a == cur and b not in seen:
                    seen.add(b)
                    q.append(b)
        reach[r] = seen

    gaps = []
    for side in range(4):
        if (side == 0 and row == 0) or (side == 1 and row == 7) or \
           (side == 2 and col == 0) or (side == 3 and col == 7):
            continue
        nb = world_base + cell + DELTA[side]
        nwalk, nledge, nregion = grids[nb]
        opp = OPPOSITE[side]
        dx, dy = [(0, -1), (0, 1), (-1, 0), (1, 0)][side]
        axis = 1 if dy else 2

        def across(x, y, _nb=nb, _dx=dx, _dy=dy):
            """Tile field spanning the border: coordinates in this screen's
            frame, extended into the neighbour along the crossing axis."""
            if 0 <= x < SCREEN_W and 0 <= y < SCREEN_W:
                return (walk[y][x], ledge[y][x], region[y][x])
            # Neighbour frame: shift by the screen size along the axis.
            nx, ny = x - _dx * SCREEN_W, y - _dy * SCREEN_W
            if 0 <= nx < SCREEN_W and 0 <= ny < SCREEN_W:
                w, l, r = grids[_nb]
                return (w[ny][nx], l[ny][nx], r[ny][nx])
            return None

        def runs(kind):
            out = []
            start = None
            key = None
            for i in range(SCREEN_W + 1):
                cur = None
                if i < SCREEN_W:
                    x, y = border_pos(side, i)
                    nx, ny = border_pos(opp, i)
                    if kind == "walk":
                        if walk[y][x] and nwalk[ny][nx]:
                            cur = (region[y][x], nregion[ny][nx])
                    else:
                        # Ledge hop crossing the border: try takeoffs on
                        # this side (border tile and one/two tiles inside).
                        for back in range(3):
                            tx, ty = x - dx * back, y - dy * back
                            t = here(tx, ty)
                            if t is None or not t[0]:
                                continue
                            land = _hop_landing(across, tx, ty, dx, dy, axis)
                            if land is None:
                                continue
                            if 0 <= land[0] < SCREEN_W and 0 <= land[1] < SCREEN_W:
                                continue  # landing stayed home: closure's job
                            lt = across(land[0], land[1])
                            if lt is None:
                                continue
                            cur = (region[ty][tx], lt[2])
                            break
                if cur != key:
                    if key is not None and i - start >= (2 if kind == "walk" else 1):
                        out.append((start, i - 1, key[0], key[1]))
                    start = i
                    key = cur
            return out

        raw = []
        for (a, b, rh, rt) in runs("walk"):
            raw.append(((a + b) // 2, rh, rt, 0))
        for (a, b, rh, rt) in runs("ledge"):
            raw.append(((a + b) // 2, rh, rt, 1))
        # Fold the intra-cell closure into regionHere.
        seen_out = set()
        for (third, rh, rt, is_ledge) in raw:
            for r0 in all_regions:
                if rh in reach.get(r0, {r0}):
                    item = (side, third, r0, rt, is_ledge)
                    if item not in seen_out:
                        seen_out.add(item)
                        gaps.append(item)
    return gaps


HEADER_H = '''/*
 * alttp_graph.h — the overworld walking graph, generated data.
 *
 * Per screen (0..127; 8x8 cells per world, light first), the area that owns
 * it and the border gaps where Link can walk — or drop — into the next
 * screen. Derived offline from the game's own tile assembly by
 * scripts/gen-alttp-graph.py; no tile data ships, only the gaps.
 */
#ifndef OGA_ALTTP_GRAPH_H
#define OGA_ALTTP_GRAPH_H

#include <stdint.h>

namespace oga {

struct AlttpGap {
    uint8_t side;      /* 0 north, 1 south, 2 west, 3 east */
    uint8_t third;     /* which third of the border the gap sits in: 0, 1, 2 */
    uint8_t region;    /* this screen's walkable region at the gap */
    uint8_t toRegion;  /* the neighbour screen's region at the gap */
    int8_t  dScreen;   /* neighbour screen delta: -8, +8, -1, +1 */
    uint8_t ledge;     /* 1: a one-way ledge drop in the direction of travel */
};

/* The area id that owns a screen (a big area owns four screens). 0..127. */
uint8_t AlttpAreaOfScreen(uint8_t screen);

/* The screen's gaps; returns the count and sets *out (nullptr when none). */
int AlttpScreenGaps(uint8_t screen, const AlttpGap** out);

/* How many walkable regions the screen's tiles fall into. Region ids in
 * the gap table (and in a live flood fill done the same way: row-major
 * scan, four-connectivity) run 0..count-1. */
uint8_t AlttpScreenRegionCount(uint8_t screen);

/* Whether a map16 tile id is walkable ground for Link: any of its four
 * map8 subtiles carries a walk-on behaviour. The adapter flood-fills the
 * live screen from the game's own tile-attribute RAM with this table, so
 * the region it stands in is measured, not tracked. */
bool AlttpMap16Walkable(uint16_t map16);

/* Whether a map16 tile id is STEERABLE ground for auto walk
 * (docs/design/auto-walk.md SS5): the collision-shrunk form of the
 * table above. ALL four subtiles must carry a walk-on behaviour (a
 * tile walkable on one subtile only is a wall the walker would grind
 * on), and no subtile may be a spike floor, a warp tile or an entrance
 * threshold — the walker stops AT doors and warps, never on them. */
bool AlttpMap16Steerable(uint16_t map16);

/* Whether a map16 tile id is PASSABLE ground for auto walk's
 * fallback plans: at least three of its four subtiles walk-on, and
 * still no spike, warp or entrance subtile. Used only when no
 * steerable path exists on a screen (stairs and cliff paths). */
bool AlttpMap16Passable(uint16_t map16);

} // namespace oga

#endif
'''

HEADER_CPP = '''/*
 * alttp_graph.cpp — the overworld walking graph, generated; do not edit.
 *
 * Source: the zelda3 decompilation's overworld data and the USA 1.0
 * cartridge's own tile tables, assembled exactly as src/overworld.c
 * assembles them. Only derived facts are here: area ownership and border
 * gaps with their walkable regions. Regenerate with
 * scripts/gen-alttp-graph.py.
 */
#include "alttp_graph.h"

namespace oga {
namespace {

'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default="/mnt/c/Users/Devin Prater/Dropbox/games/"
                                     "snes/Legend of Zelda, The - A Link to "
                                     "the Past (USA).sfc")
    ap.add_argument("--src", default=os.path.expanduser("~/z3work/zelda3"))
    ap.add_argument("--out-dir", default="Core")
    args = ap.parse_args()

    rom = Rom(args.rom)
    walkable, ledges, blocked = load_behaviours(args.src)
    ledge_ns = ledges.get("North", set()) | ledges.get("South", set()) | \
        ledges.get("NorthDiagonal", set()) | ledges.get("SouthDiagonal", set())
    ledge_ew = ledges.get("EastWest", set()) | \
        ledges.get("NorthDiagonal", set()) | ledges.get("SouthDiagonal", set())
    ledge_all = ledge_ns | ledge_ew
    m32tab = load_map32_table(args.src)
    map16tab = rom.words(0x8F8000, 3752 * 4)
    map8attr = list(rom.bytes(0x8E9459, 512))

    grids = {}
    region_counts = {}
    for screen in range(CELLS * WORLDS):
        walk, ledge = screen_tiles(rom, screen, m32tab, map16tab, map8attr,
                                   walkable, ledge_ns, ledge_ew)
        region, count = flood_regions(walk)
        grids[screen] = (walk, ledge, region)
        region_counts[screen] = count

    # Area ownership from the game's own head table (ROM 0x82A5EC).
    head = [rom.byte(0x82A5EC + c) == c for c in range(CELLS)]
    owner = []
    for c in range(CELLS):
        row, col = divmod(c, 8)
        o = None
        for cand in (c, c - 1, c - 8, c - 9):
            crow, ccol = divmod(cand, 8) if cand >= 0 else (-1, -1)
            if cand >= 0 and head[cand] and crow <= row and ccol <= col \
                    and row - crow <= 1 and col - ccol <= 1:
                o = cand
                break
        if o is None:
            raise SystemExit("cell %d has no owning head" % c)
        owner.append(o)
    if sorted(set(owner)) != [c for c in range(CELLS) if head[c]]:
        raise SystemExit("ownership does not partition the 64 cells")

    all_gaps = {}
    for screen in range(CELLS * WORLDS):
        all_gaps[screen] = find_gaps(screen, grids)

    # ---- statistics + spot checks -------------------------------------
    total_gaps = sum(len(g) for g in all_gaps.values())
    ledge_gaps = sum(1 for g in all_gaps.values() for x in g if x[4])
    multi = sum(1 for s in range(128) if region_counts[s] > 1)
    print("screens=128 gaps=%d ledge_gaps=%d multi_region_screens=%d"
          % (total_gaps, ledge_gaps, multi))
    assert owner[0x2C] == 0x2C, "Link's house cell must own itself"

    def reachable_areas(start_screen):
        """Areas reachable from a screen over (screen, region) states."""
        seen = set()
        start_regions = {g[2] for g in all_gaps[start_screen]} | {0}
        q = deque()
        for r in start_regions:
            q.append((start_screen, r))
            seen.add((start_screen, r))
        areas = set()
        while q:
            s, r = q.popleft()
            areas.add(owner[s % CELLS] + (s // CELLS) * CELLS)
            for (side, _mid, rh, rt, _ledge) in all_gaps[s]:
                if rh != r:
                    continue
                ns = s + DELTA[side]
                if (ns, rt) not in seen:
                    seen.add((ns, rt))
                    q.append((ns, rt))
        return areas

    lw = reachable_areas(0x2C)
    for area, name in ((0x18, "Kakariko"), (0x1E, "Eastern Palace"),
                       (0x1B, "Hyrule Castle"), (0x30, "Desert of Mystery"),
                       (0x35, "Lake Hylia"), (0x05, "Death Mountain East"),
                       (0x00, "Lost Woods"), (0x13, "Sanctuary")):
        print("light: %-18s %s" % (name, "reachable" if area in lw else "UNREACHABLE"))
    assert 0x18 in lw and 0x1E in lw, "design spot-checks must pass"
    dw = reachable_areas(0x5B)
    for area, name in ((0x40, "Skull Woods"), (0x43, "Ganon's Tower"),
                       (0x47, "Turtle Rock"), (0x58, "Thieves' Town"),
                       (0x5B, "Pyramid of Power"), (0x5E, "Palace of Darkness"),
                       (0x70, "Misery Mire"), (0x75, "Ice Palace shore"),
                       (0x7B, "Swamp Palace")):
        print("dark:  %-18s %s" % (name, "reachable" if area in dw else "UNREACHABLE"))

    # ---- emit ----------------------------------------------------------
    rows = []
    offsets = []
    flat = []
    for screen in range(CELLS * WORLDS):
        offsets.append(len(flat))
        for (side, mid, rh, rt, ledge) in all_gaps[screen]:
            flat.append((side, min(2, (mid * 3) // 32), rh, rt,
                         DELTA[side], ledge))
    offsets.append(len(flat))

    out = [HEADER_CPP]
    out.append("const uint8_t kAreaOf[128] = {")
    for w in range(WORLDS):
        out.append("    " + ", ".join(str(owner[c] + w * CELLS) for c in range(CELLS)) + ",")
    out.append("};\n")
    out.append("const uint16_t kGapOffset[129] = {")
    for i in range(0, 129, 8):
        out.append("    " + ", ".join(str(v) for v in offsets[i:i + 8]) + ",")
    out.append("};\n")
    out.append("const AlttpGap kGaps[%d] = {" % len(flat))
    for g in flat:
        out.append("    { %d, %d, %d, %d, %d, %d }," % g)
    out.append("};\n")
    out.append("} // namespace\n")
    out.append("const uint8_t kRegionCount[128] = {")
    for i in range(0, 128, 16):
        out.append("    " + ", ".join(str(region_counts[s]) for s in range(i, i + 16)) + ",")
    out.append("};\n")
    # Walkable bit per map16 id, same any-subtile rule as screen_tiles.
    n16 = len(map16tab) // 4
    bits = bytearray((n16 + 7) // 8)
    for m16 in range(n16):
        for q in range(4):
            if map8attr[map16tab[m16 * 4 + q] & 0x1FF] in walkable:
                bits[m16 >> 3] |= 1 << (m16 & 7)
                break
    out.append("const uint8_t kMap16Walkable[%d] = {" % len(bits))
    for i in range(0, len(bits), 16):
        out.append("    " + ", ".join("0x%02x" % b for b in bits[i:i + 16]) + ",")
    out.append("};\n")
    # Steerable bit per map16 id: the collision-shrunk steering mask.
    # All four subtiles walk-on, and none of them a spike, warp or
    # entrance subtile (see load_behaviours for the groups).
    sbits = bytearray((n16 + 7) // 8)
    for m16 in range(n16):
        attrs = [map8attr[map16tab[m16 * 4 + q] & 0x1FF] for q in range(4)]
        if all(a in walkable for a in attrs) and \
                not any(a in blocked for a in attrs):
            sbits[m16 >> 3] |= 1 << (m16 & 7)
    out.append("const uint8_t kMap16Steerable[%d] = {" % len(sbits))
    for i in range(0, len(sbits), 16):
        out.append("    " + ", ".join("0x%02x" % b for b in sbits[i:i + 16]) + ",")
    out.append("};\n")
    # Passable bit per map16 id: the fallback mask. At least three of
    # four subtiles walk-on and still no spike, warp or entrance
    # subtile — Link's body is narrower than a tile, so stairs and
    # cliff paths whose fourth subtile is the face are traversable,
    # and the hazard exclusion (the mask's safety property) is
    # unchanged. Measured need: the climb into Kakariko from the
    # south runs over such tiles and has no all-four-subtiles route.
    pbits = bytearray((n16 + 7) // 8)
    for m16 in range(n16):
        attrs = [map8attr[map16tab[m16 * 4 + q] & 0x1FF] for q in range(4)]
        if sum(1 for a in attrs if a in walkable) >= 3 and \
                not any(a in blocked for a in attrs):
            pbits[m16 >> 3] |= 1 << (m16 & 7)
    out.append("const uint8_t kMap16Passable[%d] = {" % len(pbits))
    for i in range(0, len(pbits), 16):
        out.append("    " + ", ".join("0x%02x" % b for b in pbits[i:i + 16]) + ",")
    out.append("};\n")
    out.append("uint8_t AlttpAreaOfScreen(uint8_t screen)\n{\n"
               "    return screen < 128 ? kAreaOf[screen] : screen;\n}\n")
    out.append("uint8_t AlttpScreenRegionCount(uint8_t screen)\n{\n"
               "    return screen < 128 ? kRegionCount[screen] : 0;\n}\n")
    out.append("bool AlttpMap16Walkable(uint16_t map16)\n{\n"
               "    return map16 < %d && (kMap16Walkable[map16 >> 3] >> (map16 & 7)) & 1;\n}\n" % n16)
    out.append("bool AlttpMap16Steerable(uint16_t map16)\n{\n"
               "    return map16 < %d && (kMap16Steerable[map16 >> 3] >> (map16 & 7)) & 1;\n}\n" % n16)
    out.append("bool AlttpMap16Passable(uint16_t map16)\n{\n"
               "    return map16 < %d && (kMap16Passable[map16 >> 3] >> (map16 & 7)) & 1;\n}\n" % n16)
    out.append("int AlttpScreenGaps(uint8_t screen, const AlttpGap** out)\n{\n"
               "    if (screen >= 128) { *out = nullptr; return 0; }\n"
               "    int n = (int) kGapOffset[screen + 1] - (int) kGapOffset[screen];\n"
               "    *out = n ? &kGaps[kGapOffset[screen]] : nullptr;\n"
               "    return n;\n}\n")
    out.append("} // namespace oga\n")

    with open(os.path.join(args.out_dir, "alttp_graph.h"), "w") as f:
        f.write(HEADER_H)
    with open(os.path.join(args.out_dir, "alttp_graph.cpp"), "w") as f:
        f.write("\n".join(out))
    print("wrote Core/alttp_graph.h and Core/alttp_graph.cpp (%d gaps)" % len(flat))
    return 0


if __name__ == "__main__":
    sys.exit(main())
