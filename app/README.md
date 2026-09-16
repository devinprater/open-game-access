# Pokemon Access — accessible Pokémon Black/White player for Android

An Android app that lets blind players run Pokémon Black/White with Ola's
accessibility script (`main.lua`, the "pokemon-access" loader) through the
melonDS emulator core with Lua scripting enabled.

## Core choice and why

**melonDS with the melonDS-lua patch (NPO-197/melonDS-lua).** The script is a
BizHawk Lua script: it reads raw NDS Main RAM (`mainmemory.*`), writes
overrides and synthetic touch input (`joypad.*`), speaks through a `speech`
table, and loops with `emu.frameadvance()`. Of the NDS cores that run on
Android:

- **melonDS (melonDS-lua fork)** — has a Lua engine built for accessibility
  ("The aim ... is to support accessibility features and trackers"), exposes
  memory/input/joypad style APIs, and is the base of the actively maintained
  melonDS-android port, whose JNI layer (`MelonEmulator.kt`,
  `MelonDSAndroidJNI.cpp`, ROM file processors, savestates) this app wraps.
  This is the core this app uses.
- **DeSmuME** — has Lua but no maintained Android port with Lua enabled.
- **RetroArch melonDS/DeSmuME cores** — no Lua script support on Android.

## How the script runs unchanged

melonDS-lua calls a global `_Update()` once per frame; BizHawk scripts use
`while true do ... emu.frameadvance() end`. `app/src/main/assets/lua/bizhawk_compat.lua`
bridges the two:

- `emu.frameadvance()` becomes a coroutine yield; the runner resumes the
  script's coroutine from `_Update()` each frame — main.lua stays byte-identical.
- `mainmemory.read_*` offsets are relative to 0x02000000, which is exactly
  melonDS's MainRAM base; they map 1:1 to `memory.read_*(addr, "MainRAM")`.
- `memory.read_u8(a, "ROM")` (used for game-code detection, IRAO/IRBO) maps to
  the ROM domain.
- `joypad.setfrommnemonicstr("|x,y,mic,0,....T....|")` — the synthetic touch
  mechanism — maps to melonDS-lua's `input.NDSTapDown(x,y)` / `NDSTapUp()`.
- `speech.say/stop` map onto Android's TextToSpeech via a JavascriptInterface
  bridge injected as the global `hermes_tts`.
- `joypad.set{}` (the hold-to-block controller-mod layer) degrades to a no-op:
  melonDS-lua has no per-frame host-button override, and main.lua's uses are
  nil-safe without it.

## Building

1. JDK 17 + Android SDK (platform 34, build-tools 34.0.0) — already installed
   on this machine.
2. `bash scripts/setup-core.sh` — vendors melonDS-android and swaps its
   melonDS submodule for NPO-197/melonDS-lua, then copies main.lua into assets.
3. `./gradlew :app:assembleDebug` — APK at `app/build/outputs/apk/debug/`.

## Using

1. Open the app, tap **Select ROM**, pick a Pokémon Black or White `.nds`.
2. Tap **Start**. The core boots the ROM, then loads `main.lua` with the
   compat shim.
3. The script speaks one startup line ("Pokemon black accessibility loader
   running.") and then only game content — exactly as on BizHawk.

## Files

- `app/src/main/java/com/devin/pokemonaccess/MainActivity.kt` — UI + TTS bridge
- `app/src/main/java/com/devin/pokemonaccess/LuaScriptRunner.kt` — coroutine
  script runner
- `app/src/main/assets/lua/bizhawk_compat.lua` — the BizHawk→melonDS shim
- `scripts/setup-core.sh` — vendors the Lua-enabled core
- `settings.gradle.kts` / `build.gradle.kts` — Gradle wiring
