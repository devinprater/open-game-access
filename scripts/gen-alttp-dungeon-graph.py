#!/usr/bin/env python3
"""gen-alttp-dungeon-graph.py -- the dungeon room graph, generated data.

Reads the zelda3 decompilation's per-room database (assets/dungeon/*.yaml)
and emits Core/alttp_dungeon_graph.{h,cpp}: per room, the directed travel
edges a player can actually take, the room's connected component, its
points of interest (entrance / big chest / boss) and its door-gating tag.

Edge rules (all measured from the game's own code, src/dungeon.c):
- Doors: a door ships only when the neighbouring room answers with a
  passage door on the opposite wall (the both-sides rule). The edge
  class comes from the DEPARTURE room's door type: free, shutter,
  small-key (0x1C), big-key (0x1E - it tests link_bigkey), bombable
  (0x28, 48). Non-passage types (entrances, exits, stair masks) never
  ship as door edges.
- Stairs: header destinations ship only when mutually confirmed (the
  destination room's own stair list points back) - the header carries
  inert template values in rooms with no staircase, so an unconfirmed
  dest is not evidence of travel.
- Pits: a one-way edge where the room holds an open-hole object
  (FE6-Hole / A4-Hole) and hole0_dest names another room.

The generator prints the closure invariant per component: from every
entrance room, are the component's big chest and boss rooms reachable
with all edge kinds allowed? A shortfall is a finding, not a ship.
"""
import argparse, glob, os, re, sys
from collections import defaultdict, deque

FREE = {0, 2, 4, 8, 26, 50, 54, 56, 64, 70}
SHUTTER = {24, 68, 72, 74}
LOCKED = {28}          # 0x1C SmallKeyDoor
BIGKEY = {30}          # 0x1E, tests link_bigkey (dungeon.c)
BOMB = {40, 42, 46, 48}  # 0x28 BreakableWall, its 0x2A/0x2E variants, LgExplosion
PASSAGE = FREE | SHUTTER | LOCKED | BIGKEY | BOMB

KIND = {}
for t in FREE: KIND[t] = 0
for t in SHUTTER: KIND[t] = 1
for t in LOCKED: KIND[t] = 2
for t in BIGKEY: KIND[t] = 3
for t in BOMB: KIND[t] = 4
KIND_STAIRS = 5
KIND_PIT = 6

DELTA = {0: -16, 1: 16, 2: -1, 3: 1}
OPP = {0: 1, 1: 0, 2: 3, 3: 2}
BOSS_ROOMS = {200, 51, 7, 32, 6, 90, 41, 144, 222, 164, 172, 13, 0}
HOLE_OBJECTS = ("FE6-Hole", "A4-Hole")


def gate_class(header):
    text = " ".join(str(header.get(k) or "") for k in ("tag0", "tag1"))
    if "Switch" in text:
        return 2
    if "Kill" in text or "Clear" in text:
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.expanduser("~/z3work/zelda3/assets/dungeon"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Core"))
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()

    import yaml
    rooms = {}
    for path in glob.glob(os.path.join(args.src, "dungeon-*.yaml")):
        m = re.search(r"dungeon-(\d+)\.yaml$", path)
        if not m:
            continue
        room = int(m.group(1))
        if room < 320:
            rooms[room] = yaml.safe_load(open(path)) or {}
    if len(rooms) < 300:
        sys.exit("only %d rooms parsed" % len(rooms))

    doors = {}       # room -> list of (dir, pos, type)
    dests = {}       # room -> [hole, stair0..3] room ids
    gates = {}
    poi = defaultdict(int)   # 1 entrance, 2 big chest, 4 boss
    entrance_name = {}
    has_hole = {}
    for room, data in rooms.items():
        header = data.get("Header") or {}
        doors[room] = [(d["dir"], d["pos"], d["type"])
                       for d in (data.get("Layer3.doors") or [])]
        dests[room] = [(header.get(k) or [0, 0])[0]
                       for k in ("hole0_dest", "stair0_dest", "stair1_dest",
                                 "stair2_dest", "stair3_dest")]
        gates[room] = gate_class(header)
        for chest in data.get("Chests") or []:
            if isinstance(chest, str) and chest.endswith("!"):
                poi[room] |= 2
        if room in BOSS_ROOMS:
            poi[room] |= 4
        for e in data.get("Entrances") or []:
            poi[room] |= 1
            entrance_name[room] = e.get("name", "")
        names = [o.get("n", "") for layer in ("Layer1", "Layer2")
                 for o in (data.get(layer) or []) if isinstance(o, dict)]
        has_hole[room] = any(n.startswith(HOLE_OBJECTS) for n in names)

    def neighbour(room, d):
        if d == 0 and room < 16: return None
        if d == 1 and room >= 304: return None
        if d == 2 and room % 16 == 0: return None
        if d == 3 and room % 16 == 15: return None
        return room + DELTA[d]

    edges = defaultdict(list)   # room -> [(to, kind, dir, pos)]
    diag = defaultdict(int)
    for room in range(320):
        for (d, pos, t) in doors.get(room, []):
            if t not in PASSAGE:
                continue
            n = neighbour(room, d)
            if n is None:
                diag["door_off_grid"] += 1
                continue
            # The game's own pairing (Dungeon_CheckAdjacentRoomsForOpenDoors
            # and its kLookup/kLookup2 tables): a door at pos p on one wall
            # is answered by the door at pos p+6 (p<6) or p-6 (p>=6) on the
            # opposite wall of the neighbouring room - the two walls are
            # measured from opposite ends. Confirmed in the data: the
            # Eastern Palace bombable pair 185-E@6 <-> 186-W@0, both type
            # 0x28, matches under this rule and no other.
            want = pos + 6 if pos < 6 else pos - 6
            answers = [p for (dd, p, tt) in doors.get(n, [])
                       if dd == OPP[d] and tt in PASSAGE and p == want]
            if not answers:
                diag["one_sided"] += 1
                continue
            diag["paired_flip"] += 1
            edges[room].append((n, KIND[t], d, pos))

    # Staircases: a header dest ships only when it is mutually confirmed
    # AND both rooms actually contain a staircase (a floor-stair object or
    # a stair-mask door). The header carries inert template values - whole
    # dest tails repeat across unrelated dungeons - so a dest alone, or
    # even a mutual pair of dests between rooms with no staircase, is not
    # evidence of travel. (The genuine pair 9<->74, Palace of Darkness
    # basement <-> entrance hall, has complementary Door Staircase Up /
    # Down objects at both ends.)
    def has_stairs(room):
        if any(t in (32, 34, 36, 38) for (_d, _p, t) in doors.get(room, [])):
            return True
        data = rooms.get(room) or {}
        for layer in ("Layer1", "Layer2"):
            for o in (data.get(layer) or []):
                if isinstance(o, dict) and "tair" in o.get("n", ""):
                    return True
        return False

    stair_dests = {r: dests[r][1:] for r in range(320)}
    for room in range(320):
        for dest in stair_dests[room]:
            if dest == 0 or dest == room or dest >= 320:
                continue
            if not (has_stairs(room) and has_stairs(dest)):
                diag["stair_no_object"] += 1
                continue
            if room in stair_dests.get(dest, []):
                if not any(e[0] == dest and e[1] == KIND_STAIRS for e in edges[room]):
                    edges[room].append((dest, KIND_STAIRS, 0xFF, 0))
                    diag["stair_mutual"] += 1
            else:
                diag["stair_unconfirmed"] += 1

    # Components from doors + stairs first: a pit drop ships only when it
    # lands inside the same component. A hole dest that leaves the
    # component is template residue (Skull Woods' drop-in rooms carry
    # hole dest 9, a Palace of Darkness room - palette-verified different
    # dungeon) and is reported, not shipped.
    parent0 = list(range(320))
    def find0(x):
        while parent0[x] != x:
            parent0[x] = parent0[parent0[x]]
            x = parent0[x]
        return x
    for room in range(320):
        for (to, *_rest) in edges[room]:
            a, b = find0(room), find0(to)
            if a != b:
                parent0[a] = b
    for room in range(320):
        hole = dests[room][0]
        if hole and hole != room and hole < 320 and has_hole.get(room):
            if find0(hole) == find0(room):
                edges[room].append((hole, KIND_PIT, 0xFE, 0))
                diag["pit_edges"] += 1
            else:
                diag["pit_cross_component"] += 1

    # components over shipped edges, undirected
    parent = list(range(320))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for room in range(320):
        for (to, *_rest) in edges[room]:
            a, b = find(room), find(to)
            if a != b:
                parent[a] = b
    comp = [find(r) for r in range(320)]
    # normalise component ids to the smallest room in the component
    compid = {}
    for r in range(320):
        compid.setdefault(comp[r], r)
    comp = [compid[c] for c in comp]

    # A room entered from the overworld as a dungeon's "Boss Lair" is the
    # boss room when its component has no boss-list room: Skull Woods'
    # lair (room 89) is where Mothula is fought, and the exit list's
    # Skull Woods entry names a room in another dungeon's palette.
    by_comp_pre = defaultdict(list)
    for r in range(320):
        by_comp_pre[comp[r]].append(r)
    for c, members in by_comp_pre.items():
        if any(poi[r] & 4 for r in members):
            continue
        for r in members:
            if (poi[r] & 1) and "Boss" in entrance_name.get(r, ""):
                poi[r] |= 4

    # closure invariant: per component with an entrance, directed reach
    by_comp = defaultdict(list)
    for r in range(320):
        by_comp[comp[r]].append(r)
    print("components: %d" % len(by_comp))
    failures = 0
    for c in sorted(by_comp):
        members = by_comp[c]
        entrances = [r for r in members if poi[r] & 1]
        targets = [r for r in members if poi[r] & 6]
        if not entrances:
            if targets:
                print("  [FIND] component %3d (%2d rooms) has targets %s but NO entrance room"
                      % (c, len(members), targets))
            continue
        reach = set()
        dq = deque(entrances)
        reach.update(entrances)
        while dq:
            cur = dq.popleft()
            for (to, *_rest) in edges[cur]:
                if to not in reach:
                    reach.add(to)
                    dq.append(to)
        missing = [r for r in targets if r not in reach]
        names = sorted({entrance_name.get(e, "?") for e in entrances})
        status = "OK " if not missing else "FAIL"
        if missing:
            failures += 1
        print("  [%s] component %3d (%2d rooms) entrances=%s targets=%s missing=%s"
              % (status, c, len(members), entrances, targets, missing))
        for nm in names:
            print("        entrance name: %s" % nm)
    print("closure failures: %d" % failures)
    print("diagnostics:", dict(diag))
    total_edges = sum(len(v) for v in edges.values())
    print("edges shipped: %d" % total_edges)
    if args.check_only:
        return

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "alttp_dungeon_graph.h"), "w") as f:
        f.write(HEADER_H)
    with open(os.path.join(args.out, "alttp_dungeon_graph.cpp"), "w") as f:
        f.write(HEADER_CPP)
        offs = []
        flat = []
        for room in range(320):
            offs.append(len(flat))
            for (to, kind, d, pos) in sorted(edges[room]):
                flat.append((to, kind, d, pos))
        offs.append(len(flat))
        f.write("const uint16_t kEdgeOffset[321] = {\n")
        for i in range(0, 321, 8):
            f.write("    " + ", ".join(str(x) for x in offs[i:i + 8]) + ",\n")
        f.write("};\n\n")
        f.write("const AlttpRoomEdge kEdges[%d] = {\n" % len(flat))
        for (to, kind, d, pos) in flat:
            f.write("    { %d, %d, %d, %d },\n" % (to, kind, d, pos))
        f.write("};\n\n")
        f.write("const uint16_t kComponent[320] = {\n")
        for i in range(0, 320, 8):
            f.write("    " + ", ".join(str(x) for x in comp[i:i + 8]) + ",\n")
        f.write("};\n\n")
        f.write("const uint8_t kPoi[320] = {\n")
        for i in range(0, 320, 16):
            f.write("    " + ", ".join(str(poi[r]) for r in range(i, i + 16)) + ",\n")
        f.write("};\n\n")
        f.write("const uint8_t kGate[320] = {\n")
        for i in range(0, 320, 16):
            f.write("    " + ", ".join(str(gates.get(r, 0)) for r in range(i, i + 16)) + ",\n")
        f.write("};\n")
        f.write(FOOTER_CPP)
    print("wrote alttp_dungeon_graph.{h,cpp}: %d edges" % len(flat))


HEADER_H = """/*
 * alttp_dungeon_graph.h - the dungeon room graph, generated data.
 *
 * Per room (dungeon_room_index 0..319), the directed travel edges the
 * game's own door and header data prove: doors confirmed from both
 * sides, staircases confirmed from both ends, and open-pit drops.
 * Generated offline by scripts/gen-alttp-dungeon-graph.py from the
 * decompilation's room database; no room data ships, only the edges.
 */
#ifndef OGA_ALTTP_DUNGEON_GRAPH_H
#define OGA_ALTTP_DUNGEON_GRAPH_H

#include <stdint.h>

namespace oga {

struct AlttpRoomEdge {
    uint16_t to;    /* destination room */
    uint8_t kind;   /* 0 free door, 1 shutter, 2 small-key door,
                       3 big-key door, 4 bombable, 5 stairs, 6 pit */
    uint8_t dir;    /* door direction 0N 1S 2W 3E; 0xFF stairs, 0xFE pit */
    uint8_t pos;    /* door position along the wall, 0..11 */
};

/* The room's travel edges; returns the count and sets *out. */
int AlttpRoomEdges(uint16_t room, const AlttpRoomEdge** out);

/* The connected component the room belongs to (its smallest room id). */
uint16_t AlttpRoomComponent(uint16_t room);

/* Points of interest in the room: bit 1 entrance, bit 2 big chest,
 * bit 4 boss room. */
uint8_t AlttpRoomPoi(uint16_t room);

/* Whether the room's doors are gated: 0 no, 1 cleared-room, 2 switch. */
uint8_t AlttpRoomGate(uint16_t room);

} // namespace oga

#endif
"""

HEADER_CPP = """/*
 * alttp_dungeon_graph.cpp - generated by scripts/gen-alttp-dungeon-graph.py.
 * Do not edit by hand; regenerate from the decompilation's room database.
 */
#include "alttp_dungeon_graph.h"

namespace oga {
namespace {

"""

FOOTER_CPP = """
} // namespace

int AlttpRoomEdges(uint16_t room, const AlttpRoomEdge** out)
{
    if (room >= 320) { *out = nullptr; return 0; }
    int n = (int) kEdgeOffset[room + 1] - (int) kEdgeOffset[room];
    *out = n ? &kEdges[kEdgeOffset[room]] : nullptr;
    return n;
}

uint16_t AlttpRoomComponent(uint16_t room)
{
    return room < 320 ? kComponent[room] : room;
}

uint8_t AlttpRoomPoi(uint16_t room)
{
    return room < 320 ? kPoi[room] : 0;
}

uint8_t AlttpRoomGate(uint16_t room)
{
    return room < 320 ? kGate[room] : 0;
}

} // namespace oga
"""

if __name__ == "__main__":
    main()
