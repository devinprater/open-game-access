# Plan: make the shell emulator-agnostic

Scoped 2026-10-04. The goal is that adding a console is a row plus a core, not a
hunt through the UI. Requested systems, in the order Devin named them:

**NES** (a Zelda accessibility mod), **N64** (Super Mario 64), **PS1** (Metal Gear
Solid — good sound design), **PSP**, **NDS**. Later: **PS2**, **GameCube**, **Wii**,
and Mario Kart 8 / Deluxe.

## What already exists (verified, not assumed)

| Piece | State |
|---|---|
| `Core/systems.{h,cpp}` | The registry: 14 consoles, hardware facts, backend state. 58 tests + 6 mutations, in CI. **Now IN the build** (it was in no build list, so on device it did not exist) and exposed to the UI through the C ABI. |
| `Sources/CPokeCore/include/pokecore.h` | A ~40-function C ABI that is **already system-neutral**. No DS in it. Supports DS, GB, GBA, PSP today. |
| `Core/pokecore.cpp` | Dispatches by file extension across three backends (`nds`, `gba`, `psp`). Already multi-core; the extension checks are its own, not the registry's. |
| `Core/gba_core.h`, `Core/psp_core.h` | **Done (2026-10-04).** Formalised as one `OgaCore` ops table in `Core/oga_core.h`. The ~100 dispatch sites in pokecore.cpp are gone; `poke_frame` and `poke_framebuffer` are now one body each for every console. |
| `GameSystem.swift` (iOS) | **Done (2026-10-04).** Hardware facts now READ FROM the registry over the C ABI; the script/hotkey tables stayed, because they belong to the loaded script, not the console — see the note in the file. |
| Android `MelonInstance` | Still **DS-hardwired** for NDS. The Game Boy path is now REACHABLE (see below) but does not go through the shared `Core/` yet. |

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
| NES | **Mesen (pinned, admitted)** | **Yes — measured** | 45 TUs + Shared compile for aarch64-linux-android26 (2026-10-04). Adapter seam already exists |
| SNES | **Mesen Core/SNES** | **Yes — measured** | 66 TUs. Replaces the unnamed "a SNES core", and it is the same tree as the NES |
| GB / GBC | mGBA (**or Mesen Core/Gameboy**) | Yes | mGBA path is already in the build; Mesen's 19 TUs are a second option |
| GBA | mGBA (**or Mesen Core/GBA**) | Yes | mGBA is wired in; Mesen's 23 TUs are a second option |
| SMS / Game Gear | **Mesen Core/SMS** | **Yes — measured** | 17 TUs — the cheapest console in the set |
| PC Engine | **Mesen Core/PCE** | **Yes — measured** | 27 TUs. CD titles (.cue) are claimed by the PS1 row, so discs are out of reach |
| WonderSwan | **Mesen Core/WS** | **Yes — measured** | 19 TUs |
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

1. ~~**`OgaCore` vtable**~~ — **DONE 2026-10-04.** See `Core/oga_core.h`.
2. ~~**iOS reads the registry**~~ — **DONE 2026-10-04.** Hardware facts come from
   the registry; script keys stay with the script, deliberately.
3. ~~**Wire the Game Boy path end to end**~~ — **PARTLY DONE 2026-10-04.** The
   Android launcher can now reach the reader (`GbBridge.kt`), and the host proves
   a real ROM boots and speaks. **The speech is not yet meaningful** — see
   `docs/research/gba-host-proof.md`. That is the next real work.
4. **NES** — smallest real win, and it proves steps 1–2 with a console neither
   existing backend resembles. **The core is now CHOSEN AND ADMITTED**: Mesen2
   is pinned in `scripts/bootstrap-deps.sh`, `scripts/mesen-feasibility.sh`
   compiles all seven of its console cores for this target, and the measured
   lists are in `scripts/core-sources.sh`. What is missing is the host glue
   (`Core/mesen_core.cpp`) — the core is no longer the unknown.
   ⛔ The same measurement turned up the rest of Mesen's set, so SNES, SMS/Game
   Gear, PC Engine and WonderSwan are now listed in the registry as PLANNED with
   a measured core rather than as names. See `docs/emulator-inventory.md`.
   **GB/GBC and GBA have a second, unused Mesen core** — mGBA already boots
   both, so those two are a choice (swap, or keep mGBA) and not a gap.
5. **Android reads the registry** — retire `MelonInstance` for NDS too.
6. **PS1**, then **N64** (measure first), then **SNES**.
7. PS2 / GC / Wii: not planned; revisit when a core exists that runs on a phone.

Each step is shippable on its own and none of them require the others.

## ⛔ What step 3 actually uncovered, because it changes the plan's shape

Two things had been recorded as "done" that were not reachable in a shipped build:

  * **The registry was in no build list.** 52 green tests, a CI workflow, and
    nothing compiled it into the app. "Both UIs read the registry" could not have
    worked: there was nothing to read.
  * **Nothing called the Game Boy path on Android.** The JNI entry points linked
    into the APK; no Kotlin or JS invoked them, and the launcher's status text
    still told the player the core was "not compiled into this test build".

The lesson for the rest of this plan: **"compiles", "links" and "has tests" are
all weaker than "a player can reach it".** Every future console step should end
with a reachability check, not a build check.

## What this does NOT change

The accessibility design stays exactly as it is: one speech channel, the
announcement queue, adapters keyed by game ID, and the Lua script where a game has
one. Making the shell agnostic is about *which emulator runs the ROM* — the reader
and speech layers already sit above that and do not care which console produced
the RAM they read.
