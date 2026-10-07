# Open Game Access v0.6.2 — reader assets single-sourced; no functional change

**Correction: this release's first version stated a defect that does not exist.** The
notes below are rewritten from measurement. Nothing here changes what the app does.

## What the previous notes claimed, and why it was wrong

They said the Android Game Boy / GBA reader set was "five days stale" and shipped
"without its bootstrap and without its API shim", breaking the GBA path on Android.

**That was wrong.** The two platforms install the reader environment differently, and
I read Android's from iOS's architecture instead of from Android's own loader:

- **iOS** loads `oga_bootstrap.lua`, which installs `mgba_compat.lua` in Lua.
- **Android** loads **only `pokemon.lua`** (`GbAccessibilityScript.readScript()` opens
  exactly that one file), and `MGBACore.cpp` installs the equivalent surface **in C** —
  `print, memory, emu, tolk, audio, controls, bit, input, unpack, module, scriptpath,
  loadfile, require, console, gui, savestate`. `require "crc32"`, `"encoding"`,
  `"win-controls"` and `"tolk"` are stubbed there too.

`oga_bootstrap.lua` and `mgba_compat.lua` are **never loaded on Android at all**, so
their absence from the APK was correct, not a defect.

Measured against the shipped v0.6.1 APK: `pokemon.lua`, `gb.lua`, `gba.lua`, all 150
`game/` files, all 6 `message/` files and all 33 sounds were **already present**. The
old Android tree and the canonical tree were **195 files, identical, zero differing**.

## What this release actually contains

- **One canonical reader tree.** The tracked duplicate at
  `app/src/main/assets/lua/gb/` (195 files) is deleted; the Android overlay stages
  `assets/lua/gb/` from `Sources/OpenGameAccess/Resources/gba-lua/` at build time.
  This removes the drift RISK, not an existing break.
- **`sounds/` (33 WAVs) moved into the canonical tree.** They previously existed only
  in the Android copy, so the **iOS bundle never carried them**. It does now.
  For Android this is a no-op — it already shipped them and never loaded the extra
  files.
- **Gates.** `scripts/check-reader-assets.sh` fails if a second reader tree reappears,
  and asserts the files and sounds the readers name. The APK gate now asserts specific
  filenames instead of `grep -qi lua`, which matched 197 entries and would have passed
  with the path absent.
- **The Android workflow runs on `main`**, filtered to the paths that can break the app.
  It previously ran only on a tag, so a broken reader set could land unbuilt.

**No app behaviour changes on either platform.** This is a maintenance release.

## The `joypad.set` item is withdrawn

The earlier notes described Android's `bizhawk_compat.lua` (NDS path) as having an
inert `joypad.set` that leaks a held direction into the game. **Not proven, and
probably false.** The Android NDS host registers only `HeldKeys, GetJoy, NDSTapDown,
NDSTapUp, Keys` and has **no button-override path at all** — `setJoypadState` is
defined but never called. `main.lua` calls `joypad.set({})` to clear overrides; with no
override mechanism to clear, an empty function is consistent rather than broken.
Implementing `input.JoySet` there would store overrides nothing consumes.

## A real gap this did surface

`oga_audio.lua` is a **recording stub** — it captures `path/pan/volume` and returns
without playing. There is **no host sound callback on iOS**, and the reader names the
WAV files directly (`scriptpath .. "sounds\\gba\\s_grass.wav"`). So positional audio
is silent **on every platform**, and always has been. Bundling the 33 WAVs for iOS does
not change that; it only means the files are finally where the reader looks for them.

Closing it means a host-side sound sink that plays those WAVs with pan applied — which
the Android bridge already does via `setSoundCallback`, and iOS does not have.
