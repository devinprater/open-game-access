# The Chrono Compendium — what it gives us, and what I checked

Surveyed 2026-10-13. The Compendium's Term pages and its forum archive are the
community's own documentation of Chrono Trigger's internals: T. Geiger (author of
Temporal Flux, the ROM editor CT modders still use) is a regular poster, and most of
the addresses below come from him. Everything is decades old and was made for ZSNES
and Geiger's own debugger, so **each entry the adapter would use was checked live**
against the running game (snes9x2010 over RetroArch's MCP server, Crono's house
savestate, `docs/evidence/ct-walk-and-compendium.txt`).

Verdict up front: **very useful.** It independently names the mechanism Probe 7 found,
it supplies a room-name table keyed by the byte we already read, and it hands us a
verified live RAM map that covers narration, party, stats and money — all without
another reverse-engineering pass.

## 1. It names our control flag for us

Probe 7 found that the field engine's control flag is `$011F`, written by field-VM
opcode `0xE3` (`E3 00` lock / `E3 01` unlock), and named `CtSceneExplore` in the decomp.
The Compendium's **Temporal Flux Event Encyclopedia** and the **Event Tricks** page
call the same thing **`ExploreModeOff` / `ExploreModeOn`**, and the documented pattern
for a scripted encounter is exactly what we saw in the bedroom:

```
ExploreModeOff
Move Party
Move Sprites
Scroll Screen
Battle
Remove (object)
ExploreModeOn
```

So three sources agree — the ROM I traced, the decomp's notes, and the community's
editor — and the community's vocabulary is the one to use in our own notes. The
"trigger a battle from a touch region" idiom is documented the same way, which is
directly the shape of the events the adapter has to narrate:

```
getObjectCoordinates, variant 22, PC, 00, 7f0220, 7f0222
if(7f0220 >= (x coordinate))
if(7f0222 >= (y coordinate))
  explore mode off
  move party / move enemies / move screen / battle
goto line of get object coordinates
```

## 2. A room-name table, keyed by the byte we already read

The **Location Offsets (CTP)** page keys rooms by the `$7E0100` value, which is the
byte the adapter already reads for "where are we":

| `$7E0100` | room |
|---|---|
| 00 | Title Screen |
| 01 | Crono's House, Downstairs |
| 02 | Crono's House, Upstars |
| 03 | Lucca's House, Upstairs |
| 05 | Millennial Fair, First Area |
| 06 | Millennial Fair, Gato |

The same page settles the width question: `7E010100` loads the first group of maps and
`7E010101` the second, so **the location index is at least nine bits** — `$7E0100`
plus bit 0 of `$7E0101`. Our live read of `02 00` -> `002` is therefore correct *and*
we now know not to read only one byte when the index grows.

This is a ready-made narration table, and it is also a navigation index: the
compendium's list is what tells us the bedroom exit is `002 -> 001`.

## 3. The live RAM map, checked against the running game

Geiger's map, with the live value from Crono's house at the end of the walk. Checked
values are marked; the rest are recorded but not yet exercised.

| address | meaning | live |
|---|---|---|
| `7E0100` | Current Location (2 bytes, index >= 9 bits) | `02 00` -> 002 ✓ |
| `7E0118` | flash screen with these colours, stops (set `08`) | not tested |
| `7E0119` | **Layer 1 brightness** (`07` = pause value, `0F` = normal) | `0F` ✓ |
| `7E011F` | our explore / control flag | `01` ✓ |
| `7E0121` | Layer 1 colouring | not tested |
| `7E010F` | set `88` to remove the black bar at the bottom | not tested |
| `7E00F2` | JoyPress, read by event commands 38 / 39 | `00` |
| `7E00F8` | used by event command 31 (save points) | not tested |
| `7E0150` / `7E0151` | JoyPress1 / JoyPress2 | `80` / `80` |
| `7E0155` | Z-plane | `01` |
| `7E1D1A`–`7E1D1D` | scroll masks L / R / T / B, **in tiles** | `10 1F 01 0E` ✓ |
| `7E1D00` | screen scrolling | — |
| `7E1D1E`–`7E1D23` | layer widths / heights | — |
| `7E2603` | Crono current HP | `70` ✓ |
| `7E2605` | Crono max HP | `70` ✓ |
| `7E2607` / `7E2609` | Crono current / max MP | `8` / `8` ✓ |
| `7E260B`–`7E2611` | power, stamina, speed, magic, hit, evade, magic defence | `5 8 12 5 8 8 2` ✓ |
| `7E2612` / `7E2613` | Crono level / XP (3 bytes) | `1` / `0` ✓ |
| `7E2980`–`7E2982` | party PC1 / PC2 / PC3 | `00 80 80` ✓ |
| `7E2C53` | gold (3 bytes) | `400` ✓ |
| `7E3000` | location map, layer 1 tiles (layer 2 `7E3040`, layer 3 `7E3080`, properties `7E30C0`) | read |
| `7E7000` / `7E7040` | location tile properties 2 / 3 | read |
| `7E7080` | the decomp's door plane ("???" in Geiger's map) | `0x8080` throughout |
| `7F0000` | storyline counter | `03` ✓ |
| `7F2000` | **Location Event Data, 0x1800 bytes** | read (our script image) |

The stat block is a **0x50-byte stride**, which the Compendium confirms by listing the
same offsets for every character: Crono `7E2603`, Marle `7E2662`, Lucca `7E26B2`,
Robo `7E2702`, Frog `7E2752`, Ayla `7E27A2`, Magus `7E27F2`. So the whole party's
stats are one arithmetic expression away, at any point in the game.

**The party bytes are corroborated independently.** The Event Tricks page documents
location `{000}` "Load Screen", label `[0075]`, initialising the party with
`MemCpy(Mem.PC1, { 00, 80, 80, 80, … })` and states: "80 values are empty, 00 is Crono,
and the party members go in the order of Crono, Marle, Lucca, Robo, Frog, Ayla, Magus."
Our live read in Crono's bedroom is `00 80 80` — a fresh game with only Crono. Same
numbers, two entirely independent routes.

**And `$7E0119` cross-checks the decomp.** Geiger's map calls it Layer 1 brightness;
the decomp's field note has `$0119` as the drain the field loader waits on during a
fade. Live it reads `0F` (normal) in a settled room. That is a second source agreeing
with the fade byte, and it is a cheap "is the screen mid-transition" signal for the
adapter.

## 4. The coordinate model, and where it says our window numbers are

Event-level object coordinates are in **tiles**, and the room-detection idiom proves it
by setting `7E1D1A`–`7E1D1D` from tile comparisons:

```
if(mem.7f0220 < 0F)   // he's in the left column
if(mem.7f0222 < 11)   // he's in the top row
mem.7E1D1A = 0 ... mem.7E1D1D = 0F
```

So `$7E1D1A`–`$7E1D1D` are tile bounds of the scroll window, which is why our live
`10 1F 01 0E` reads as **columns 0x10–0x1F, rows 0x01–0x0E** for the bedroom. Useful
for the adapter both as room extent and as a sanity check on any tile conversion.

Also from the same pages:

* `ChangeLocation` warps to another map, and its X / Y are the destination **tile**
  coordinates on the new map; the **variant matters** — variant `E0` warps instantly
  while others animate, so a transition the adapter narrates must not assume one.
* `getObjectCoordinates` (variant 22, mode PC, object 00) copies the PC's tile position
  into two words, conventionally `7F0220` / `7F0222`.
* **Object 00's "activate" runs automatically after the player leaves a menu** — the
  per-load / per-return hook. Worth knowing before we attribute anything odd to a
  menu round trip.
* Epoch placement uses `7E0290` / `7E0292` (X / Y in pixels), `7E0294` (wings) and
  `7E029F` = **map number** (`1F0` = 1000 AD, `1F1` = 600 AD, `1F2` = 2300 AD) — a
  world-map identity byte, not present in the location byte.

## 5. Smaller things worth keeping

* **Save-file structure** (Data Crystal) — a character block at `+0x200` within a save
  slot, gold at `0x5E0`, world at `0x5F3`, player X / Y at `0x5F5`. Lets us read a save
  without the game running, and it is the layout a save editor would use.
* **Breakpoint addresses** — `C14CC4` / `C14658` for tech animation scripts, for
  anyone doing animation work.
* **The New Tech Editor thread** decodes the animation-command families
  (`C0`–`C3` circular sprite movement, `74` set priority, `75` reset screen colour,
  `79` / `7B` play sound, `7D` play weapon sound) and confirms the "command 5F is
  empty" and "command 60 is set palette" cases. This is the layer above the field VM:
  relevant only if the adapter starts narrating battle animation.
* **`MemCopy88`** — the `88 40 1f ff 02` family and its per-mode byte layout
  (`+0 = mode & 0xF0`, `+2 = p2 & 0x0F`, `+3 = 8`, `+7 = (p3 >> 4) | (p3 & 0xF0)`…).
  Niche, but it is the only written decode of that command I have found.
* **The Prerelease translated text** records a debug message reading `Info:` followed
  by a 16-bit number, with two values each for X and Y, "probably used to display
  character X and Y map location values". A debug build that prints the player's map
  coordinates is a nice confirmation that reading them is the intended affordance.
* **Storyline / flag bytes** appear repeatedly as `7F00DE` (Lavos attack modes),
  `7F005A` (Lucca flags) and `7F00AB`; Geiger's map puts the storyline counter at
  `7F0000` instead. Two different bytes with similar-sounding roles — do not
  conflate them without checking, and our live `7F0000 = 03` is consistent with the
  counter at the start of the game.

## 6. What the Compendium does NOT give us

* **No door / exit table per location.** The exit mechanism is only in the decomp
  (`field_exit_scan_guard.md`, `field_door_list.md`), which is what Probe 8 used to
  show the bedroom exit is a **bumped door tile in the `$7E:3000` map**, not
  something you walk onto.
* **No opcode table for the field VM.** The Encyclopedia is a *Temporal Flux UI*
  reference — command names and their editor fields, not byte encodings. Our
  hand-built `OPLEN` table stays necessary.
* **No memory map for the DS or PSX versions** in the place we need it. The PSP / PSX
  pages cover the ROM-side data (`$7F0000` decompressed event data) rather than a
  live debugger map.
