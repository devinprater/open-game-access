# Open Game Access v0.6.2 — the Android Game Boy reader set actually ships

**The v0.6.1 Android APK claimed to support Game Boy and GBA. On Android it could not
work.** This release makes that true, and fixes the reason nothing caught it.

## What was wrong

The Game Boy / GBA reader set existed **twice** in this repo. iOS stages the canonical
tree wholesale (`Package.swift` `.copy("Resources/gba-lua")`), so it always carried
everything. Android had a second, hand-copied tree checked in at
`app/src/main/assets/lua/gb/` — and nothing regenerated it.

By the time anyone looked, that copy was **five days stale** and missing the two files
that make the path work at all:

- **`oga_bootstrap.lua`** — the loader, which by its own header is "the ONE file to
  load".
- **`mgba_compat.lua`** — the mGBA ↔ BizHawk API shim the readers depend on.

Android's Kotlin points the core straight at `lua/gb`, so the GBA path shipped to the
phone without its bootstrap and without its API shim. **iOS was never affected.**

This is the fourth time this project has shipped something *linked* but not
*reachable*, and it is the same shape as v0.6.0-nes: the feature announced, the piece
that makes it work absent, and every check green.

## Why nothing caught it

The CI gate for the APK's reader assets was:

```sh
grep -qi lua "$RUNNER_TEMP/apk-list.txt"
```

Measured against the shipped v0.6.1 APK, that matched **197 entries** and would have
passed with the Game Boy path entirely absent: `main.lua` (the DS reader), any vintage
of the reader directory, and the emulator core's own Lua engine all satisfy it.

And the Android workflow **did not run on `main` at all** — only on `push: tags: ['v*']`
and manual dispatch. So the drift could land, and did, with the APK never rebuilt.

## What this release fixes

- **One canonical reader set.** `Sources/OpenGameAccess/Resources/gba-lua/` is the only
  tree. The Android overlay stages it into `assets/lua/gb/` at build time — the same way
  it already stages the Java and C++ overlay — excluding the two host development tools
  (`host-sim-rom.lua`, which asserts its own loadfile paths, and `oga_capture.lua`, which
  writes to hardcoded Windows temp paths).
- **The positional-audio sounds are now canonical.** `sounds/` — 33 WAVs the readers name
  directly (`scriptpath .. "sounds\\gba\\s_grass.wav"`) — existed **only** in the stale
  Android copy. iOS had never carried them, because the canonical tree had no `sounds/`
  at all. They are now part of the single tree.
- **A gate that can fail.** `scripts/check-reader-assets.sh` asserts the files a reader
  set cannot work without, the sounds the readers name, and **fails if a second reader
  tree ever reappears**. It runs in the Android workflow *before* the build. The
  post-build gate now asserts the same specific filenames instead of "some .lua file
  exists", and the local build script had the identical weak check, now fixed.
- **The Android workflow runs on `main`**, filtered to the paths that can actually break
  the app, so drift fails in minutes instead of shipping.

Verified by unpacking the built APK and reading its file list, not by the build log:
`oga_bootstrap.lua`, `mgba_compat.lua`, `gb.lua`, `gba.lua`, `pokemon.lua` and all 33
sounds are present; the two dev tools are correctly absent.

## Known issue, recorded rather than fixed

Android's `bizhawk_compat.lua` has an **inert `joypad.set`**. The canonical version calls
`input.JoySet()`; Android's is an empty function. `main.lua` calls `joypad.set({})` at 16
sites specifically to *clear* its overrides and re-latch from the physical pad, so on
Android **a direction held while the modifier trigger is released can leak through to the
game** as an unasked-for step.

It cannot simply be copied over: `input.JoySet` is registered only in
`Core/pokecore.cpp`, which Android does not link — the Android tree has no Lua `JoySet`
binding at all. Fixing it means exposing that binding in the Android Lua host first. Full
writeup in `docs/android-joypad-set.md`.

## Unchanged

NES readers remain **iOS only** — `nes-lua/` has no Android staging path, so the Android
APK does not carry them and this release does not claim otherwise.
