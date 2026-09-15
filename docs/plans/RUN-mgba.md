# How to run the Pokémon Access readers in mGBA

Status: **the shim and bootstrap are written, tested against a stubbed host, and verified to
boot the real v3.1.0 reader set. Nothing has run against a real game yet.** This is card A5
in `docs/plans/gb-gba-mgba-board.md`.

---

## The reader tree — use the REAL one

```
%USERPROFILE%\Dropbox\programs\pokemon-access\lua\      <- Pokemon Access v3.1.0
```

⛔ **Not `%LOCALAPPDATA%\Temp\pokemon-access-gb\`.** That was a partial copy in a directory
Windows clears. The Dropbox tree is the source of truth (see `READER-SOURCE.md`). The four
loader files have already been copied there:

```
oga_bootstrap.lua  oga_pure.lua  mgba_compat.lua  host-sim.lua
```

---

## What is needed

- **mGBA 0.10.5** — `%USERPROFILE%\scoop\apps\mgba\current\mGBA.exe`
- A **Pokémon save you own**, or a fresh game to walk around in
- The reader tree above, with the four loader files present

⚠️ **mGBA's Lua has no command-line entry point.** Neither `mGBA.exe` nor `mgba-sdl.exe`
accepts `--script`; scripting is reached through **Tools → Scripting**. This cannot be
automated from a shell, which is why it is the one step left for a human.

---

## Steps

1. Open mGBA and load a Pokémon ROM.

2. **Tools → Scripting…**, then load **`oga_bootstrap.lua`**.

   Nothing else. The bootstrap installs the host shim, stubs the FFI/LuaJIT surfaces,
   restores `module()`, bypasses the native `audio.dll` load, and loads `pokemon.lua`
   itself.

3. Watch the scripting console.

---

## What success looks like

Before any game is even considered:

```
[oga-boot] pure-Lua crc32 + encoding installed
[oga-shim] shim installed; host platform = 1
[oga-boot] platform: 1
[oga-boot] loading reader: ...\pokemon-access\lua\pokemon.lua
[oga-boot] native library requested: kernel32 (stubbed — no FFI in mGBA)
[oga-boot] audio.dll load bypassed
[oga-shim] <something about the game>
```

That last line is the real result. **`game_not_supported` would mean the reader ran but
could not identify the ROM** — which is a different, diagnosable problem from a crash.

## What to send back

The **full scripting console text**, worked or not. In particular:

- any `[oga-boot] !!` line — carries the position of a failure
- any `attempt to index a nil value` — names the missing shim function
- any `native library requested:` line — an FFI dependency the stub did not cover
- **whether mGBA's window froze** — see below

A failure is a useful result; it names the next thing to fix.

---

## The three things most likely to go wrong

**1. mGBA's window may freeze.** The reader ends in:

```lua
while true do
  emu.frameadvance()
  main_loop()
end
```

It **owns the main loop** — that is BizHawk's model. mGBA runs scripts on the main thread,
and its changelog notes *"Qt: Disable sync while running scripts from main thread."* If the
window stops responding, this loop is why. The fix belongs in the **shim** (turn
`frameadvance` into a frame callback) rather than in the reader. Deliberately not done
pre-emptively — mGBA may handle it fine.

**2. Footsteps may not announce.** The readers detect footsteps with
`memory.registerexec`, an exec hook mGBA does not have. The shim emulates it by polling the
PC once per frame. A routine that runs and returns inside one frame is **missed** by a frame
poll, and footsteps are the core "walk and hear what is ahead" feature. Silence while
walking means this. The fix is to poll the observable effect (position change) instead of
the PC.

**3. Speech is console text for now.** Tolk is stubbed, so reader output goes to the mGBA
console rather than to NVDA. That is intended at this stage — the Open Game Access layer
(Phase C) is what turns text into real speech. The readers are unchanged; only the delivery
changed.

---

## Already verified — do not re-check these

Proven by execution, not by reading code:

- the reader **boots** to its main loop against a stubbed host (`host-sim.lua`) — including
  against the real v3.1.0 tree
- `readbyterange` returns a correct **1-based table** (31 checks, including the
  `gb.lua` row/column pattern and preservation of the `0xED`/`0xEE` menu markers)
- sign extension, register-name mapping, and little-endian `readword` are correct
- `crc32` matches published CRC-32 vectors **including table input**

**Not proven:** any address or length against real memory; footstep detection; whether the
speech is *meaningful*.

---

## Strong test once it works in mGBA

v3.1.0 in **VBA is the known-good reference.** Running the same save in both VBA and mGBA
and diffing the spoken output is the strongest available fidelity check — far stronger than
"it produced some text". Tracked as card A6.
