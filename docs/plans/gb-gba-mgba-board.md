# Kanban — Pokémon Access (GB/GBC/GBA) → mGBA → Open Game Access

Plan: `docs/plans/pokemon-gb-gba-mgba.md`

Cards move only on **verified** evidence. "It loads" is not evidence; a line of spoken
output from a running game is.

---

## Backlog

| # | Card | Notes |
|---|---|---|
| B1 | Verify shim across GB/GBC generations | Red/Blue, Yellow, Gold, Silver, Crystal |
| B2 | Verify shim across GBA generations | RSE, FRLG |
| B3 | Verify ROM-hack path | `game/expansion/firered/cfru.lua` |
| C1 | Add mGBA runtime adapter beside melonDS in OGA | one interface, two consoles |
| C2 | Prove one OGA speech layer narrates NDS + GBA | the actual integration goal |
| D1 | Port the GBA path to iOS via xtool | deliberately last |

## Ready

| # | Card | Definition of done |
|---|---|---|
| A3 | Grep the 166 reader files for the exact BizHawk API calls | a list of every `emu.*` / `memory.*` / `joypad.*` symbol used, with counts — this defines the shim's required surface |

## In progress

| # | Card | Status |
|---|---|---|
| A1 | Install mGBA on Windows via Scoop | starting — check what Scoop provides before installing |
| A2 | Determine mGBA's Lua API surface | blocked on A1 |

## Done

| # | Card | Evidence |
|---|---|---|
| P1 | Survey what already exists | 166 Lua files at `Temp/pokemon-access-gb/`; `MGBAScriptJNI.cpp` (11,519 B) + `MGBACore.{cpp,h}` in the Android fork; speech contract `speak(String,boolean)/stop()/playSound(String,int,int)` already resolved via JNI reflection |
| P2 | Plan written | `docs/plans/pokemon-gb-gba-mgba.md` |

---

## The three findings that shape this board

**1. Most of it already exists.** The readers (166 files), an mGBA bridge in C++, and a
working speech contract are all present. This is a hosting job, not a rewrite.

**2. The one real gap is a host-API shim.** The readers target BizHawk's Lua API
(`emu.`, `memory.`, `joypad.`) and grep found **zero** mGBA-specific calls in all 166
files. So the shim is the load-bearing new component — which is exactly what card A3
measures before A4 writes anything.

**3. mGBA's Lua file-loading is the highest-risk unknown.** The reader set is structured
as modules across directories, including per-language trees. If mGBA's Lua cannot load
sibling files, that is a packaging problem (concatenate at load time), not an
architectural one — but it must be established *before* designing the shim, not
discovered halfway through.

## Deliberately not on this board

- **Decompilation.** The readers already read live RAM; that is what accessibility needs.
  A Pokémon decompilation is a much larger job and would only be justified if a reader
  needs a structure runtime inspection cannot locate. Escalate then, not now.
- **New reader logic.** These scripts work in BizHawk today. The task is to host them.
