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
