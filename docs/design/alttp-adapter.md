# A Link to the Past on Open Game Access — design

Status: design, approved in outline 2026-10-17. Implementation not started.
Scope: a Super Nintendo console for OGA, and a native reader for
The Legend of Zelda: A Link to the Past (USA, CRC32 `777AAC2F`).

## 1. Purpose

A blind player should be able to load A Link to the Past in OGA, play it with
speech, and get useful answers: where they are, what state Link is in, what the
menus say, and what the game says.

Success is not "the adapter compiles". Success is Devin playing the opening
sequence — house, uncle, castle, first dungeon — with speech that is accurate
and quiet enough to hear the game.

The SNES console is a second deliverable in its own right: once it exists, every
SNES title in the library is loadable, and the next SNES reader (Chrono Trigger)
needs only an adapter.

## 2. What was measured before this design

Every fact below was read or run on this machine, not recalled.

| Fact | Where it came from |
|---|---|
| OGA has no SNES backend; the registry lists SNES as `OGA_BACKEND_PLANNED` with Mesen named as the chosen core | `Core/systems.cpp` |
| Mesen's SNES core compiles for `aarch64-linux-android26` | `scripts/mesen-feasibility.sh` (admission test, 2026-10-04) |
| The SNES translation-unit list is already audited in the shared source list (`MESEN_SNES`) | `scripts/core-sources.sh` line 638 |
| The host build compiles all of Mesen's cores, SNES included (`MESEN_CORE_ALL`) | `scripts/build-host.sh` |
| Mesen exposes `MemoryType::SnesWorkRam`, `SnesPrgRom` and `SnesSaveRam` | `Core/Shared/MemoryType.h` |
| `SnesConsole` offers `GetCpu`, `GetPpu`, `GetSpc`, `GetMemoryManager`, `GetControlManager`, `RunAudio`, `SaveBattery`; it supports `.sfc`, `.smc`, `.fig`, `.swc` | `Core/SNES/SnesConsole.h` |
| The USA dump is 1 MiB, CRC32 `777AAC2F`, SHA-256 `66871d66…09cfb` | computed with zlib on Devin's file |
| A Link to the Past has no usable four-letter header code; `0x7FB0` reads `01 8D 24 01` | computed on the same file |
| zelda3 has 1,417 named RAM values for this exact game; its `g_ram` is 128 KiB, so offsets map to `0x7E0000 + offset` | `~/z3work/zelda3/src/variables.h`, `src/zelda_rtl.c:15` |
| zelda3 builds and runs from the same dump | `~/z3work/zelda3/zelda3`, built 2026-10-17 |
| The NES console already proves the host pattern: Mesen `Emulator::Run()` on its own thread, reads on the caller's thread | `Core/mesen_core.cpp` |

The zelda3 decomp is a **map**, not a dependency. No zelda3 code and no game
data ship with OGA. Every address taken from its names is re-read against live
memory before the adapter uses it.

## 3. Architecture

Two parts, joined by the existing adapter seam.

```
ROM file ──► oga_resolve_backend (extension)
                │
                ├─ snes_hint ──► LoadSnesRom ──► OgaCore{ snes ops } ──► Mesen SnesConsole
                │                                      │
                │                                      ├─ framebuffer / input / savestates
                │                                      └─ read(state, addr, width)  ──► work RAM
                │
                └─ core->gameCode = "crc:777aac2f"
                                      │
                                      ▼
                        oga::find_by_game_code ──► kZeldaLinkToThePast
                                      │
                                      ▼
                        per-frame on_frame + player commands (speech)
```

### 3.1 Part 1 — the SNES console

New files `Core/snes_core.h` and `Core/snes_core.cpp`, modelled line for line
on `Core/mesen_core.h` / `Core/mesen_core.cpp`. The public ABI mirrors the NES
core so `pokecore.cpp` and the front ends need no new concepts:

- create / destroy, speech and log callbacks
- `snes_load_rom(core, path, save_path, code_out[16])`
- `snes_start`, `snes_stop`, `snes_running`, `snes_frame`
- `snes_framebuffer`, `snes_framebuffer_ptr` (RGBA8888, dimensions from the PPU frame)
- `snes_set_button(core, index, down)`
- `snes_save_state`, `snes_load_state`, `snes_frames_completed`, `snes_last_error`
- `snes_read(core, addr, width, out)` — adapters only
- diagnostics: `snes_cpu_pc`, `snes_console_frame_count`, `snes_fps`

Decisions and their reasons:

1. **Frame loop on its own thread.** Mesen's `Run()` owns its frame limiter and
   its own loop; the NES core already lives with this. No synchronous
   single-frame stepping is invented.
2. **Memory reads through `MemoryType::SnesWorkRam`.** That is a plain 128 KiB
   buffer, bounded by `ConsoleMemoryInfo.Size`, with no MMIO side effects.
   Addresses outside `0x7E0000–0x7FFFFF` are refused, not mapped through the
   CPU bus, because a read of `$2100` would have side effects. Adapters get
   `false` for those, which they already treat as "no answer".
3. **No audio in the first pass.** `nes_read_audio` returns 0 today. The SNES
   core does the same until Mesen's `IAudioDevice` path is wired and measured.
   P0 must prove the console runs frames with no audio device registered. Sound
   is a later phase, and the reader never depends on it.
4. **Identification is content, not filename.** `snes_load_rom` writes
   `crc:<8 lowercase hex>` from `Emulator::GetCrc32()` into `code_out`. A file
   rename or a translation patch cannot attach the wrong reader. The string is
   an identifier for the registry, never spoken.
5. **Battery save on stop.** `SnesConsole::SaveBattery()` runs in `snes_stop`,
   with Mesen's home folder pointed at the save directory, as the NES core does.
6. **Input provider.** A `SnesInputProvider` translates the app's twelve
   `POKE_BTN_*` indices to Mesen's SNES controller, registered before the loop
   starts, mirroring `NesInputProvider`.

Wiring changes:

- `Core/pokecore.h` / `Core/pokecore.cpp`: a `SnesCore* snes` member, a
  `LoadSnesRom` path, teardown, and the load dispatch.
- `Core/oga_core.h` / `Core/oga_core.cpp`: `snes_hint` in
  `OgaResolvedBackend`, extension cases for `.sfc`, `.smc`, `.fig`, `.swc`, and
  a `SnesOps` table.
- `Core/systems.cpp`: the SNES row moves `PLANNED` → `READY` in the same commit
  that boots a ROM, per the registry's own rule.
- `scripts/core-sources.sh`: `snes_core.cpp` joins `OGA_GLUE`.
- `Sources/OpenGameAccess/InputBridge.swift`: `.sfc`, `.smc`, `.fig`, `.swc`
  join the picker filter. No new UI concept.
- The Android front end gets the same extension list when the phone build is
  attempted, not before.

### 3.2 Registry identity

The existing registry already compares full strings, and the SNES core's
`crc:…` id is just a string. So **the registry needs no new matching code and
the `Adapter` struct gains no member**: the ALttP adapter sets
`game_code = "crc:777aac2f"`. Only the `Command` enum grows, append-only, for
`Status`. That keeps the C ABI stable and works for every future SNES reader.

### 3.3 Part 2 — the reader

New `Core/alttp_adapter.cpp`, registered in `Core/adapters.cpp` as
`kZeldaLinkToThePast`, display name "The Legend of Zelda: A Link to the Past".

`attach` refuses unless the content check passes:

- the id is the measured CRC,
- the console is running and has produced frames,
- `link_x_coord` and `link_y_coord` are inside the coordinate range the game
  uses, and the frame counter is advancing.

If any check fails, `ready()` is false and the UI shows no reader controls.
Silence is the correct failure, per the project rule.

The memory map starts from these zelda3 names. Each row is confirmed live
before it is used in speech, and the confirmation is recorded in
`docs/design/alttp-memory.md` with the value observed.

| Purpose | zelda3 name | Address | Width |
|---|---|---|---|
| liveness | `frame_counter` | `0x7E001A` | 8 |
| Link Y | `link_y_coord` | `0x7E0020` | 16 |
| Link X | `link_x_coord` | `0x7E0022` | 16 |
| facing | `link_direction_facing` | `0x7E002F` | 8 |
| overworld screen | `overworld_screen_index` | `0x7E008A` | 16 |
| dungeon room | `dungeon_room_index` | `0x7E00A0` | 16 |
| overworld area | `overworld_area_index` | `0x7E040A` | 16 |
| rupees | `link_rupees_actual` | `0x7EF362` | 16 |
| heart capacity | `link_health_capacity` | `0x7EF36C` | 8 |
| health now | `link_health_current` | `0x7EF36D` | 8 |
| magic | `link_magic_power` | `0x7EF36E` | 8 |
| text pointer table | `kTextDialoguePointers` | `0x7FF1C0` | — |

Health is counted in eighths of a heart by the game; speech converts that to
hearts and half hearts. Magic and rupees keep the game's own units in the dump
and are converted only when spoken.

### 3.4 Speech policy

- Player-requested answers go through `oga::announce()` with the queue's High
  priority, so a repeat request always speaks (existing behaviour).
- Automatic changes (area entered, health crossing a threshold) are Low and
  deduplicated by the queue. No automatic line may interrupt a dialogue line
  the player triggered.
- Nothing speaks before `ready()`. No "reading" filler.
- `DumpState` writes the raw values for a bug report and speaks nothing.

## 4. Reader content, phase by phase

### P1 — identity, position, status

- `WhereAmI`: overworld area or dungeon and room, spoken with the raw numbers
  until names exist ("Area 3, screen 4" is honest; a guessed name is not).
  Include distance from the room's origin only if it helps; otherwise omit.
- `Status` (new append-only command): hearts and health, magic, rupees. Keys
  when mapped.
- `DumpState`: the table above, with values.

### P2 — names

- A shipped table `Core/alttp_names.cpp` maps overworld area index and dungeon
  room id to names. Area names follow the game's own regions; dungeon names are
  the game's own.
- The table is data about the game, not game data: no ROM bytes, no text ripped
  from the cartridge.
- The light/dark world split and the area index range are measured in P2, not
  assumed.

### P3 — menus

- File select (three save slots, name, progress).
- Item menu and equipment menu, driven by the existing `MenuNext` / `MenuPrev`
  / `MenuLeft` / `MenuRight` commands, so the host's menu tracker works with no
  new mechanism.
- Cursor addresses are not in the table above because they are not yet
  measured. P3 begins with live reverse engineering of those screens, using the
  repo's existing tooling, and records what it finds.

### P4 — dialogue

- The game decompresses dialogue into the `kTextDialoguePointers` region. The
  decoder is ported from the decomp's own text routine (`src/messaging.c` in
  zelda3), with no game data.
- Speech fires when a new message index appears, not every frame, and finishes
  before any automatic line.
- Choice prompts ("yes/no") speak each option and the current selection.

### P5 — guidance

- Counted steps and turns, following `docs/design/navigation-prior-art.md`.
- Requires room collision or a waypoint graph. The cheapest source is the
  game's own room data in the ROM; the fallback is a hand-built graph per room.
  This is the largest phase and does not gate P1–P4.

## 5. Failure behaviour

| Situation | Behaviour |
|---|---|
| Unknown SNES dump | Console loads, no adapter, UI shows no reader controls. Log names the CRC. |
| Known CRC, reads out of range | `attach` refuses. Nothing speaks. |
| Console stopped or reset | Adapter detaches on the next frame; the UI hides controls. |
| Mesen fails to load the file | `snes_last_error` carries Mesen's own message, as the NES path does. |
| Audio device absent | Frames still run; reader unaffected. |

## 6. Testing

1. **Unit tests, no ROM.** `Core/alttp_adapter_test.cpp` with a stubbed host
   and a synthetic 128 KiB work-RAM image, like the Mortal Kombat test. Covers:
   identity refusal on a wrong CRC, coordinate sanity, health conversion,
   speech strings, and `DumpState`.
   Runner: `scripts/alttp-adapter-test.sh`, added to `scripts/adapter-tests.sh`.
2. **Host proof, real ROM.** `scripts/snes-alttp-proof.sh` builds a probe that
   loads the ROM through the core, runs a fixed number of frames, and prints:
   CRC, frames completed, PPU frame count, `frame_counter`, Link X/Y, area,
   room, health. It fails when any value is out of range or when the frame
   counter does not advance by the expected amount. No input is scripted in
   P0; the title screen is enough to prove the console and the reads.
3. **Adapter smoke on the host.** A second probe drives the game through the
   app's own `PokeCore` path, so the wiring is proven separately from the core.
4. **Phone measurement.** Android build, then frames per second and the first
   ten seconds of the opening on Devin's phone. This is the only number that
   decides whether the SNES route is viable on device.
5. **Playtest.** Devin plays the opening. Speech usefulness is his call.

## 7. Phases and acceptance criteria

| Phase | Deliverable | Size | Acceptance |
|---|---|---|---|
| P0a | `snes_core` loads a ROM, runs frames, reads work RAM | L | Host probe boots the USA dump, 600 frames, values plausible, counter advancing |
| P0b | Backend wiring: resolver, `pokecore`, `systems`, build lists, Swift picker | M | The app loads and displays the ROM; no regression in `scripts/adapter-tests.sh` |
| P1 | Adapter identity, `WhereAmI`, `Status`, `DumpState` | M | Unit tests pass; host probe speaks area/room/health from live values |
| P2 | Area and room name tables | M | Every area and every mapped room returns a name; unknown ids say "unknown room N" |
| P3 | File select and item/equipment menus | L | Each menu row speaks on cursor moves; addresses recorded with observed values |
| P4 | Dialogue | M | Opening uncle text and the first NPC lines speak once each, in order |
| P5 | Guidance | L | Counted steps into the first dungeon's first room; stops on walls |
| P6 | Android build and phone measurement | M | FPS measured; Devin completes the opening on the phone |

Sizes are relative to the code that exists: L is "a new subsystem", M is "one
file plus tests", not hours.

## 8. Risks

1. **Phone speed is unmeasured.** Mesen's SNES interpreter may be too slow on
   Android. P0a and P6 measure it. If it fails, the fallback is a lighter SNES
   core with the same adapter seam, and that decision is made on numbers.
2. **Work-RAM reads race the emulator thread.** The NES core reads live memory
   the same way. P0a measures stability by reading `frame_counter` across 600
   frames and asserting it advances.
3. **Menus and dialogue need new reverse engineering.** Only the table in §3.3
   is measured. P3 and P4 each begin with live RE and can slip without
   affecting P1.
4. **Names are curated.** The table will have gaps; the adapter must say "room
   42" rather than invent a name.
5. **No sound at first.** A Zelda dungeon silent is a worse experience, but not
   a broken reader. Audio is scheduled after P1.

## 9. Open questions, with the default I would pick

1. **Sound.** Default: skip in P0, wire Mesen audio after P1 if the phone can
   afford it. Revisit when the reader is usable.
2. **A separate `Status` command, or fold health into `WhereAmI`?** Default:
   separate, append-only, because health changes constantly and location does
   not, and a player will want one without the other.
3. **Key counts.** Default: read from the game's own dungeon save table once
   its layout is measured in P1; if that proves expensive, speak keys only on
   the item menu in P3.
4. **Which revisions to support.** Default: the USA 1.0 dump measured here.
   Other regions and revisions are added only when a CRC is measured on a real
   file.
5. **Room naming granularity.** Default: dungeon name plus room number, with a
   short landmark phrase once a table exists.

## 10. Not in scope

- Randomizer support.
- Save editing or cheating.
- Other SNES games' readers (the console is generic; the reader is per game).
- iOS build of the SNES console before Android is measured.

## 11. Status log

- 2026-10-17: console implemented and proved on the host: load + payload CRC identity,
  frame loop, video, input, work-RAM reads, backend wiring, registry READY, savestates;
  `scripts/snes-alttp-proof.sh` is the proof. The iOS app builds and its picker offers
  `.sfc/.smc/.fig/.swc`.
- 2026-10-17: ⛔ THE ANDROID APP HAS NO BACKEND SEAM AT ALL (`app/src/main/cpp` is
  melonDS only; no Mesen, no `oga_core`). The phone measurement therefore moved to iOS,
  or waits for a separate Android plan. Measured: `docs/evidence/alttp-android-fps.txt`.
- 2026-10-17: reader plan written: `docs/plans/alttp-reader.md` (identity, where am I,
  status, names, file select, item and equipment menus, dialogue).
- 2026-10-17: design written. Nothing implemented.
