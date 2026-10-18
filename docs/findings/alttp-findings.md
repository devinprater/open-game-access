# A Link to the Past in Open Game Access — findings so far

Paused 2026-10-17. This is the state of the work, what is proven, what is only
believed, and what is still open. Every address below was measured or taken from
the decompilation at `~/z3work/zelda3`; none is a guess.

## 1. Where the work stands

The SNES console runs inside OGA through MesenCE, and the A Link to the Past
adapter is registered, tested, and linked into the iOS app. The reader can say
what screen you are on, indoors or outdoors, read health, magic, rupees, keys,
and speak dialogue from the game's own text buffer.

It cannot yet read the item, save, or map menus, and it cannot guide a route.

The Android app is not a path for this work yet: its native code is melonDS only,
with no Mesen and no OGA core, and no device was attached to measure.

## 2. The console layer (SNES in OGA)

`Core/snes_core.h` / `Core/snes_core.cpp` wrap MesenCE's `SnesConsole` behind the
OGA C ABI: create, load, start, stop, frame, framebuffer, work-RAM read, input,
savestate, battery save, destroy.

Things that were wrong at first and are fixed, each proven by a specific check:

- Framebuffer was empty (`fb=0x0`, `nonzero=0`). `GetVideoFilter()` returns a new
  object per call, so a cached filter is now configured with
  `SetBaseFrameInfo()` and `SetOverscan()` before `SendFrame()`. Output is
  `0xAARRGGBB`, matching `SnesDefaultVideoFilter.cpp`.
- Identity was a file-size guess. Now the CRC32 is computed from
  `MemoryType::SnesPrgRom`, the image Mesen actually runs, so a headered `.smc`
  and a headerless `.sfc` both report `crc:777aac2f`.
- No controller existed: Mesen starts with `Port1.Type == None`, so every press
  was dropped (`provider-calls=15 last-type=0`). Setting
  `cfg.Port1.Type = ControllerType::SnesController` before load fixed it
  (`provider-calls=34 last-type=1`, `start-bits=00000080`).
- Teardown leaked one console (`live-after-second-load=2`, `live-after-destroy=1`);
  `snes_destroy` had landed in the wrong function. Now 1 and 0.
- Reading after stop returned true from a dead console, and reset after stop
  segfaulted. Both paths now refuse cleanly.

Proof harness: `Core/snes_probe.cpp` with `scripts/snes-alttp-proof.sh`
(`--identity`, `--run`, `--input`, `--reads`, `--resolve`, `--states`,
`--after-stop`, `--teardown`, `--headered`, `--memory`, `--resume`, `--drive`,
`--measure`, `--text`, `--keytest`).

## 3. Measured memory map

| What | Address | Notes |
| --- | --- | --- |
| main module | `0x7E0010` | the dispatch index |
| submodule | `0x7E0011` | |
| saved module for menu | `0x7E010C` | the real module when 14 is up |
| indoors flag | `0x7E001B` | 1 = indoors |
| Link x / y | `0x7E0022` / `0x7E0020` | 16-bit |
| overworld screen | `0x7E008A` | 16-bit |
| room index | `0x7E00A0` | |
| overworld area | `0x7E040A` | game computes `screen & 0x3F \| darkworld` |
| heart capacity | `0x7EF36C` | eighths of a heart |
| current health | `0x7EF36D` | eighths |
| magic | `0x7EF36E` | 0 when no meter yet |
| rupees | `0x7EF362` | 16-bit |
| message index | `0x7E1CF0` | |
| message text buffer | `0x7F1200` | already decoded by the game |

Boot garbage at frame 0 reads capacity `0x7F` and health `0x56`, which a naive
range check accepts. The adapter requires capacity in `0x18..0xA0` and
`(capacity - 0x18) % 8 == 0`, one heart container being eight eighths, and
current health no greater than capacity.

## 4. Module semantics (from the decompilation)

`kMainRouting` in `src/misc.c` names the modules. The important correction:
**module 14 is the interface/HUD layer, not a cutscene.** The real screen is in
`saved_module_for_menu`.

- 0 title, 1 file select, 4 name entry, 5 load file, 7 dungeon/gameplay,
  8–11 overworld load and overworld, 14 interface, 16 spotlight open,
  18 game over, 20 attract, 23 save and quit.
- Interface submodules: 1 HUD, 2 text, 3 dungeon map, 11 save menu.

Consequence: "where am I" must resolve module 14 through `saved_module_for_menu`.
Reading module 14 literally is how a HUD over indoor gameplay was mistaken for a
cutscene.

## 5. Dialogue comes from the game's own buffer

The game decodes its messages before display. `messaging_text_buffer` at
`0x7F1200` holds font tile indices for the current message, so the reader needs no
menu driving and no decompression.

Measured message 388:

```
7a 00 44 12 2d 1a 2b 2d 59 05 2b 28 26 59 0b 22 27 24 51 2c 59
07 28 2e 2c 1e 75 59 59 12 2d 1a 2b 2d 59 05 2b 28 26 59 12 1a
27 1c 2d 2e 1a 2b 32 72 7f
```

Decodes to: **"Start From Link's House, Start From Sanctuary."**

The mapping, all measured: A–Z `0x00`–`0x19`, a–z `0x1A`–`0x33`, digits
`0x34`–`0x3D`, space `0x59`, apostrophe `0x51`, comma `0x75`, period `0x72`,
end `0x7F`. Commands start at `0x67` (`kTextCommandStart_US`) and their parameter
counts come from the decomp's `kText_CommandLengths_US`; skipping only the
command byte would read its parameter as text.

The adapter speaks a message once per new index, not once per frame.

## 6. Getting outside

The downloaded save looked frozen. It was not: the game was sitting in the
interface module. **One START press closes it** — measured `module 14 sub 1` →
`7 sub 0`. Direction input does nothing until that happens.

With the interface closed, walking out gave the first outdoor reading:

```
module 16 sub 1  real=9  indoors=0  x=2232 y=2792  area=44 screen=44
```

Module 16 is the transition, then module 9 is the overworld, and Left moved the
coordinates. So area 44 is a real observation, and the save's own description
text says where it is.

## 7. Area names

The game never shows an area name and the decompilation has no name table, so
`Core/alttp_names.cpp` is curated and deliberately small. `0x2C` Link's House is
measured; Sanctuary, Graveyard, Hyrule Castle, Kakariko, the Desert squares,
Skull Woods, Death Mountain and Turtle Rock come from the development map data.
Everything else speaks its number. A wrong name sends a blind player the wrong
way; a number does not.

## 8. What is not done

- **Menus.** Item, save, and map screens are not read. The module and submodule
  values are known (`14` with submodule `11` for save), but no reader exists.
- **Route guidance.** No navigation, no beacon, no directions.
- **Names.** Most areas are unnamed; the dark world is sparse.
- **Dialogue coverage.** Only one real message has been decoded end to end.
  Commands with larger parameter blocks are untested.
- **Android.** No Mesen backend in `app/src/main/cpp`, no device attached. The
  iOS build is the only phone path this work delivers today.
- **The no-ROM gate** trips on untracked vendor objects and probe binaries in a
  working tree, so a commit gate run from this tree will fail even though the
  branch contains no game data.

## 9. Reproduce

```
wsl -d Ubuntu-24.04
cd ~/oga-work
bash scripts/alttp-adapter-test.sh        # 24 tests
bash scripts/snes-alttp-proof.sh --identity
ALTP_SAVE=$HOME/z3work/alttp-save bash scripts/snes-alttp-proof.sh --measure
bash scripts/build-core.sh                # device archive with the adapter
bash scripts/xtool-build.sh               # iOS app
```

The ROM is Devin's USA dump, payload CRC32 `777AAC2F`, and it never enters the
repository.
