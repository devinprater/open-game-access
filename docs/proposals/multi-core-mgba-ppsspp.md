# Proposal: mGBA + PPSSPP cores in the iOS app

Goal: boot GBA ROMs (pokemon-access scripts) and PSP ISOs (Dissidia reader)
inside OpenGameAccess, with the same VoiceOver UI and speech-callback pattern
the melonDS core uses today. Scoped 2026-09-20, not started.

## 0. What changes for the player

- "Select Game" accepts `.gba` and `.iso`/`.cso` in addition to `.nds`.
- GBA games get the pokemon-access Lua narration (mGBA ships its own Lua).
- Dissidia gets its native reader (today: PPSSPP-debugger-driven from PC).
- One shared contract per core: load ROM, run frame, framebuffer, audio,
  buttons, speech callback, game id string, adapter commands.

## 1. mGBA (GBA)

- License: MPL-2.0, file-level copyleft. Linking is fine; publish any changed
  mGBA files. No whole-app effect.
- Build: C codebase, CMake, already ships as `libmgba` shared library
  (Arch ships one). Static archive for iOS via the existing clang++-target
  pattern in `scripts/build-core.sh`. No JIT needed: a GBA interpreter is
  trivially full-speed on any modern iPhone.
- Embedding: libmgba is designed for reuse (`mCore`/`mCPU` API). Pin a
  release tag as a submodule under `Vendor/` next to melonDS-lua.
- Script story (the good news): mGBA has built-in Lua since 0.10. The
  pokemon-access GBA scripts expect a VBA-style API, so they need the same
  treatment `bizhawk_compat.lua` gave the NDS script: a compat shim mapping
  `memory.read_u8` et al. onto mGBA's Lua API. Smaller than the NDS shim —
  GBA scripts touch RAM + input + a few registers.
- Adapter story: the existing `gba_adapter.cpp` + `AdapterCommand.supported`
  `"gba"` entry already exist; today the UI returns `[]` for it with the note
  that the Lua hotkeys ARE the interface. That stays true — the work is the
  shim, not new buttons.
- Risks: low. mGBA ports to everything; iOS audio/video plumbing mirrors what
  the melonDS core already does (16-bit 240x160 framebuffer, ~32 kHz audio).

## 2. PPSSPP (PSP)

- License: GPL-2.0-or-later. THIS IS THE BIG ONE. Linking PPSSPP into the app
  makes the distributed app a GPL-covered combined work: anyone who gets the
  app must be offered all of its source (app + core + build scripts). The repo
  is already public, so compliance is realistic, but it is irreversible — no
  future closed fork, no App Store build without the source offer attached.
  Decide this before writing code.
- CPU without JIT: sideloaded free-account builds have no
  dynamic-codesigning entitlement, so no JIT. PPSSPP's official position is
  that the IR caching interpreter runs nearly all PSP games at full speed on
  modern iOS anyway. Target `IRInterpreter`, not Dynarec; Dissidia is the
  acceptance test, measured on-device, before committing to the integration.
- Embedding: PPSSPP is a full application, not a library. It has an existing
  iOS frontend (`ios/` in-tree) — reuse its `Native*` app-layer entry points
  (`NativeInit`, `NativeShutdown`, `NativeRender`, `NativeUpdate`,
  `NativeGetAudio`) rather than inventing a new port. Wrap those in a
  `ppsspp_*` C ABI shaped like `poke_*`.
- Script story: PPSSPP has NO Lua. The Dissidia reader work in this repo
  drives PPSSPP's debugger from the PC side. In-app, the reader becomes a
  native adapter reading emulator state through the `Native*` layer + MIPS
  memory accessors — a port of the existing RE, not a reuse. Budget it like a
  new adapter, because it is one.
- Size/scope: PPSSPP is ~10x the melonDS core by source. Build time, archive
  size (~100+ MB objects), and CI minutes all grow. Simulator CI must cover it
  or it will rot.
- Risks: medium-high. License irreversibility, no-JIT perf to be proven per
  game, and the adapter is a port, not a carry-over.

## 3. Shared architecture changes (both cores)

1. `Core/` gains `mgba_shim.cpp` / `ppsspp_shim.cpp` exposing the `poke_*`
   shape (`*_create/load_rom/start/stop/set_button/framebuffer/speech_callback/
   game_id/adapter_*`). One C ABI per core, same Swift side.
2. `GameSession` routes by ROM kind (extension + header sniff, same place
   `ROMStore.allowedTypes` grows `.gba`, `.iso`, `.cso`): NDS -> melonDS,
   GBA -> mGBA, ISO -> PPSSPP. The UI above `GameSession` does not change.
3. `scripts/build-mgba.sh`, `scripts/build-ppsspp.sh` (+ sim variants),
   `Vendor/` archives per core per platform, same `llvm-ar` rules as today.
4. `isPokemonROM`-style gating generalizes: each core reports which script
   (if any) narrates the loaded game, and the script-button groups show only
   for those.
5. GPL notice + source-offer file ships in-app (Settings bundle) if PPSSPP
   lands.

## 4. Phased plan with rough sizes

- Phase A (mGBA, ~1-2 weeks): submodule + build scripts + shim + compat
  shim for pokemon-access GBA scripts + picker types + CI sim coverage.
  Shippable alone.
- Phase B (PPSSPP spike, ~1 week): IR-interpreter perf on Dissidia via the
  existing iOS frontend path, on-device fps measurement. GO/NO-GO gate.
- Phase C (PPSSPP integration, ~3-5 weeks): C ABI wrap + routing + Dissidia
  adapter port + GPL compliance + CI coverage.
- Phase D: per-game reader ports (FE-style) for whichever GBA/PSP titles
  matter, one adapter at a time.

## 5. Open questions for Devin

1. PPSSPP's GPL-2.0+ covers the whole distributed app — acceptable?
2. Which GBA titles first (determines which pokemon-access scripts get shims)?
3. Dissidia acceptability bar: full speed with narration, or playable first?
4. Keep three per-core archives (slower CI, bigger app) or split into
   per-core app flavors later?
