# Kanban — Pokémon Access (GB/GBC/GBA) → mGBA → Open Game Access

Plan: `docs/plans/pokemon-gb-gba-mgba.md` · Evidence ledger: `tools/re/platforms/gba/VERIFIED.md`

Cards move only on **verified** evidence. "It loads" is not evidence. Read `VERIFIED.md`
before trusting any claim on this board.

---

## Blocked (needs a human at the machine)

| # | Card | Why blocked |
|---|---|---|
| A5 | Run a game in mGBA and get a real spoken line | **mGBA's Lua has no CLI entry point** — neither `mGBA.exe` nor `mgba-sdl.exe` accepts `--script`; scripting is Tools → Scripting only. Everything else for this card is in place. |
| A6 | Diff spoken output vs VBA v3.1.0 on the same save | Depends on A5. This is the strongest available fidelity test — VBA v3.1.0 is the known-good reference. |

## Ready

| # | Card | Definition of done |
|---|---|---|
| A7 | Run Blue and Yellow to completion | Red reaches `Ready` at frame 60,001; Blue/Yellow unrun. Harness limitation (time + a per-frame allocation leak), **not** a reader failure. |

## Backlog

| # | Card | Notes |
|---|---|---|
| B1 | Verify shim across GB/GBC generations | Largely answered by the ROM-ID matrix — see Done |
| B2 | Verify shim across GBA generations | Largely answered — GBA 4/4 supported games |
| B3 | Verify ROM-hack path (`expansion/firered/cfru.lua`) | untested |
| C1 | Add an mGBA runtime adapter beside the melonDS one in OGA | one interface, two consoles |
| C2 | Prove one OGA speech layer narrates NDS + GBA | the actual integration goal |
| D1 | Port the GBA path to iOS via xtool | deliberately last |
| — | Wire a real host sound sink | 42 `audio.play` cues are recorded, not played. Real positional audio needs a host callback (the Android bridge already has this pattern), not a Lua BASS clone. |

## Done (verified by execution)

| # | Card | Evidence |
|---|---|---|
| A1 | mGBA installed via Scoop | 0.10.5; Lua scripting confirmed present in `CHANGES.txt` |
| A2 | mGBA's Lua API determined | object-oriented (`emu:read8`, `callbacks:add("frame")`), documented at mgba.io/docs/scripting.html |
| A3 | Reader API surface enumerated | **16 distinct host functions across 166 files / 688 KB** — `memory.readbyte` 161 call sites, `readword` 80, `readdword` 76, `getregister` 72, `registerexec` 6 |
| A3b | `readbyterange` shape resolved | readers index a **1-based table**; mGBA returns a *string*, and numeric indexing of a string yields nil — a silent-empty-text bug |
| A4 | Host shim written | `mgba_compat.lua`, compiles clean |
| A4b | `bit` library | **119 call sites**, never required — a LuaJIT built-in. Reimplemented with signed-32-bit semantics incl. 5-bit shift masking |
| A4c | `module()` / `tolk` / `audio` / `crc32` | `module()` was **removed in Lua 5.2** and `a-star.lua` uses it on the reader's first statement. `crc32` reimplemented (not stubbed) — a nil stub would report "unsupported game" for a supported one |
| A5b | Real-ROM identification | **6 of 9 supported games PASS**: Gold/Silver/Crystal (GBC), Emerald/FireRed/LeafGreen (GBA). Red reaches `Ready`. Ruby/Sapphire correctly *rejected* — not in v3.1.0's support list |
| A5c | Exec-hook emulation | 17 checks: fires on PC match, `nil` unregisters, hooks independent, a throwing callback doesn't kill the session, `registerwrite` fires on change not every frame |
| — | Mapping correctness | 31 checks incl. `gb.lua`'s own row/column pattern and preservation of the `0xED`/`0xEE` menu markers |
| — | `crc32` correctness | 14 checks vs published CRC-32 vectors, **including table input** |
| P1 | Reader source located | **the real v3.1.0 install** at `Dropbox\programs\pokemon-access\` — not the Temp copy |
| P2 | Plan + board + ledger written | `pokemon-gb-gba-mgba.md`, this file, `VERIFIED.md` |

---

## The shape of what happened

The plan was written believing this was a hosting job with one gap (a host-API shim). That was
right, but the gap was **six** LuaJIT-isms deep, and **every one of them was found by running
the reader rather than reading it**:

`ffi` → `module()` → `tolk` → `audio.dll` → `bit` → and the `readbyterange` shape.

Two of them (`module()`, `bit`) are not mentioned in any BizHawk documentation because they
are *implicit* — a runtime you get for free. That is exactly why the load path had to be
executed to be trusted.

## Two errors of my own, recorded so they are not repeated

**A grep that was too narrow.** I recorded "`audio.` is never called" after searching only
`pokemon.lua`. The whole set has **42 `audio.play` calls**. I bypassed the DLL on that basis
and the GB reader then died at `gb.lua:516`.

**A budget that was too small.** A 200-frame test budget made the GB games look like they
hung. They need ~60,001 frames because `main_loop` reads the full 360-byte screen every frame.
Both errors had the same shape: **a measurement too narrow to support the conclusion drawn
from it.**

## What is NOT verified

Everything needing live memory: any address or length; whether footsteps actually fire; whether
the speech is *meaningful*; performance and stability under mGBA's threading (the reader owns
`while true do emu.frameadvance() … end`, which is BizHawk's model). `VERIFIED.md` lists these
explicitly.
