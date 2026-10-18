# A Link to the Past guidance — first design

Started 2026-10-17. The reader already speaks areas, menus, dialogue, dungeons
and status. Guidance is the next open item, and this is its own project: the
game has no named paths, and a wrong direction is worse than a number.

## What the reader already has

- 82 named overworld areas keyed by the live `overworld_area_index`.
- Area-entry announcements on module 9 outdoors.
- Link's coordinates at `0x7E0022` (x) and `0x7E0020` (y), 16-bit.
- The dungeon id at `0x7E040C` (0xFF = none).

What it does not have: any idea which way is walkable. That is the whole
problem.

## Measured facts to build on

- The game derives the area from the coordinates itself, in `overworld.c`:
  `current_area_of_player = (y & 0x1E00) >> 5 | (x & 0x1E00) >> 8`
  at `0x7E0700`. Table lookups use `>> 1`, so an area id is a screen index
  doubled.
- `overworld_area_index` (`0x7E040A`) is `screen & 0x3F | darkworld`, which is
  what the name table is keyed on. Measured: 0x2C Link's House, 0x40 Skull
  Woods.
- Some areas cover two by two screens (`overworld_area_is_big`), and
  `kOverworld_OffsetBaseX/Y` carry their base offsets. A screen-centre
  calculation must use that table, not assume 512 by 512 for every area.

## Three options

1. **Beacon** — say the compass direction and rough screen distance to a target.
   Wording stays honest: "North-east, about three screens." It never claims a
   path exists. This is small, measurable, and useful on open ground.
2. **Screen graph** — nodes are screens, edges are transitions a walking Link
   can take. Needs collision or entrance data, or a lot of scripted walking.
   This is what real route guidance needs.
3. **Talking map** — read the map screen's cursor and name the area under it.
   The cursor is Mode 7 scroll offsets with two zoom levels, so the maths comes
   first.

## Recommended first task: the beacon

- Append `Command::Guide` (and whatever target cycling it needs) to the C ABI;
  never reorder existing values. Add the Swift `AdapterCommand` rows and
  `supported(adapterID:)` entry.
- Targets: start with the named landmarks a player actually walks to — Link's
  House, Sanctuary, Hyrule Castle, Kakariko Village, Eastern Palace, Desert of
  Mystery, Lake Hylia, Death Mountain East, Ganon's Tower. Cycle with the
  player's command; the UI decides how.
- Answer format: "North-east, about three screens." / "Here." when the target
  screen is the current one. Compass names from the sign of dx, dy.
- Refuse indoors, in menus, while not ready, and for an unknown target. Silence
  is better than a direction from a house.

## Measurement plan before speaking

1. Confirm world coordinates per screen by walking one screen and diffing x/y.
2. Confirm the centre of one large area and one small area against
   `kOverworld_OffsetBaseX/Y`.
3. Prove one direction against the save: Link's House (0x2C) to Hyrule Castle
   (0x1B) must come out north, and the distance must match the map.

## Not in this design

Walking route, ledge and river crossings, dungeon routes, and anything that
needs the screen graph. Those get their own measurement pass after the beacon
is honest.
