# Open Game Access v0.6.3 — positional sound cues on iOS

## What this fixes

Pokémon Access conveys **direction with sound**. 42 `audio.play(...)` sites in the
reader set pan a WAV to an obstacle's side — `gb.lua` pans a boulder's sound to where
the boulder is, and pans the menu-select blip by which row is selected. The pan *is*
the information; it is the same content as the spoken line, encoded in space.

**On iOS those cues were silent, always.** `oga_audio.lua` was a recording stub: it
accepted a `set_sink()` handoff that nothing ever called, so every cue was captured and
dropped. The 33 WAV files shipped; nothing played them.

This release wires the host side:

- The C core now exposes the reader's cue path — `LuaPlaySound` →
  `_G.oga_play_sound` → `poke_set_sound_callback` → `GbaSoundForward`.
- `CuePlayer.swift` plays the reader's own WAV with the pan applied, under the
  settings' existing **Game sound** toggle.

## Platform scope — iOS only

Android is **unchanged**. It already had a complete cue path (`LuaAudioPlay` in
`MGBACore.cpp` → `playSound` → Kotlin `SoundPool`, with pan applied), landed in
September. The Android host installs its own `audio` table in C and does not load
`oga_audio.lua` at all, so this release changes nothing there — not a fix, not a
regression. The 33 WAVs were already in the APK.

## Details worth knowing

- **Cues are separate from the emulator's audio graph, on purpose.** The reader's files
  are mixed format (mono/stereo, 8- and 16-bit, 44100 Hz); the engine's own format is
  32768 Hz int16. Decoding into the realtime render graph would mean allocating on the
  audio thread, so cues use the platform's own player (one reused player per file) and
  mix at the OS level. That is also what lets a cue sound over the game without stopping it.
- **A cue never aborts the reader.** Out-of-range or nil pan values are clamped, and a
  failed decode or a refused play is logged once and skipped — cosmetic failure is not
  worth a dead frame loop.
- **New gate:** `scripts/cue-sink-test.sh` drives the shipped `oga_audio.lua` with a
  stand-in host sink and asserts each cue arrives with its pan intact, then mutates the
  Lua four ways to prove the check can actually fail. It runs in CI.

## Verification

- IPA: `platform ios`, unsigned (SideStore can re-sign it), 33 cue WAVs and the
  `oga_play_sound` binding present in the shipped binary.
- The IPA's own bundled `oga_audio.lua` was driven directly and delivers cues with
  correct left/right pans.

## Honest limit

**No phone has heard a cue yet.** The seam is proven on the host — cues leave the reader
with correct pans, and the shipped bundle's copy does the same — but audibility over real
game audio on a device is exactly what this release is for. Please report what you hear,
including "nothing".
