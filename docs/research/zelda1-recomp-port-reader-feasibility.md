# Could our Zelda 1 reader live in the recompiled NES port? — measured

Devin asked for this after the port survey. **Everything below was run today on this machine**,
against `mstan/LegendOfZeldaNESRecomp` v1.9.1 and Devin's own cartridge dump. Nothing was
downloaded except the port's own release; no game data was obtained from anywhere.

## 1. What was actually run

- **Port**: `LegendOfZeldaNESRecomp-windows-x64.zip`, v1.9.1, 3,968,082 bytes. Checked against
  the release's own `SHA256SUMS.txt`: `6370a3fd58095eb0501bc732ec443331b4a4c4716b535a8c19ea9490a1700672`. Match.
- **ROM**: `Dropbox/games/NES/Legend of Zelda, The (USA).nes`, his own copy. Payload CRC32
  (ROM data, iNES header excluded) = `0x3FE272FB` — **exactly the revision the port requires**
  ("Select your stock USA PRG0 ROM, payload CRC32 3fe272fb").
- ⛔ **The port refused his file as dumped.** It reported `Cartridge metadata differs from the
  compiled program`, because the dump's iNES header carries `0x08` in byte 7, which the loader
  reads as a cartridge field the compiled program does not have. **Zeroing that one header byte
  in a local copy** made it boot; the payload was untouched, and no other candidate header
  (fully zeroed bytes 8-15, classic iNES, NES-2.0) was accepted. This is worth knowing for any
  OGA build: the port identifies cartridges by a metadata hash, not just the payload CRC.
- **Runs** (headless, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `NESRECOMP_NO_LAUNCHER=1`):

  | frames | input | result |
  |---|---|---|
  | 90 | none | boots; `mode=native cycles=2677864 native_cycles=2677857 (100.0%)` |
  | 2,400 | 11 scheduled presses | `frames=2400 native (99.8%)`, frame log written: 2,400 records |
  | 1,500 | START/A schedule | native 99.5%; picture written |
  | 1,600 | START/DOWN/A schedule | native 99.9%; picture written |

- **It renders.** The 256×240 PNG frames written by the run differ from one another
  (`present_00000/00300/00600/00900/01200.png` are five distinct images), so this is a live
  picture, not a frozen frame.

## 2. The game's own state is readable from outside, at the reader's own addresses

The `--frame-log` file is documented in `runner/cyc/cyc_host.c` (`write_frame_log`): `CYCFRAME`
plus a version, then **per frame** the frame number, cycle count, six blob lengths, palette(32),
OAM(256), **CPU RAM (2 KB)**, CIRAM, cartridge RAM, CHR RAM, the 256×240 picture and the FDS
state. A 2,400-frame run produced 344 MB of exactly that.

Reading the reader's addresses out of it, with scripted presses:

| frame | what happened | GameMode `0x12` | Quest `0x11` | ObjX `0x70` | ObjY `0x84` |
|---|---|---|---|---|---|
| 0-5 | power-on RAM pattern | 240 | 15 | 15 | 240 |
| 10 | game has cleared RAM | 0 | 1 | 0 | 0 |
| 310 | after START (title) | **1** | 1 | 0 | 0 |
| 401 / 701 | after further presses | **14** | 1 | 112 → **160** | 47 |

Mode `0x01` is the file-selection screen and `0x0E` (14) is Registration — the reader's own
table (`FILE_SCREEN_MODES`) says exactly that. The object slot moving `112 → 136 → 152 → 160`
under DOWN presses is the name-entry cursor, read out of the port's RAM. **Our scripted input
moved the game and we observed the movement through the reader's addresses.**

## 3. The port names every address the reader uses

`symbols.sym` (2,156 entries) against the reader's `ADDR` table. Every address matched, and the
names agree with the reader's meanings:

- `0x0010 CurLevel` ↔ `cur_level`; `0x0012 GameMode` ↔ `game_mode`; `0x0013 GameSubmode`;
  `0x0015 FrameCounter`; `0x0011 IsUpdatingMode` (the reader calls it `quest`);
- `0x00EB RoomId` ↔ `room_id`; `0x0064 LadderSlot` ↔ `ladder_slot`; `0x0515 ActiveBoulders` ↔ `active_boulders`;
- `0x0656 SelectedItemSlot`; `0x0657 Items`; `0x0658 InvBombs`; `0x065A Bow`; `0x065B InvCandle`;
  `0x065D InvFood`; `0x065E Potion`; `0x0665 InvBracelet`; `0x0666 InvLetter`; `0x0667 InvCompass`;
  `0x0668 InvMap`; `0x066D InvRupees`; `0x066E InvKeys`; `0x066F HeartValues`; `0x0670 HeartPartial`;
  `0x0671 InvTriforce`; `0x0674 InvBoomerang`; `0x0675 InvMagicBoomerang`; `0x067F WorldFlags`
  (the reader reads `0x067F + room_id` for a screen flag);
- `0x0070 ObjX`, `0x0084 ObjY`, `0x0098 ObjRoomItemId`, `0x00C0 ObjShoveDir`, `0x00D3 ObjShoveDistance`, `0x03F8 ObjInputDir`.

**Two divergences worth keeping, both cases of one address doing several jobs:** the reader
calls `0x034F` `obj_type_base`, while the symbol file calls it `RoomKillCount`; and the reader's
`0x0415` "selector" is `Gleeok_ObjHeadSpeedX` in the symbol file. These are per-mode scratch
uses, so a hook must know which mode it is in before trusting the name.

## 4. The port already has a reader-shaped channel

`extras.c` implements `game_handle_debug_cmd()` for this game with:

- **`zelda_state`** → `game_mode` `0x12`, `room` `0xEB`, `link_x` `0x70`, `link_y` `0x84`,
  `hp` `0x66F`, `sword` `0x657`, `sub_mode` `0x13` — the same addresses, named, in one call;
- **`entity_snapshot`** → all 12 entity slots (type `0x34F+i`, x `0x70+i`, y `0x84+i`, dir
  `0x98+i`, state `0xAC+i`, metastate `0x405+i`, timer, uninit) — the object tables the reader walks.

`TCP.md` states the rule for this framework: *"If a piece of state cannot be observed over TCP,
add a command in `runner/src/debug_server.c` — do not work around it."* The game-specific hook
is the sanctioned extension point, and the port's author already used it for exactly our kind of
state. What the reader needs against what exists:

| the reader needs | the port provides |
|---|---|
| `mainmemory.read_u8/u16/read_bytes` | per-frame CPU RAM in the frame log; `read_ram` over TCP |
| `emu.framecount` | the frame number in every record |
| `joypad.get` / scripted input | `--input` schedule file; TCP `key` / `pad` / `action` |
| `savestate.save/load` | `--save-state F:FILE` / `--load-state FILE`; TCP `save_state`/`load_state` |
| `console.log`, `gui.text` | our own host, unchanged |
| the reader's speech file | our own host, unchanged |

⛔ **Live TCP reads were NOT established here.** Passing `--tcp 4370` on a headless run left no
listener in two attempts; the cycle server is registered by the SDL/window path
(`tcp_setup()` in `runner/cyc/cyc_sdl.c`), which `--headless` does not enter. The measured
observation path is the frame log. A windowed or `--hidden` run with `debug.ini` is the
untested live path, and the non-cycle backend's richer server is not what this release ships.

## 5. What this means for the integration

**The reader goes in host-side; the port does not change.**

- Keep the reader byte-identical — our standing rule for hosted Lua reader sets.
- The NES Lua host we already have (`Core/mesen_core.cpp`) binds `mainmemory.*`, `memory.read_u*`,
  `memory.usememorydomain`, `emu.framecount`, `console.log`, `gui.text`, `joypad.*` and
  `oga.set_speech_file`. It needs **one new memory source**: the port's RAM instead of Mesen's.
- Two feeds are possible from what was measured: the per-frame log (offline, deterministic) or
  `read_ram` over the port's TCP once the live path is established.
- The alternative — porting ~4,000 lines of Lua to C, or building a Lua host into the port's C
  runner — is more work and breaks the byte-identical rule. Not needed for a first proof.

## 6. Honest limits

- **Windows desktop only.** The port ships Windows x64 (other titles in the family ship a Linux
  AppImage); nothing here touches iOS or Android, so OGA's phone app gains nothing from this today.
- **One ROM revision.** A different dump (Rev 1, Europe, Virtual Console) is refused outright.
- The runs reached **name registration (`mode 0x0E`), not the overworld** (`0x05`/`0x0B`), so no
  gameplay address has been exercised live yet. The port writes save states, which is the cheap
  way to start a reader test already in the overworld.
- Nothing here tests the **audio** path, and the picture was only checked for change, not for
  correctness against the console.

## 7. Next step, if this is pursued

Add the memory source to the NES Lua host, drive the port to a save state inside the overworld,
and run the unmodified reader against it; then compare its speech with the same reader under
Mesen on the same ROM. That comparison is the real acceptance test, and it is now cheap.
