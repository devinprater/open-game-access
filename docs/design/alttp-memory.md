# A Link to the Past — the reader's memory map

Every address here is used by `Core/alttp_adapter.cpp`. The names come from the
decompilation (`~/z3work/zelda3/src/variables.h`, where `g_ram` is the whole
128 KiB of SNES work RAM, so an offset maps to `0x7E0000 + offset`). The values
are what the console actually read, not what the names claim.

## The map

| Purpose | Decomp name | Address | Width |
|---|---|---|---|
| module (menu state) | `main_module_index` | `0x7E0010` | 8 |
| submodule | `submodule_index` | `0x7E0011` | 8 |
| frame counter | `frame_counter` | `0x7E001A` | 8 |
| indoors | `player_is_indoors` | `0x7E001B` | 8 |
| Link Y | `link_y_coord` | `0x7E0020` | 16 |
| Link X | `link_x_coord` | `0x7E0022` | 16 |
| overworld screen | `overworld_screen_index` | `0x7E008A` | 16 |
| dungeon room | `dungeon_room_index` | `0x7E00A0` | 16 |
| overworld area | `overworld_area_index` | `0x7E040A` | 16 |
| rupees | `link_rupees_actual` | `0x7EF362` | 16 |
| heart capacity | `link_health_capacity` | `0x7EF36C` | 8 |
| health now | `link_health_current` | `0x7EF36D` | 8 |
| magic | `link_magic_power` | `0x7EF36E` | 8 |

## Measured 2026-10-17 — boot and title

Method: `bash scripts/snes-alttp-proof.sh --memory` — 600 frames, uncapped,
Start held from frame 120 to 160, one line every 60 frames.

**Frame 0, before the game initialises its RAM:**

```
mod=190 sub=153 in=183 x=47243 y=59151 scr=7850 room=63075 area=52452
hp=86/127 mag=201 rup=8023
```

⛔ This is the important measurement. Capacity `0x7F` and health `0x56` are
inside every naive "sane range" test, and the coordinates are non-zero, so a
reader that only range-checks would call this a playable game and speak from
it. The game's real capacities are `0x18` (three hearts) through `0xA0` (twenty)
in steps of eight, so `(capacity - 0x18) % 8 == 0` is the rule that refuses
garbage while accepting every real maximum. `StateSane()` uses it.

**Frames 60–600, title and attract:**

```
mod=0 sub=1..4 in=0 x=0 y=0 scr=0 room=0 area=0 hp=0/0 mag=0 rup=0
```

Start at frame 120 did not reach a playable state within 600 frames. Nothing in
the reader map has a meaningful gameplay value yet.

## Not yet measured

- `overworld_area_index`, `overworld_screen_index`, `dungeon_room_index`,
  `player_is_indoors` and the health values **in gameplay**. They need a save
  file or scripted play past the file-select screen. Until that is recorded
  here, the adapter speaks the numbers as numbers, and Task 4's names are not
  claimed for an unmeasured id.
- The dungeon identifier (which dungeon the room number belongs to).
- Menu cursors and the dialogue message bytes (Tasks 5–7).

## Measured 2026-10-17 — in gameplay (Link's house)

Method: `bash scripts/snes-alttp-proof.sh --gameplay` — the probe drives Start at
the title, A on the file slot and Start at name entry, then walks for 1800
frames. This reached a real game for the first time.

```
module 7 sub 15, then module 7 sub 0     (gameplay; module 14 sub 2 during text)
indoors=1  room=260  area=0  screen=0
x=2368 y=8538 (later 2344/8640)          hp=24/24  magic=0  rupees=0
```

What this confirms:

- **Health**: capacity 24 (`0x18`) exactly three hearts, current 24. The
  validation's step-of-eight rule accepts it, as designed.
- **`player_is_indoors`** is 1 in Link's house.
- ⛔ **`dungeon_room_index` is used by INDOOR ROOMS GENERALLY, not only
  dungeons** — a house reads as room 260. The speech therefore says
  "Indoors, room 260", not "Dungeon room", and the index alone does not say
  which dungeon the player is in.
- `overworld_area_index` and `overworld_screen_index` are 0 while indoors.

Still not measured: outdoors area and screen, the dungeon identifier, the
file-select and item-menu cursors, and the dialogue message bytes. The scripted
walk could not leave the house in 1800 frames — the opening sequence still gates
movement. Those need a real save file from a playthrough (`--gameplay` writes the
`.srm` when the game itself saves), after which the same probe can be pointed at
that save.

## Measured 2026-10-17 — with a downloaded save (stateofsave "Intro Complete")

A public 8192-byte `.srm` (stateofsave.com, "Intro Complete", described as just
after the rescue of Zelda) was placed at
`Saves/Legend of Zelda, The - A Link to the Past (USA).srm` and the probe booted
with it. The save is real and Mesen loaded it:

```
module 14 sub 1  indoors=1  room=260  area=0 screen=0
hp=24/32  magic=128  rupees=205
```

So the reader now has in-game evidence for all three status figures: four
hearts of capacity (`0x20`), 24 eighths current, a full magic meter (`0x80`),
and 205 rupees. The indoor room id is 260 again.

⛔ The save sits inside a message/cutscene state (`module 14 sub 1`). Scripted
A, B and Start presses over 700+ frames did not advance it, and the coordinates
never moved, so no OUTDOOR area or screen id was observed. `module 7` is
gameplay, `module 14` is the text/cutscene module; both were seen.

Names still wait on a save at a playable point (outdoors), not on an address
question: the ids are readable, they have simply never been observed outdoors.
