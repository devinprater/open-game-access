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

**DONE 2026-10-18.** `Command::Guide` is number 41, appended after Status, and the
Swift `AdapterCommand` carries the same value with the button title "Guide".

- It cycles nine landmarks: Link's House, Sanctuary, Hyrule Castle, Kakariko,
  Eastern Palace, Desert of Mystery, Lake Hylia, Death Mountain East, Lost Woods.
  **The first press aims at the current objective**, and the cycle re-aims when
  the story stage changes, so the first answer is where to go next; later presses
  walk the whole list.
- It speaks "Guide: Hyrule Castle, north-west, about two screens away." and
  "Guide: Link's House. You are here." when the area id matches.
- It refuses indoors ("Guidance works outside."), in a menu ("Close the menu
  first."), and in the dark world ("Guidance covers the light world only.").
- Distance is octile in 512-unit screens, rounded; direction is eight-way with a
  3:1 ratio so a small offset does not wobble between north and north-east.
- ⛔ Found on the way: `poke_command` still rejected any command above
  RepeatOlder, so Status (40) and Guide (41) were dead on the live path while
  their unit tests passed. The bound must move with every appended command; it
  now ends at Objective (42).

## The objective

`Command::Objective` = 42 names the current story goal and adds the bearing to
the place it happens at. The stage comes from the game's own bytes, not from a
guess about where the player should be.

- `sram_progress_flags` at `0x7EF3C6`: bit `0x01` = the uncle gave the sword,
  bit `0x04` = Zelda reached the Sanctuary, bit `0x10` = the uncle left the
  house.
- `sram_progress_indicator` at `0x7EF3C5`.
- `link_which_pendants` at `0x7EF374`; all three are `& 7 == 7`.
- **Measured on the public save:** flags `0x15`, indicator `2`, pendants `0`,
  sword `1`, standing outdoors at area `0x2C`. That reads as "Collect the three
  pendants. Sahasrahla in Kakariko knows where to start." with Kakariko to the
  north-west, five screens away.

Stages, in order:

1. No sword flag: "Find your uncle in Hyrule Castle." Target `0x1B`.
2. Sword but no Sanctuary flag: "Rescue Zelda in Hyrule Castle." Target `0x1B`.
3. Pendants incomplete: "Collect the three pendants. Sahasrahla in Kakariko knows
   where to start." Target `0x18`.
4. Pendants complete, no Master Sword: "Claim the Master Sword in the Lost
   Woods." Target `0x00`.
5. Master Sword: "Confront Agahnim in Hyrule Castle." Target `0x1B`.

Indoors it names the goal without a bearing, because room coordinates are not
world coordinates. In the dark world it refuses: none of those stages have been
measured.

## What was measured before speaking

1. The screen grid is the game's own: 512 world units per screen, and the area's
   low six bits are row times eight plus column. No new table was invented.
2. The public save's Link stands at x=2232, y=2792 in area 0x2C, which is row 5,
   column 4 — the anchor both the tests and the live proof use.
3. Two bearings were worked out by hand and matched the running game: Sanctuary
   north and three screens away, Hyrule Castle north-west and two screens away.

## What this still cannot do

Walking route, ledge and river crossings, dungeon routes, and anything that
needs the screen graph. Those get their own measurement pass after the beacon
is honest. A large area also aims at its base cell, so the last screen of the
approach may read one cell off; arrival is detected by area id, not by cell.
