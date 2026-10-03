# Open Game Access — iOS port: what was done, what is verified, and how to test it

Last updated: 2026-09-13.

## What this app is

The iOS side of the Pokémon Access mobile project. It runs Pokémon Black/White
(and other NDS games) through the melonDS core with Ola's `main.lua`
accessibility script, which reads the game's own memory and narrates it. The app
is VoiceOver-first: every DS button and every reading command is a labelled,
focusable element, and all speech goes through the app's own speech engine.

## Where the Android findings landed

The Android app (`me.magnum.melonds.dev` in
`%LOCALAPPDATA%\Temp\pokemon-a11y-app\native\melonDS-android`) is the working
reference. Its findings were diffed against the iOS tree, not assumed:

| Finding (from Android) | Status on iOS |
|---|---|
| `CartRetailIR::SPITransmitReceive` returns an **uninitialised** `u8` for any IR command outside `{0x00, 0x08}`, stalling every IR cart (B/W, HG/SS) at boot while non-IR carts work | **Was missing — now applied.** `scripts/apply-android-findings.sh` |
| Memory API must **rebase the guest address** by the domain `start` before the bounds check, or every read above 4 MiB silently returns 0 | Already correct in the iOS core (`off = address - domain.start`), verified in `pokecore.cpp` |
| DS keys are **active low** and `SetKeyMask()` does not invert | Already correct (`released = 0x00000FFF`, clear a bit per pressed button) |
| `Reset()` before *and after* cart insertion; seed PowerMan + RTC; explicitly disable the JIT | Already correct |
| Script is loaded as **shim + main.lua in ONE chunk**, shim first, with a `\n` separator | Already correct, and the bundled `main.lua` hashes equal to the original |
| `speech.stop` must reach the platform | **Was missing — now fixed.** `LuaStopSpeech` was a no-op, so the script's R key and the Stop speech button did nothing. |

A full file-by-file diff of the two cores shows **three** differences in total:
`CartRetailIR.cpp` (the fix above), a GLES3 compat header present only on Android,
and an 8-line `PlatformOGL.h` delta for the OpenGL renderer that iOS deliberately
does not build.

Two Android behaviours were deliberately *not* copied:

- The Android app boots with **FreeBIOS + generated firmware → direct boot**, which
  is what the iOS app already does. Booting the iOS core with real `bios7/bios9/
  firmware.bin` was tried and hangs in the BIOS itself (>20,000 frames, ARM9 parked
  in BIOS space, flat framebuffer), so real firmware is not a better path here.
- The Android `melonds_bridge.cpp` and its `index.html` in the app skeleton are the
  first-draft JNI/WebView layer, superseded by `PokeScript.cpp` +
  `AccessibilityScript.kt` on Android and by `poke_script` + SwiftUI on iOS. They
  are not part of the working app and were not ported.

## How the port is tested on this machine

**An iOS Simulator cannot run on Windows or Linux.** The simulator is a macOS-only
userland: `CoreSimulatorService` + `simulator-trampoline` + `launchd_sim` plus an
iOS runtime sysroot that ships only inside Xcode, and `simctl` is a macOS binary.
xtool's own simulator support is `#if os(macOS)`-gated for the same reason.

So "test it in a simulated iPhone" is met in two halves:

1. **A real iOS-Simulator app is built** — `scripts/build-sim.sh` compiles the whole
   core for `arm64-apple-ios17.0-simulator` against the iPhoneSimulator SDK, and
   `xtool dev build --triple arm64-apple-ios-simulator` links the SwiftUI app
   against it. The result carries `LC_BUILD_VERSION platform iossimulator` (checked:
   the device build says `platform ios`), `CFBundleSupportedPlatforms =
   iPhoneSimulator` and `DTPlatformName = iphonesimulator`. It installs with
   `xcrun simctl install booted OpenGameAccess.app`.
2. **The app's behaviour is exercised on the host** — `scripts/sim-test.sh` and
   `scripts/sim-play.sh` drive the *identical* core, C ABI, bundled script and
   once-per-frame pacing from a scripted player, and capture every spoken line.
   This is the same technique that found the four real Android bugs.

Scripts (all run through `wsl.sh <name>`, which mirrors the Windows source tree into
WSL first so a script can never test a stale copy):

| Script | What it does |
|---|---|
| `build-host.sh` | host object set from the shared source list |
| `build-core.sh` | device core archive (`Vendor/libpokecore.a`) |
| `build-sim.sh` | simulator core archive (`Vendor/sim/libpokecore-sim.a`) |
| `build-sim-app.sh` | simulator core + app, then packages it |
| `package-sim-app.sh` | assembles the `.app` (`simctl`-ready) and the zip |
| `sim-test.sh` / `sim-play.sh` | scripted playthrough with the real script |
| `black-check.sh` | focused Black run with the full speech trail |
| `regression.sh` | Black + Diamond, so a fix that repairs one cart cannot break the other |
| `apply-android-findings.sh` | the core-level Android fixes (idempotent) |

## Verified (measured, not assumed)

- **The core boots Black through the real accessibility script and narrates game
  content.** Captured lines: `Pokémon Black Version. Developed by GAME FREAK inc.
  Press Start`, `New Game, 1 of 4`, the full intro text, and the script's own
  `[ctx]` context trail.
- **Rendering is live**: `VRAMCNT_A = 83`, the top screen carries 61 distinct
  colours and the bottom 2, and both change over time.
- **The script runs byte-identically**: the bundled `main.lua` sha256
  (`abb7378…649c`) equals Devin's original in `Dropbox/Games/NDS/Lua/main.lua`.
- **The simulator build is a valid simulator app**: platform `iossimulator`, all
  2,648 melonDS symbols and all 24 `poke_*` entry points linked in, both Lua
  resources present and hashing equal to the originals.
- **Speech can be stopped again**: the core now emits the stop signal for the
  script's R key (observed as `[SPEAK] (stop)`), and the speech engine latches
  silence so the script's per-frame narration cannot immediately talk over it.

## Not verified here

- **Nothing has been heard.** Speech is confirmed at the callback boundary — the
  app's own `poke_set_speech_callback` — not through a speaker. AVSpeechSynthesizer
  and VoiceOver behaviour is untested by construction.
- **No UI has been seen.** SwiftUI layout, VoiceOver focus order and the control
  labels are code-reviewed only.
- **The simulator app has not been launched** (no macOS host here). It has been
  built, packaged and structurally validated only.
- In-game narration beyond the intro: the scripted player reaches the title screen
  and the intro, and the reading hotkeys are delivered, but no save file exists here
  so a full overworld run is not exercised. **Dropping a Black `.sav` next to the ROM
  makes this cheap** — the script reads the save on cart insert.

## Testing on the iPhone

When the phone is attached:

```bash
wsl.sh deploy          # mirror, rebuild the core, build + sign + install + launch
```

Requires the Apple mux port-forward to be up (elevated
`bin\setup-usbmux-forward.cmd`) and the phone unlocked. The core archive must be
rebuilt because `CartRetailIR.cpp` changed — `deploy.sh` handles that.

On the phone, the things to check by ear and by swiping:

1. The app speaks on start (it announces its own state).
2. Select a ROM, Start Game, and the loader line is heard
   (`Pokemon black accessibility loader running.`).
3. Swipe through the controls: d-pad, A/B/X/Y, Start/Select/L/R, and the Reading
   commands group. Every one should announce a name and a hint.
4. Play into the game and use **Where am I** (C) and the reading keys.
5. **Stop speech** must actually stop it, and stay stopped until the next command.
