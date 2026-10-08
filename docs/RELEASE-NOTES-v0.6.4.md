# Open Game Access v0.6.4 — the Game Boy reader, and the GBA hooks that make it speak

**The short version:** the Game Boy reader was never broken — it was being misdiagnosed — and the
GBA reader's menu and text hooks now work, because mGBA turned out to have real instruction-level
hooks that this app was not using. Two bugs are fixed, one piece of the GBA path is genuinely
repaired, and everything here is host-proven.

## What this fixes

### 1. The Game Boy reader re-read the whole game once per step, and said "Ready" 729 times

Pokémon Access registers `init_script` at the CPU's **entry vector** (0x100 on Game Boy, 0x8000000
on GBA) so the reader survives a soft reset. That is a **reset handler**. The BizHawk compatibility
shim had folded every registration into one table and fired that table whenever the player moved —
so `init_script` ran on **every step**. It calls `get_game()` → `load_game()` and ends by speaking
"Ready".

Measured on Pokémon Red, 40000 frames: **"Ready" was spoken 729 times**, and the whole reader was
rebuilt and re-announced while the player walked. The caller was identified by a traceback in the
speech sink, not inferred — `pokemon.lua:893` inside `init_script`, via the shim's movement poll.

Fixed: entry vectors are held separately and are never fired by the movement poll. Re-measured on
the same ROM: **"Ready" 729 → 1**, with the game's own dialogue untouched.

### 2. "The Game Boy reader says raw numbers and `nil`" was the wrong diagnosis

This had been recorded against **both** consoles. It is a **GBA title-screen** symptom only — the
GBA path registers about 38 map predicates that legitimately fail before a map exists.

Measured on Pokémon Red: the Game Boy path reads Oak's opening speech **line by line**, then the
naming screen, then the name it typed —

    Hello there! Welcome to the
    world of POKéMON!
    ...
    Right! So your name is AJR!
    AJR is playing the SNES! ...Okay!

— with **zero** `nil`, zero raw numbers and zero hook errors. The Game Boy path was working. Two
consoles had been described from one console's behaviour.

### 3. ⛔ THE GBA MENU AND TEXT HOOKS NOW RUN AT THE INSTRUCTION — this is the real repair

The GBA reader's text and menu hooks read **CPU registers** (`memory.getregister("r1")` and friends)
to find what the game was doing at a specific instruction. Measured across the reader set: **18 of
`gba.lua`'s 68 hook functions and 6 of `rse.lua`'s 19** work this way.

mGBA's *Lua* API has no exec hook, so the shim approximated them by firing on player movement — and
a register read one frame later holds unrelated data. In the game world the reader spoke garbage:
`49154:58718`, `4`, and blank lines where menu items belong.

**mGBA the emulator has real breakpoints, and they were already compiled into this app.** They never
fired because the frame driver called `runFrame()`, which does not check breakpoints — only the
debugger's `mDebuggerRunFrame()` does. The capability was in the build and was simply never driven.

Now wired: `memory.registerexec` installs a **real breakpoint**, and frames are driven through the
debugger when any hook exists (a game with no hooks keeps the cheaper path, so nothing else pays).
Same Emerald savestate, same input, before and after:

    before : "", "                    ", "4", "49154:58718", "À"
    after  : "BAG", "CLOSE BAG", "Return to the field."

**Real menu items where there was noise.** The readers themselves were not modified — the fix is in
the host layer, as it should be.

## Platform scope — iOS and Android, read per platform

The Game Boy reader and the shim are in the **shared canonical reader set**
(`Sources/OpenGameAccess/Resources/gba-lua/`), so both bugs are fixed for the reader itself on both
platforms.

- **iOS** gets the exec hooks: `Core/gba_core.cpp` is the iOS/GB host, and the debugger path is
  compiled in (`ENABLE_DEBUGGERS` is already in the device flags).
- **Android** embeds `MGBACore.cpp` with its own BizHawk surface in C and does **not** load
  `mgba_compat.lua`; its host has no equivalent of the new `oga_set_exec_hook` binding, so its
  `registerexec` path is unchanged — neither fixed nor broken by this release. The Game Boy
  reader's "Ready" fix does reach it, because that lives in the canonical Lua.

## Known gap, stated plainly

⚠️ **No iPhone or Android device has run any of the above.** Every claim in this file is
host-measured: real ROMs through the real app code path on the host, with the framebuffer captured
and looked at. The GBA work is additionally verified to be *inside the shipped binary* (the binding
name and the debugger frame driver are both present in the released `.ipa`).

Also still open, and not addressed here:

- The GBA intro still cannot be walked by the test harness with the reader loaded (it costs hours of
  wall clock). A committed helper works around that by walking the intro with a no-op script and
  capturing a savestate, then resuming it with the real reader —
  `scripts/gba-reach-world.sh`. No save file exists in the library and archive.org's Emerald items
  carry none, so this route is the reproducible one.
- Navigation and counted-guidance work for Chrono Trigger SNES is researched and written up
  (`docs/design/navigation-prior-art.md`) but not started.
- PPSSPP exec hooks were surveyed and **parked**: the IR interpreter this app runs does have real
  exec hooks, but what they would add is observability rather than speech, and the native Dissidia
  reader works without them. `docs/research/ppsspp-exec-hooks.md`.

## Verification

Everything below was run against this build's tree, on the host:

- `scripts/verify-ipa.sh` on the built IPA — platform `ios`, unsigned, both reader sets present by
  name, 33 cue WAVs, the cue binding, no game data.
- The exec-hook wiring in the **shipped binary** — `_gba_set_exec_hook`, `_gba_clear_exec_hook`,
  `_gba_hooks_active`, the `oga_set_exec_hook` / `oga_clear_exec_hook` binding names, and
  `mDebuggerRunFrame` all present.
- `scripts/registerexec-kind-test.sh` — reset hooks and movement hooks stay separate (3 sabotage
  mutations must fail).
- `scripts/gba-exec-hook-test.sh` — hooks are installed as real breakpoints, entry vectors are
  excluded, unregister clears them, and the core still clears the debugger's pause flag (without
  which the emulator hangs with no error). 3 sabotage mutations must fail.
- Pokémon Red end to end: 40000 frames, dialogue read line by line, "Ready" once.
- Emerald, resumed in the world: real menu items, zero hook errors.
- The full adapter suite and the path/shell/syntax gates.

## Install

Unsigned IPA — sideload and re-sign as usual. Android APK is attached for the same tag.
