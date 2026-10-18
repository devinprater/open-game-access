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

## What the decompilation says the modules are (2026-10-17)

`~/z3work/zelda3/src/misc.c` holds `kMainRouting[28]`, the module table the
game dispatches on `main_module_index`:

```
0  Module00_Intro          8  OverworldLoad        16  SpotlightOpen
1  FileSelect              9  Overworld            17  DungeonFallingEntrance
2  CopyFile               10  OverworldLoad        18  GameOver
3  KILLFile               11  Overworld            19  BossVictory_Pendant
4  NameFile               12  Unknown0             20  Attract
5  LoadFile               13  Unknown1             21  MirrorWarpFromAga
6  PreDungeon             14  Interface (HUD/menus) 22 BossVictory_Crystal
7  Dungeon                15  SpotlightClose       23  SaveAndQuit
```

⛔ **`main_module_index == 14` does NOT mean "cutscene".** It is the interface
module. The real game state is in `saved_module_for_menu` (`0x7E010C`):
measured in the downloaded save, `module=14 sub=1 real=7` — the HUD running over
indoor gameplay. `submodule_index` selects the interface handler
(`kMessagingSubmodules`): 1 = `Hud_Module_Run`, 2 = `RenderText`, 3 = dungeon
map, 11 = save menu.

**Consequence for the reader:** "what screen am I on" must read
`saved_module_for_menu` when `main_module_index == 14`, and the module table
above is the vocabulary for that answer.

## Dialogue is readable from RAM, no gameplay needed

`messaging_text_buffer` is `g_ram+0x11200` = **`0x7F1200`**: the message text
AFTER the game has decoded it into font tile indices. Measured in the
downloaded save (`dialogue_message_index` `0x7E1CF0` = 388):

```
7a 00 44 12 2d 1a 2b 2d 59 05 2b 28 26 59 0b 22 27 24 51 2c 59
07 28 2e 2c 1e 75 59 59 12 2d 1a 2b 2d 59 05 2b 28 26 59 12 1a
27 1c 2d 2e 1a 2b 32 72 7f
```

Decoded with the US font order (A–Z = `0x00`–`0x19`, a–z = `0x1A`–`0x33`,
space = `0x59`, `0x51` = apostrophe):

**"Start From Link's House,"** — the save's own description text.

So a dialogue reader needs no cursor driving at all: read the message index,
read the buffer, map tiles to characters, and speak. Commands above the letter
range (`0x7A`, `0x7F`, …) terminate or decorate.

Still unknown: why that particular save refuses to move Link (module 14 sub 1
with `message=388`, `incap=0`, coordinates frozen). It is a save made mid-event,
not a reader problem.

## Outdoors, measured (2026-10-17)

The earlier "frozen save" was not frozen: the game was sitting in the interface
module. **One START press closes it** — measured: `module 14 sub 1` → `7 sub 0`.
Without that press, direction input does nothing.

With the interface closed and Down held from that save:

```
module 16 sub 1   real=9   indoors=0   x=2232 y=2792   area=44 screen=44
```

Module 16 is `SpotlightOpen` (the transition), then module 9 is `Overworld`
(`mod=9 sub=0`), and Left moved the coordinates — **overworld movement works**.
Area 44 / screen 44 is the first OUTDOOR reading. The save's own description
text ("Start From Link's House") says where that is.

So the reader's outdoor branch, "Outside, area 44, screen 44.", now rests on a
real observation. What it does not yet have is a name for area 44: the
decompilation has no area-name table (the game never shows one), so names come
from a curated table in `Core/alttp_names.cpp`, with numbers as the fallback.

## Dialogue measured and spoken

`alttp_text.cpp` decodes the message buffer with the measured font order and the
decomp's command-length table (`kTextCommandStart_US = 0x67`). Message 388
decodes exactly to "Start From Link's House, Start From Sanctuary." and the
adapter speaks a new message index once, not once per frame.
