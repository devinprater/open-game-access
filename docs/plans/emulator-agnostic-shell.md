# Plan: make the shell emulator-agnostic

Scoped 2026-10-04. The goal is that adding a console is a row plus a core, not a
hunt through the UI. Requested systems, in the order Devin named them:

**NES** (a Zelda accessibility mod), **N64** (Super Mario 64), **PS1** (Metal Gear
Solid — good sound design), **PSP**, **NDS**. Later: **PS2**, **GameCube**, **Wii**,
and Mario Kart 8 / Deluxe.

## What already exists (verified, not assumed)

| Piece | State |
|---|---|
| `Core/systems.{h,cpp}` | **New.** The registry: 11 consoles, hardware facts, backend state. 40 tests + 6 mutations, in CI. |
| `Sources/CPokeCore/include/pokecore.h` | A ~40-function C ABI that is **already system-neutral**. No DS in it. Supports DS, GB, GBA, PSP today. |
| `Core/pokecore.cpp` | Dispatches by file extension across three backends (`nds`, `gba`, `psp`). Already multi-core; the extension checks are its own, not the registry's. |
| `Core/gba_core.h`, `Core/psp_core.h` | Two backend interfaces sharing 17 of ~20 function shapes. The seam exists informally. |
| `GameSystem.swift` (iOS) | Per-system UI, but mixes hardware facts with script facts and hardcodes four systems. |
| Android `MelonInstance` | **DS-hardwired.** This is the real work. |

## The three things that actually need doing

### 1. One backend interface (both platforms)

`gba_core.h` and `psp_core.h` already agree on 17 functions: load, start, stop,
frame, framebuffer, button, save/load state, speech callback, last error. That
convergence is not an accident — it is the interface trying to be born.

Formalize it as one `OgaCore` vtable. Then a backend is a struct of function
pointers, `pokecore.cpp` holds `OgaCore*` instead of three nullable pointers plus
three `isX` booleans, and **104 branch sites collapse**. This is the change that
makes console #5 cheap.

### 2. Android stops assuming melonDS

Android does not use the shared `Core/` at all — it has `MelonInstance` (DS) and a
separate `MGBAScriptJNI` (GBA), with nothing for PSP. The native side is not the
hard part (the JNI entry points already exist); the UI is: 578 Kotlin files sit in
a package called `me.magnum.melonds` and the ROM picker only knows `.nds`.

The path: point Android at the same `Core/` the iOS app uses, via JNI, and let the
UI read `Core/systems.{h,cpp}` for what a console is. That is a real project, not a
patch, and it should be sequenced after (1).

### 3. Both UIs read the registry

iOS `GameSystem` becomes a thin Swift wrapper over `oga_system_*`. Script/hotkey
tables move to the adapter layer where the game is known. Android does the same
over JNI.

## Per-console reality check

| System | Core | Feasible? | The catch |
|---|---|---|---|
| DS, GB, GBA, PSP | melonDS-lua, mGBA, PPSSPP | **In this build** | PSP is GPL: irreversible, see `multi-core-mgba-ppsspp.md` |
| NES | Mesen, or FCEUmm | Yes | CPU is trivial; audio-accurate cores exist |
| SNES | a SNES core | Yes | Fine on ARM64 |
| N64 | needs a recompiler | Yes, with effort | **JIT.** ARM64 iOS gives no JIT entitlement, so this is interpreter-or-nothing on sideloads. Perf to be measured before promising |
| PS1 | DuckStation-class | Yes | Software renderer is required (no GPU on this path); Metal/GLES port is the work |
| PS2 | PCSX2-class | **Not soon** | Very heavy. On phone it is years out; I would not plan around it |
| GC/Wii | Dolphin-class | **Not soon** | Same, plus 32-bit ARM is a wall |

Devin's Mario Kart 8 / Deluxe case has a further catch worth stating plainly:
**that is a Switch game.** No Switch emulation is viable on iOS or Android for
sideloaded apps — it needs hardware support no phone provides, and the legal
position is worse than the technical one. Mario Kart 8 Deluxe will not run here.
Mario Kart 64 (N64) or Double Dash (GameCube) would.

## Order of work

1. **`OgaCore` vtable** — collapse `pokecore.cpp`'s dispatch. No new console yet.
2. **iOS reads the registry** — delete `GameSystem`'s hardcoded switch.
3. **NES** — smallest real win, and it proves steps 1–2 with a console neither
   existing backend resembles. Mesen's core is clean C++.
4. **Android reads the registry** — retire `MelonInstance`.
5. **PS1**, then **N64** (measure first), then **SNES**.
6. PS2 / GC / Wii: not planned; revisit when a core exists that runs on a phone.

Each step is shippable on its own and none of them require the others.

## What this does NOT change

The accessibility design stays exactly as it is: one speech channel, the
announcement queue, adapters keyed by game ID, and the Lua script where a game has
one. Making the shell agnostic is about *which emulator runs the ROM* — the reader
and speech layers already sit above that and do not care which console produced
the RAM they read.
