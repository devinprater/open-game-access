#!/usr/bin/env python3
"""Generate Core/alttp_rooms.cpp from the zelda3 room database.

The game's room contents are not in work RAM: they live in the cartridge. The
decompilation carries one YAML file per room, extracted from that cartridge, so
this script turns the FACTS we need (chest count, big chest, the editor's puzzle
note) into a small table the adapter can read at runtime. The YAML stays outside
this repo; only the derived summary is committed.

Usage:
  python3 scripts/gen-alttp-rooms.py [--src ~/z3work/zelda3/assets/dungeon]
"""
import argparse
import glob
import os
import re
import sys

HEADER = '''/*
 * alttp_rooms.cpp — what each dungeon room holds, generated; do not edit.
 *
 * Source: the zelda3 decompilation's per-room database (assets/dungeon/*.yaml),
 * which is itself extracted from the USA 1.0 cartridge. Only derived facts are
 * here: how many chests, whether one is a big chest, and the editor's own
 * one-line note about the room's puzzle. No room layout, sprite list or ROM data
 * is copied.
 *
 * Regenerate with scripts/gen-alttp-rooms.py; the room index is the game's own
 * dungeon_room_index (0..319).
 */
#include "alttp_rooms.h"

namespace oga {
namespace {

struct RoomRow { uint8_t chests; uint8_t big; const char* note; };

const RoomRow kRooms[320] = {
'''

FOOTER = '''};

} // namespace

const RoomNote* AlttpRoomNote(uint16_t room)
{
    static RoomNote out;
    if (room >= 320) return nullptr;
    const RoomRow& row = kRooms[room];
    if (row.chests == 0 && !row.note) return nullptr;
    out.chests = row.chests;
    out.big = row.big != 0;
    out.note = row.note;
    return &out;
}

} // namespace oga
'''

HEADER_H = '''/*
 * alttp_rooms.h — what a dungeon room holds, as facts.
 */
#ifndef OGA_ALTTP_ROOMS_H
#define OGA_ALTTP_ROOMS_H

#include <stdint.h>

namespace oga {

struct RoomNote {
    uint8_t chests;      // how many chests the room holds
    bool big;            // at least one of them is a big chest
    const char* note;    // the editor's one-line puzzle note, or nullptr
};

// nullptr when the room has nothing to say (no chests and no note), and for a
// room index outside the game's 0..319 table.
const RoomNote* AlttpRoomNote(uint16_t room);

} // namespace oga

#endif
'''


def clean(value):
    if value is None:
        return None
    text = str(value).strip()
    if not text or text == "None":
        return None
    # The database has a few numeric placeholders in the effect field.
    if re.fullmatch(r"[0-9]+", text):
        return None
    # Quadrant prefixes are compass hints for an editor, not prose for a player.
    text = re.sub(r"^(NW|NE|SW|SE|N|S|E|W)\s+", "", text)
    return text.strip().rstrip(".")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.expanduser("~/z3work/zelda3/assets/dungeon"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Core"))
    args = ap.parse_args()

    rows = [None] * 320
    files = glob.glob(os.path.join(args.src, "dungeon-*.yaml"))
    if not files:
        sys.exit("no dungeon-*.yaml under %s" % args.src)

    import yaml  # PyYAML, present in the decomp's requirements

    for path in files:
        m = re.search(r"dungeon-(\d+)\.yaml$", path)
        if not m:
            continue
        room = int(m.group(1))
        if room >= 320:
            continue
        data = yaml.safe_load(open(path)) or {}
        header = data.get("Header") or {}
        chests = data.get("Chests") or []
        big = 0
        for chest in chests:
            if isinstance(chest, str) and chest.endswith("!"):
                big = 1
        note = clean(header.get("tag0")) or clean(header.get("effect"))
        rows[room] = (len(chests), big, note)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "alttp_rooms.h"), "w") as f:
        f.write(HEADER_H)

    with open(os.path.join(args.out, "alttp_rooms.cpp"), "w") as f:
        f.write(HEADER)
        for i, row in enumerate(rows):
            if row is None:
                f.write("    { 0, 0, nullptr },  // %d: no room data\n" % i)
                continue
            chests, big, note = row
            note_c = "nullptr" if not note else '"%s"' % note.replace('"', '\\"')
            f.write("    { %d, %d, %s },  // %d\n" % (chests, big, note_c, i))
        f.write(FOOTER)

    filled = sum(1 for r in rows if r and (r[0] or r[2]))
    print("wrote %s: %d rooms with a note or chest, of 320" %
          (os.path.join(args.out, "alttp_rooms.cpp"), filled))


if __name__ == "__main__":
    main()
