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
decodes exactly to "Start From Link's House, Start From Sanctuary."

⛔ **THREE THINGS THAT ONLY SHOWED UP ON THE REAL INTRO MESSAGE** (message 31 on
a new file, the dream telepathy):

1. **Unknown tiles between words are word breaks.** The measured bytes put
   unnamed values (`0x43`, `0x73`) between words, and skipping them ran the text
   together: "dungeonof the castle", "Help mePlease help me". They now become a
   space, and a space is never doubled or placed before punctuation.
2. **One message has many pages.** `dialogue_message_index` names the message,
   not the page; a page advance rewrites the buffer and keeps the index. The
   reader now hashes the first 96 bytes and speaks when the content changes,
   with one frame of settle so a half-written page is never spoken.
3. **A zero index is no message.** An empty buffer decodes to a run of "A"
   glyphs, so content alone speaks nonsense between messages; the index must be
   non-zero as well.

Measured live with `scripts/snes-alttp-proof.sh --dialogue` on a new file:

```
SAY: Help me Please help me I am a prisoner in the dungeon of the castle My name
     is Zelda The wizard Agahnim has done so
dialogue-proof=YES
```

The box is paged, so that is the first page; the rest follows as the pages turn.

## Area names: curated, never guessed

The game never displays an area name and the decompilation has no name table, so
`Core/alttp_names.cpp` is a curated table keyed by `overworld_area_index`
(`0x00`-`0x3F` light world, `0x40`-`0x7F` dark world; the game itself computes
the area as `screen & 0x3F | darkworld` in `src/overworld.c`).

Named today:

- `0x2C` **Link's House** — MEASURED: the public save whose description text is
  "Start From Link's House" loads at area `0x2C` and reports it after walking out.
- `0x13` Sanctuary, `0x14` Graveyard, `0x1B` Hyrule Castle, `0x21` Kakariko
  Village, `0x39` Desert of Mystery, `0x3A` Desert Passage — map squares named in
  The Cutting Room Floor's overworld development tables.
- `0x40/0x41/0x48/0x49` Skull Woods, `0x42-0x44/0x4B/0x4C` Death Mountain,
  `0x45/0x46/0x4D/0x4E` Death Mountain East, `0x47` Turtle Rock — same tables,
  dark world.

Everything else speaks "Outside, area N, screen S." A wrong name sends a blind
player the wrong way; a number does not.

## Menus measured and spoken

Measured 2026-10-17 on the public save (the "Intro Complete" SRM), with
`scripts/snes-alttp-proof.sh --menus`. Two different screens share module 14, and
only the submodule tells them apart.

### The item menu (START)

```
-- at rest --      mod=7  sub=0  real=7  oms=0  hud_cur=2
-- opened --       mod=14 sub=1  real=7  oms=4  hud_cur=2
-- RIGHT --        mod=14 sub=1  oms=4  hud_cur=11
-- LEFT --         mod=14 sub=1  oms=4  hud_cur=2
```

- The gate is `main_module_index == 14 && submodule_index == 1`
  (`Hud_Module_Run`).
- The cursor is `hud_cur_item` at `0x7E0202`. It moved 2 -> 11 on RIGHT and back
  on LEFT while the inventory bytes did not change, so it is the cursor and not
  a copy of the item.
- `hud_cur_item` indexes the game's inventory order directly: 1 bow, 2
  boomerang, 3 hookshot, 4 bombs, 5 mushroom, 6 fire rod, 7 ice rod, 8 Bombos,
  9 Ether, 10 Quake, 11 lamp, 12 hammer, 13 flute/shovel, 14 bug net, 15 Book of
  Mudora, 16 bottles (original inventory), 17 Cane of Somaria, 18 Cane of Byrna,
  19 Magic Cape, 20 Magic Mirror. The save had bytes `00 01 .. 01` at
  `0x7EF340`/`0x7EF34A`, i.e. boomerang at hud 2 and lamp at hud 11, exactly
  where the cursor stopped.
- The grid is four columns; row and column are `(hud-1)/4 + 1` and
  `(hud-1)%4 + 1`.
- Equipment at `0x7EF359` sword, `0x7EF35A` shield, `0x7EF35B` armour (the save
  read 1/1/0 here, so only sword and shield are spoken for that file).

### The continue/save box (SELECT)

```
-- opened --  mod=14 sub=11 real=7  choice=0  msg=390
-- DOWN --    mod=14 sub=11        choice=1  msg=10
-- UP --      mod=14 sub=11        choice=0  msg=9
-- B --       mod=7  sub=0         choice=0
```

- The gate is `main_module_index == 14 && submodule_index == 11`
  (`Module0E_0B_SaveMenu`).
- The cursor is `choice_in_multiselect_box` at `0x7E1CE8`. Choice 0 is
  "Continue game"; choice 1 is "Save and quit" — the decomp saves and quits only
  when the choice is non-zero.
- Message 390 decodes to "Continue Game, Save and Quit." Moving the choice
  loads messages 9 and 10, which decode to the cursor glyphs alone (`","` and
  `"."`). They are NOT prose and must never be spoken; the adapter marks the
  menu's message index as seen and refuses a decoded line with no letter or
  digit.

### App-path proof

`scripts/snes-alttp-proof.sh --menus` drives the APP path (`poke_load_rom`,
`poke_frame`, `poke_set_button`) on the real ROM and save, and prints what the
adapter says. Measured output:

```
SAY: Item menu. Boomerang, row one, column two. Fighter's Sword, Fighter's Shield.
SAY: Lamp, row three, column three.
SAY: Boomerang, row one, column two.
SAY: Save menu. Continue game, or save and quit. Continue game selected.
SAY: Save and quit selected.
SAY: Continue game selected.
menu-proof=YES
```

## File select measured and spoken

Measured 2026-10-17 with `scripts/snes-alttp-proof.sh --file` (core probe) and
`--files` (app path).

```
-- entering --  mod=1 sub=1  cursor=216   <- fade-in: 216, 77, 155, 85
-- settled --   mod=1 sub=4  cursor=0
-- DOWN --      mod=1 sub=5  cursor=1  arr1=01
-- DOWN --      mod=1 sub=5  cursor=2
-- UP --        mod=1 sub=5  cursor=1
```

- **Gate:** `main_module_index == 1` AND `submodule_index >= 4`. During
  submodules 1-3 the screen is still being built and `0x7E00C8` holds garbage;
  the first version announced "Erase file" over a screen that was still drawing.
- **Cursor:** `selectfile_R16` at `0x7E00C8`. 0-2 are the three files, 3 is
  copy, 4 is erase.
- **Slots are not in work RAM.** The game draws them straight from the
  cartridge's save RAM, so `snes_read` now also serves the LoROM SRAM window at
  `0x700000-0x70FFFF` (`MemoryType::SnesSaveRam`). Per slot, `0x500` bytes:

| Field | Slot offset | Notes |
| --- | --- | --- |
| exists | `0x3E5` | `0x55AA` when the file is used |
| name | `0x3D9` | six 16-bit glyphs, `0xA9` pads |
| hearts | `0x36C` | capacity in eighths; 32 = four hearts |
| sword / shield | `0x359` / `0x35A` | same values as the item menu |

- **Name encoding:** the decomp stores
  `chr = (t & 0xFFF0) * 2 + (t & 0x0F)`. Invert that, then use the game's font
  order: `0x00-0x19` A-Z, `0x1A-0x33` a-z, `0x34-0x3D` 0-9, `0x60` i, `0x61` j.
  The public save's four glyphs `0x0B, 0xC0, 0x47, 0x44` decode to **Link**.
  One unknown glyph drops the whole name rather than guessing a letter.
- **Measured app-path output:**

```
SAY: File select. File one, Link. Four hearts. Fighter's Sword, Fighter's Shield.
SAY: File two, empty.
SAY: File three, empty.
SAY: Copy file.
SAY: Erase file.
file-proof=YES
```

## Area names corrected

The first area table used development-map numbering, not the live
`overworld_area_index`. Kakariko is **0x18**, not 0x21; the Desert of Mystery is
**0x30**, not 0x39; and the dark-world Death Mountain ids were wrong too. The
table now follows the community `ow_names` table from the yba.lua auto-tracker
(`milde.no/public/alttp/yba.lua`), keyed by the same live index, with
abbreviations expanded. `0x2C` Link's House stays the measured value.

## Dungeon names

`cur_palace_index_x2` at `0x7E040C` is a 16-bit value whose low byte is the
dungeon id times two. The decomp's map table names them: 0x00 Sewer Passage,
0x02 Hyrule Castle, 0x04 Eastern Palace, 0x06 Desert Palace, 0x08 Hyrule Castle,
0x0A Swamp Palace, 0x0C Palace of Darkness, 0x0E Misery Mire, 0x10 Skull Woods,
0x12 Ice Palace, 0x14 Tower of Hera, 0x16 Thieves' Town, 0x18 Turtle Rock,
0x1A Ganon's Tower.

- **MEASURED:** `0xFF` at Link's house, the "no dungeon" sentinel the decomp
  tests for. The dungeon values themselves are sourced from that table and have
  not yet been observed by walking into a dungeon.
- ⛔ **0 is the sewers, a real dungeon**, not "no dungeon". Only `0xFF` means
  none.
- `WhereAmI` now says "Inside Eastern Palace, room 12." when the id is known,
  and keeps "Indoors, room 12." when it is not. Room *notes* (landmarks inside a
  room) are still not curated.

## Area entry announcements

Outdoors, in module 9, a change of `overworld_area_index` speaks
"Outside, <name>." at Normal in group `area`. Indoors clears the remembered
area, so stepping out of a house or dungeon announces where the player has
arrived, and the first outdoor area after a load speaks too. Standing still says
nothing.

App-path proof (`scripts/snes-alttp-proof.sh --walk`), measured:

```
-- walking out --
SAY: Outside, Link's House.
walk-proof=YES
```

## The bottle submenu

The item menu's bottle slot (hud 16 in the original inventory) opens a second
menu. `hud_cur_item` stays 16 while `link_item_bottle_index` (`0x7EF34F`, 1-4)
moves, so watching only the item cursor leaves this menu silent.

- **Gate:** item menu (module 14 sub 1), `hud_cur_item == 16`,
  `overworld_map_state >= 7`.
- **Contents:** `link_bottle_info` at `0x7EF35C`, one byte per bottle.
- **Names:** the game's own, decoded from `kHudBottlesItemText` in `hud.c` with
  the same `L(x) = 0x2550 + x - 'A'` the game uses: 1 mushroom, 2 bottle (the
  empty bottle), 3 life, 4 medicine, 5 magic, 6 cure-all, 7 fairy, 8 good bee.
- **Spoken:** "Bottle menu. Bottle one, life." on opening, "Bottle two, magic."
  on a change.
- ⛔ **NOT MEASURED LIVE.** The public save has no bottles, so this is sourced
  from the decompilation and covered by unit tests only. A save with a bottle in
  it would let the live proof cover it.

## Guidance

- **Command:** `Command::Guide` = 41 (append-only C ABI). Swift's
  `AdapterCommand.guide` carries the same number.
- **Coordinates:** `link_x_coord` `0x7E0022`, `link_y_coord` `0x7E0020`, 16-bit.
- **Screen grid:** the game's own conversion, `overworld.c`:
  `packed = ((y & 0x1E00) >> 5) | ((x & 0x1E00) >> 8)`, and the area id is
  `packed >> 1`. That makes the area's low six bits `row * 8 + column` and each
  screen 0x200 world units wide and tall.
- **Measured anchor:** Link's House is area `0x2C` = row 5, column 4; the public
  save's Link stands at x=2232, y=2792 inside that cell.
- **Direction:** eight-way from the sign of dx and dy (screen y grows downward,
  so positive dy is south), with a 3:1 ratio so small offsets stay cardinal.
- **Distance:** octile, `max(ax, ay) + min(ax, ay) / 2`, then rounded to screens
  by `(span + 256) / 512`.
- **Arrival:** the current area id equals the target's, so the four cells of a
  large area all say "You are here".
- ⛔ **Live-path trap found here:** `poke_command` had its upper bound frozen at
  `RepeatOlder` (39), so Status (40) and Guide (41) returned false with no speech
  while every unit test passed. Any command appended after this must widen that
  bound and be checked by a live proof, not only by a unit test.

## The objective

- **Command:** `Command::Objective` = 42.
- **Story bytes** (zelda3 `variables.h`):
  - `sram_progress_flags` `0x7EF3C6`: `0x01` uncle gave the sword, `0x04`
    Zelda reached the Sanctuary, `0x10` the uncle left the house.
  - `sram_progress_indicator` `0x7EF3C5`.
  - `link_which_pendants` `0x7EF374`, all three when `& 7 == 7`.
- **Measured on the public save** (booted through the file select, standing
  outdoors): `prog=2 flags=15 pend=0 sword=1`, area `0x2C`.
- **Stages:** no sword flag -> find the uncle in Hyrule Castle (`0x1B`); sword
  but no Sanctuary flag -> rescue Zelda (`0x1B`); pendants incomplete -> collect
  them, starting with Sahasrahla in Kakariko (`0x18`); pendants complete and no
  Master Sword -> Lost Woods (`0x00`); Master Sword -> Agahnim (`0x1B`).
- **Refusals:** indoors it names the goal without a bearing (room coordinates are
  not world coordinates); in the dark world it refuses, because none of those
  stages have been measured.



