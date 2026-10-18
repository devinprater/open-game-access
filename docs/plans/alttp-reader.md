# A Link to the Past Reader — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A native OGA adapter that lets a blind player ask A Link to the Past where they are, what state Link is in, what the menus say, and what the game is saying.

**Architecture:** One registered adapter (`Core/alttp_adapter.cpp`) reading the SNES work RAM that `Core/snes_core.cpp` already exposes, speaking through the announcement queue, and matching the dump by `crc:777aac2f`. Names, menus and dialogue are separate tasks because each needs its own live measurement first.

**Tech Stack:** C++17 adapter against `oga::Host`, the announcement queue, host-stub unit tests, and the existing `scripts/snes-alttp-proof.sh` probe.

**Spec:** `docs/design/alttp-adapter.md`

## Global Constraints

- Never commit a ROM, ROM-derived text, or game data. Names are curated text about the game, not extracted bytes.
- Every address is confirmed against live memory before use. The table below comes from the decompilation's naming and is a hypothesis until a task's measurement step records the observed value.
- Refusal over guessing: when a read is out of range or the state is not readable, speak nothing rather than a number.
- Speech goes through `oga::announce()`. Player-requested answers are `Priority::High`; automatic lines are `Low` or `Normal`. No speech before `ready()`.
- The C ABI is append-only: `Command` gains `Status` at the end, nothing is reordered.
- `scripts/adapter-tests.sh`, `scripts/systems-test.sh` and `scripts/snes-alttp-proof.sh` stay green at every commit.
- Commits on `main`, no PRs.

## Review Focus

- **A ROM that has only just loaded.** The adapter must be not-ready and silent while the game is still booting, even though the CRC matches immediately. A reader that speaks before the game has state is the "bunch of numbers" failure.
- **Health at zero, death, and save-and-quit.** Zero hearts is a real answer and must speak as such; the adapter must not treat 0 as "unreadable" and go quiet at the worst moment.
- **A question asked during dialogue.** A `WhereAmI` or `Status` answer must be High, must not be swallowed by dedup, and must not be replaced by a dialogue line arriving in the same group.
- **`Status` on the file-select screen.** Gameplay values there are stale leftovers. The adapter must say there is no game in progress, or read the file-select screen, never yesterday's hearts.
- **A refused read.** With the console stopped or the RAM block unavailable, commands speak "I cannot read the game right now" at most once, then stay silent.

---

### Task 1: Adapter skeleton, validation, DumpState

**Files:**
- Create: `Core/alttp_adapter.cpp`
- Modify: `Core/adapters.cpp`
- Create: `Core/alttp_adapter_test.cpp`
- Create: `scripts/alttp-adapter-test.sh`
- Modify: `scripts/adapter-tests.sh`

**Interfaces:**
- Produces: `const Adapter kZeldaLinkToThePast` with `id = "alttp"`, `display_name = "The Legend of Zelda: A Link to the Past"`, `game_code = "crc:777aac2f"`.
- Produces (internal): `uint8_t u8(uint32_t)`, `uint16_t u16(uint32_t)`, `bool InRam(uint32_t addr, int n)`, `bool StateSane()`.

- [ ] **Step 1: Write the failing test**

`Core/alttp_adapter_test.cpp` supplies a stub `Host` over a 128 KiB synthetic image, with `ctx` pointing at it. Assert:
1. `attach` returns false when `0x7EF36C` (capacity) is 0 and coordinates are 0 — boot state.
2. `attach` returns true when capacity is `0x18`, current health is `0x18`, coordinates are `0x0800`/`0x0800`.
3. `ready()` is false before attach and true after a sane attach.
4. `DumpState` speaks nothing, and logs a line containing `health` and `area`.

- [ ] **Step 2: Run it and watch it fail**

Run: `bash scripts/alttp-adapter-test.sh`
Expected: compile error naming `kZeldaLinkToThePast`.

- [ ] **Step 3: Implement the skeleton**

`alttp_adapter.cpp` keeps a file-scope `const Host* g_host` and an `attached` flag. `Say()` mirrors `Core/dq9_adapter.cpp`: dedup note, `announce()` with a group, direct `speak` fallback when the queue is absent. `StateSane()` requires capacity between `0x18` and `0xA0`, current health `<= capacity`, and both coordinates non-zero. Register the adapter in `adapters.cpp` with a comment saying the id is a CRC because the cartridge has no game code.

- [ ] **Step 4: Run it**

Run: `bash scripts/alttp-adapter-test.sh`
Expected: every assertion prints `ok`, final line `PASS`.

- [ ] **Step 5: Add it to the shared runner and commit**

Add `alttp-adapter-test.sh` to the loop in `scripts/adapter-tests.sh`.

```bash
git add Core/alttp_adapter.cpp Core/adapters.cpp Core/alttp_adapter_test.cpp scripts/alttp-adapter-test.sh scripts/adapter-tests.sh
git commit -m "alttp: adapter skeleton, validation and dump"
```

---

### Task 2: Where am I

**Files:**
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`

**Interfaces:**
- Consumes: Task 1's `u8`, `u16`, `StateSane`.
- Produces: `void SpeakWhereAmI()` bound to `Command::WhereAmI`.

- [ ] **Step 1: Measure before speaking**

Extend the probe (or use `scripts/ramdiff.py`) to print `0x7E0010`, `0x7E0011`, `0x7E001B`, `0x7E008A`, `0x7E00A0`, `0x7E040A`, `0x7E0020`, `0x7E0022` at the title, in Link's house, and outdoors. Record the observed values in `docs/design/alttp-memory.md` with the state each was read in. Acceptance for this step: the values move sensibly between the three states, and `0x7E001B` (indoors) distinguishes house from outdoors.

- [ ] **Step 2: Write the failing test**

Stub image with `player_is_indoors = 0`, `area = 3`, `screen = 4` expects the exact line `Area 3, screen 4.`; with `indoors = 1`, `dungeon room = 12` expects `Dungeon room 12.`; with `ready() == false` expects no speech.

- [ ] **Step 3: Run it and watch it fail**

Run: `bash scripts/alttp-adapter-test.sh`
Expected: FAIL, the command speaks nothing.

- [ ] **Step 4: Implement**

`WhereAmI` speaks High through `Say(..., "whereami", Priority::High)`. Numbers only in this task; names arrive in Task 4.

- [ ] **Step 5: Run it, then commit**

```bash
git add Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp docs/design/alttp-memory.md
git commit -m "alttp: where am I, with measured area and room numbers"
```

---

### Task 3: Status, and the command that asks for it

**Files:**
- Modify: `Core/adapter.h`
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`
- Modify: `Sources/OpenGameAccess/AdapterCommand.swift`

**Interfaces:**
- Produces: `Command::Status` appended after `RepeatOlder` (numeric value 40).
- Produces: `void SpeakStatus()`.

- [ ] **Step 1: Write the failing test**

With capacity `0x18`, health `0x14`, magic `0x40`, rupees `0x00F2`, the expected line is `Two and a half hearts. Magic half. Two hundred forty-two rupees.` With health `0` expect `Zero hearts.` — a real answer, not silence. With a refused read (host read returns 0 but attach was never made) expect `I cannot read the game right now.` once.

- [ ] **Step 2: Run it and watch it fail**

Run: `bash scripts/alttp-adapter-test.sh`
Expected: FAIL, `Command::Status` does not exist.

- [ ] **Step 3: Implement**

Append `Status` to `Command` in `adapter.h` with a comment that the value is the C ABI. Implement hearts as `health / 8` plus `and a half` when the remainder is 4; magic as a percentage of `0x80` rounded to quarters; rupees as a number. Add `case status = 40` and its title in `AdapterCommand.swift`, and add `"alttp"` to `supported(adapterID:)` returning `[.whereAmI, .status, .dumpState]`.

- [ ] **Step 4: Run the tests and the app build**

Run: `bash scripts/alttp-adapter-test.sh && bash scripts/xtool-build.sh`
Expected: PASS, then `Build complete!`.

- [ ] **Step 5: Commit**

```bash
git add Core/adapter.h Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp Sources/OpenGameAccess/AdapterCommand.swift
git commit -m "alttp: status command with hearts, magic and rupees"
```

---

### Task 4: Names for areas, dungeons and rooms

**⛔ MEASURED 2026-10-17 — BLOCKED ON A SAVE.** The scripted drive reached
gameplay (module 7, indoors=1, room=260, hp 24/24) and confirmed the indoor room
and health addresses, but could not leave Link's house, so the overworld area and
screen ids, the dungeon identifier, menu cursors and dialogue bytes are unmeasured.
Names for unmeasured ids would be invented. Needs a `.srm` from a real playthrough;
`scripts/snes-alttp-proof.sh --gameplay` writes one when the game saves. See
`docs/design/alttp-memory.md`.

**Files:**
- Create: `Core/alttp_names.h`
- Create: `Core/alttp_names.cpp`
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`

**Interfaces:**
- Produces: `const char* AlttpAreaName(uint16_t area)` returning `nullptr` for an unknown id; `const char* AlttpDungeonName(uint8_t dungeon)`; `const char* AlttpRoomNote(uint16_t room)`.

- [ ] **Step 1: Measure the id ranges**

With the probe, record `overworld_area_index` at the light-world start, in the dark world after the first crystal, and inside two dungeons. Record which value ranges are light and dark, and what identifies the current dungeon. Write the observations into `docs/design/alttp-memory.md`. Acceptance: the light/dark split and the dungeon identifier are known numbers, not assumptions.

- [ ] **Step 2: Write the failing test**

`AlttpAreaName(0x00)` returns the name recorded in Step 1 for the light-world start; `AlttpAreaName(0xFF)` returns `nullptr`; `WhereAmI` on an unknown area speaks `Area 255.` rather than inventing a name.

- [ ] **Step 3: Implement the table and the fallback**

The table holds the game's own region names (Hyrule Castle, Eastern Palace, Death Mountain, and so on) keyed by the measured ranges. Unknown ids fall through to numbers. Room notes are a short landmark phrase for rooms measured in this task; unmapped rooms get no note.

- [ ] **Step 4: Run it, then commit**

```bash
git add Core/alttp_names.h Core/alttp_names.cpp Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp docs/design/alttp-memory.md
git commit -m "alttp: area, dungeon and room names with numeric fallback"
```

---

### Task 5: The file-select screen

**Files:**
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`
- Modify: `docs/design/alttp-memory.md`

**Interfaces:**
- Produces: `void SpeakFileSelect()`, plus menu detection in `StateSane` that keeps gameplay answers off this screen.

- [ ] **Step 1: Measure the screen**

Drive the game to the file-select screen with the probe's input, dump RAM before and after pressing Down, and diff with `scripts/ramdiff.py` to find the cursor. Record the module/submodule values that identify this screen and the cursor address. Acceptance: a recorded address that changes 1→2→3 on Down and wraps.

- [ ] **Step 2: Write the failing test**

Synthetic image with the measured module values and cursor 2 expects a line naming slot 2; with the cursor at an unnamed slot expects `Slot 3.` (numbers, no invented name).

- [ ] **Step 3: Implement**

`MenuNext`/`MenuPrev` speak the highlighted slot and its contents (name, hearts, dungeon count when readable). `Status` on this screen speaks the file-select state instead of gameplay values.

- [ ] **Step 4: Run it. Then prove it on the game**

Run: `bash scripts/alttp-adapter-test.sh`, then a probe mode that asks `Status` on the file-select screen and prints what was spoken. Expected: a slot line, and no hearts from a previous game.

- [ ] **Step 5: Commit**

```bash
git add Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp docs/design/alttp-memory.md scripts/snes-alttp-proof.sh
git commit -m "alttp: file-select screen"
```

---

### Task 6: Item and equipment menus

**Files:**
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`
- Modify: `docs/design/alttp-memory.md`

**Interfaces:**
- Produces: `void SpeakItemMenu()`, `void SpeakEquipment()`.

- [ ] **Step 1: Measure the menus**

Open the item menu with the probe, dump with `scripts/ramdiff.py` while moving the cursor, and find the cursor address. Candidate inventory bytes already named by the decompilation: `0x7EF340`–`0x7EF357` (items), `0x7EF359` sword, `0x7EF35A` shield, `0x7EF35B` armour. Confirm each against the screen before speaking it.

- [ ] **Step 2: Write the failing test**

Synthetic image with sword `2`, shield `1`, armour `1`, and a cursor on the bow expects `Bow. Sword two. Shield one. Armour one.`

- [ ] **Step 3: Implement**

`MenuNext`/`MenuPrev` move through items and speak name plus state. `MenuLeft`/`MenuRight` speak equipment values. Item names come from a table in `Core/alttp_names.cpp`; unknown item ids speak `Item 31.`

- [ ] **Step 4: Run it, then commit**

```bash
git add Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp Core/alttp_names.cpp docs/design/alttp-memory.md
git commit -m "alttp: item and equipment menus"
```

---

### Task 7: Dialogue

**Files:**
- Create: `Core/alttp_text.h`
- Create: `Core/alttp_text.cpp`
- Modify: `Core/alttp_adapter.cpp`
- Modify: `Core/alttp_adapter_test.cpp`

**Interfaces:**
- Produces: `int AlttpDecodeMessage(const uint8_t* ram, uint32_t text_base, char* out, int cap)` returning decoded bytes or -1.
- Produces: automatic speech when a new message id appears.

- [ ] **Step 1: Measure the message state**

Find the byte that identifies the current message and the one that says a message is on screen, by dumping RAM before, during and after the uncle's opening text. Record both in `docs/design/alttp-memory.md`. Acceptance: the on-screen byte is set while the box is up and clear when it closes, and the message byte changes between two different lines.

- [ ] **Step 2: Write the failing test**

A synthetic text buffer containing a known encoded sentence decodes to that sentence; an empty buffer returns -1; a truncated buffer returns -1 rather than a half sentence.

- [ ] **Step 3: Implement the decoder**

Port the encoding rules from the decompilation's text routine (`~/z3work/zelda3/src/messaging.c`), not from guesses: the dictionary lookup, the terminator, and the control codes that insert the player's name. No game text is committed; the test's sentence is written for the test.

- [ ] **Step 4: Wire it to speech**

`on_frame` checks the message byte; when a new message appears, decode and speak at `Normal`, group `dialogue`, replacing any pending dialogue line. Player commands keep group `whereami`/`status`, so they are never replaced by dialogue.

- [ ] **Step 5: Run it, then commit**

```bash
git add Core/alttp_text.h Core/alttp_text.cpp Core/alttp_adapter.cpp Core/alttp_adapter_test.cpp docs/design/alttp-memory.md
git commit -m "alttp: dialogue decoded from the game's own text buffer"
```

---

## Follow-on plan

Guidance (counted steps, room routes) stays out of this plan. It needs room collision or a waypoint graph, which is its own measurement project and its own spec section (`docs/design/navigation-prior-art.md`). It gets a plan after Task 7, when speech, names and dialogue are stable.
