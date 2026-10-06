# NES readers: Zelda 1 Access and Dragon Warrior Access

**State: both host and speak. Deep gameplay narration is not yet reached.**

## The mods

| game | repo | how it ships | entry file |
|---|---|---|---|
| Zelda 1 | `GADeuvall2000/Zelda1Access` | `Zelda1Access v1.0.zip` committed to the repo (releases have NO assets) | `Zelda1Access.lua` |
| Dragon Warrior | `GADeuvall2000/DragonWarriorAccess` | `DragonWarriorAccess v1.0.zip` committed to the repo | `DragonWarriorAccess V1.0.lua` |

File names inside are **deliberately obfuscated** (`nT9wK2bX6jHsR.lua`); identify by content.
Installed at `Resources/nes-lua/<game>/`, with an `oga_nes_reader.lua` wrapper beside each.

## How they speak — and why that mattered

⛔ **Neither reader calls a speech API.** They write `"<sequence>|<message>"` into a small text file
and a separate NVDA bridge process polls it. So a reader that loads and runs perfectly can still be
completely silent, and it looks identical to a working one.

`Core/mesen_core.cpp` polls that file once per frame and forwards each NEW sequence to the speech
sink, so the mod's own file protocol stays the single source of truth and a mod update cannot
silently break speech.

**Three wrong turns getting there, each of which "worked":**

1. **A content-shape test picked the wrong file.** Both mods also ship a `sound_bridge_command.txt`
   whose contents look like `"1|reset"` — the same `seq|text` shape as real speech. The heuristic
   chose it and reported hearing "reset". Fixed by asking the reader: `oga.set_speech_file()` is
   called by the wrapper from the reader's OWN declaration.
2. **The path arithmetic was re-implemented and dropped `/Data`.** The readers build paths from
   `ROOT_DIR .. "/Data"`; taking only the literal suffix produced a path that does not exist — so the
   poll read nothing while the log said "speech file set". Fixed by evaluating the reader's own
   expression with `DATA_DIR`/`ROOT_DIR` bound.
3. **The declaration is not always in the entry file.** Zelda declares `SPEECH_FILE` at file scope;
   Dragon Warrior declares `speech_file` inside a **Data module** and its entry never mentions it.
   Fixed by searching the whole tree, entry first.

## What the host provides

`Core/mesen_core.cpp` now carries a Lua host mirroring `Core/gba_core.cpp` — one reader-hosting
pattern in this repo, not two. Bound: `mainmemory.*` (NES-correct: the offset IS the address, because
the CPU space is flat 64 KB from 0), `memory.read_u8`/`read_u16_le`/`read_u32_le`/
`read_bytes_as_array`, `memory.usememorydomain`, `emu.framecount` (the console's REAL frame count,
not the shim's Lua call counter), `emu.frameadvance`, `console.log`, `gui.text`, `joypad.set/get`,
and `oga.set_speech_file`.

`memory.usememorydomain("CIRAM (nametables)")` is honoured through `NesConsole::DebugReadVram` — the
public path Mesen's own debugger uses. Dragon Warrior reads nametable RAM to identify the current
menu, so this is a real capability, not a formality. `BaseMapper::GetNametable` is protected and
therefore unusable by a library caller.

## Measured results

    Zelda 1 Access .......... loads, runs, and SPEAKS: "Inventory Menu.", "Inventory Menu Closed."
    Dragon Warrior Access ... loads, runs, and SPEAKS: "Warning: Critical health."

Both report their own success through the log and both are reaching the speech sink, which is the
proof that the whole path works: console -> Lua host -> wrapper -> mod -> file -> speech.

## What is NOT yet working, stated plainly

- ⛔ **Zelda's map/room narration has not been observed.** The two lines above are its menu-state
  announcements, not the room/area speech the reader is known for. The likely reason is that the
  scripted input never advanced the game far enough (START at the title, then a short walk) — but
  that is a hypothesis, not a measurement, and it has not been tested.
- ⛔ **Dragon Warrior's "Critical health" fired on a fresh boot**, which is suspicious: a new game
  does not normally start at critical HP. Either the ROM needs the name-entry flow the script did not
  drive, or a read is landing on the wrong address. Not investigated.
- ⛔ **The mods' PowerShell SoundBridge is not ported.** Both ship one for spatial cues and footsteps;
  OGA has no equivalent path yet. The readers `pcall` around it, so speech works and cues stay
  silent. This is the same conclusion the earlier survey reached from two other codebases: the
  strongest spatial readers put their audio OUTSIDE the emulator.
- The app build does not compile Mesen yet — this is host-only.

## Next steps

1. Drive Zelda past the title and into the overworld, then read what it announces — the room/area
   speech is the thing to prove.
2. Check whether DW's critical-health line is a real state or a mis-landed read (dump the address it
   reads, on a fresh boot, and compare against a known-good value).
3. Wire the NES core into the app build (`build-core.sh`) so this runs on a phone.

## Zelda 1 Access — the name-entry board, measured

The overworld room narration could not be observed because the scripted drive did not get past name
registration. That work did map the registration screen exactly, all from live reads:

- **Board size: 44 cells, 11 wide, 4 rows.** `char_board_index` (0x041F) runs 0..0x2B. DOWN from
  index 0 lands on 11 — the same column, next row — so the row stride is 11, not the 10 I first
  assumed. Index 43 is the bottom-right cell.
- **The cell map is the reader's own table.** `MODE_E_CHAR_MAP[43] = 0x24`, and `CHAR_MAP[0x24] = " "`.
  The full walk speaks A-Z, then hyphen, question mark, comma, exclamation mark, apostrophe,
  ampersand, period, then 0-9 — 44 cells, every one announced.
- ⛔ **`0x24` is the name field's FILLER, not an END marker.** Walking to index 43 and pressing A
  writes the filler into the name; it does not finish the screen. The earlier reading of that cell as
  "END" was wrong.
- **The name is 8 characters** at 0x0638..0x063F. A enters the character under the cursor and advances
  `name_char_offset` (0x0421); at offset 8 the name held `AAAAAAAA` and registration had still not
  ended.

**Confirmed working:** the reader announces every board cell live, and A enters characters (name bytes
observed stepping `2424..` -> `0A24..` -> `0A0A24..`).

**Not found:** the confirm that leaves the registration screen. A, B, START and SELECT were each
tried — including with a full 8-character name — and the mode byte stayed 0x0E. This is a HARNESS
limitation, not a reader fault: the drive also cannot reliably navigate to the bottom-right cell,
because repeated DOWN presses stop registering after the first (the index sits at 11 while RIGHT
continues to work from a fresh row). The board is mapped; only the route through it is unproven.

## Two host gaps that killed Zelda's overworld feature set

Driving the reader into the overworld exposed two things the host was not providing. Both are now
fixed, and the evidence is the reader's OWN log, not my reading of it.

**1. The Lua `bit` library was missing.** BizHawk's LuaJIT exposes a global `bit` table; stock Lua 5.4
does not. The reader uses `bit.band` (18 calls), `bit.rshift` (3) and `bit.lshift` (2) while decoding
its overworld tables, and Dragon Warrior uses `bit.band` (6). Without it the load raised

    RUNTIME ERROR: .../oR2tJ9xN5qLcE.lua:862: attempt to index a nil value (global 'bit')

which the reader's guard turned into a spoken "Navigation error, check log." — the overworld enemy
table and screen manifest were simply never built. Implemented with LuaJIT's 32-bit semantics,
including its 5-bit shift-count masking (a shift by 32 is C undefined behaviour and LuaJIT treats it
as a shift by 0).

**2. The `"PRG ROM"` domain was not implemented.** The reader reads its own static tables straight out
of the cartridge — `rom_read(0x18500 + room_id)`, `rom_read(0x19324 + i)` — passing `"PRG ROM"` as an
explicit domain, exactly as it passes `"System Bus"` for RAM. The resolver treated every unknown
domain as system RAM, so cart-ROM reads returned **live RAM bytes**: silently wrong data rather than
an obvious zero. Now routed to `MemoryType::NesPrgRom` via `Emulator::GetMemory`.

**Proof.** The reader's log after a scripted run now ends with:

    WorldMap: overworld_enemies + screen_manifest loaded

and contains no `RUNTIME ERROR` line at all. Before the fix the same run ended with the `bit` error
above and no manifest line. Both are the reader's own words.

**Also learned, from the disassembly rather than from guessing at the UI** (`aldonunez/zelda1-disassembly`,
`src/Z_02.asm`):

- The file-select menu is navigated with **SELECT** (`UpdateMode1Menu_Sub0`, which cycles
  `CurSaveSlot` and skips inactive entries) and confirmed with **START**. My earlier drives selected
  the menu option with START, which only confirms; the reader's `current_selection` (0x0016) was
  therefore never 3.
- Registration begins with `CurSaveSlot` reset to 0 (`InitModeEandF_Full`), so START alone can never
  leave the screen: `UpdateModeERegister` requires `(ButtonsPressed & $10) && CurSaveSlot == 3`.
  The "End" option is reached there with SELECT, which is what the reader labels "End".
- The board is 11 wide: RIGHT/LEFT step `CharBoardIndex` by 1 (wrapping at $2B), DOWN/UP step it by
  $0B with a row wrap, and the reader's own tables map index 43 to the name field's `0x24` filler.
