# Pokémon Access — Android build & integration notes (2026-09-09)

## Status: BUILT & VERIFIED
- APK: `app/build/outputs/apk/debug/app-debug.apk` (~6.6 MB), `BUILD SUCCESSFUL`
- JDK 17 Temurin 17.0.20.1+1 at `C:/Users/Devin Prater/AppData/Local/Java/jdk-17.0.20.1+1`
- Android SDK at `C:/Users/Devin Prater/Android/Sdk` (platform 34, build-tools 34.0.0, adb 1.0.41)
- `local.properties` points at the SDK; `gradle.properties` needs `android.useAndroidX=true`
  (build fails on appcompat deps without it) + `android.enableJetifier=true`.

## Core choice (verified via web research)
- **Core**: melonDS via the melonDS-android port (rafaelvcaetano), with the
  NPO-197/melonDS-lua fork patched in (PR #1671) for Lua scripting.
- melonDS-lua model: script runs once at load; a global `_Update()` is called
  per frame — unlike BizHawk's `while true + emu.frameadvance()` coroutine loop.

## Script adaptation (app/src/main/assets/lua/bizhawk_compat.lua)
Bridges Ola's pokemon-access main.lua (BizHawk API) to melonDS-lua without
editing main.lua itself:
- `mainmemory.read_u8/u16_le/u32_le(off)` -> `memory.read_*(0x02000000+off,"MainRAM")`
  (BizHawk offsets are relative to 0x02000000 = melonDS MainRAM base; 4 MiB domain).
- `mainmemory.read_bytes_as_array` rebuilt 0-indexed (script handles both indexings).
- `memory.read_u8(a,"ROM")` kept, wrapped in pcall with MainRAM fallback.
- `emu.frameadvance()` -> `coroutine.yield()`; runner resumes script coroutine from
  `_Update()` each frame — main.lua stays byte-identical.
- `emu.framecount()` -> local counter increment.
- `joypad.setfrommnemonicstr("|x,y,mic,0,<17btn>|")` -> parse and call
  `input.NDSTapDown(x,y)` / `input.NDSTapUp()` (Touch = 15th button char 'T').
- `joypad.set{}` blocking overrides -> no-op (not supported on melonDS-lua;
  script is nil-safe without it).
- `input.get()` -> `input.HeldKeys()` with Qt-keycode -> letter translation
  (0x52=R, 0x55=U, 0x4A=J, 0x4C=L, 0x49=I, 0x4F=O, 0x4E=N, 0x43=C, 0x50=P, 0x45=E).
- `console.writeline` -> `print`; `speech.say/stop` -> `hermes_tts` JS bridge
  (Android TextToSpeech via JavascriptInterface in MainActivity).

## Gotchas hit during build
- `WebResourceRequest` import mismatch: use the deprecated String-based
  `shouldOverrideUrlLoading(view, url)` override for compileKotlin to pass.
- Manifest must not reference `@mipmap/ic_launcher` unless icon resources exist
  (fresh skeleton lacks mipmap dirs) — removed the icon attribute.
- `gradle` is on PATH via scoop shim; JAVA_HOME/ANDROID_HOME set per-command.
- Build: `gradle :app:assembleDebug --no-daemon` ~52s; APK ~6.6 MB.
