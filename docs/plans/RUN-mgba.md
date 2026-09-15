# How to run the Pokémon Access readers in mGBA

Status: **the shim and bootstrap are written and compile; nothing has been run yet.**
This is card A5 in `docs/plans/gb-gba-mgba-board.md`.

---

## What is needed

- **mGBA 0.10.5** — installed at `%USERPROFILE%\scoop\apps\mgba\current\mGBA.exe`
- A **Pokémon save** you already own, or a fresh game to walk around in
- The reader set at `%LOCALAPPDATA%\Temp\pokemon-access-gb\` (166 Lua files)
- Two files from this repo:
  - `tools/re/platforms/gba/oga_bootstrap.lua`
  - `tools/re/platforms/gba/mgba_compat.lua`

⚠️ **mGBA's Lua has no command-line entry point.** Neither `mGBA.exe` nor `mgba-sdl.exe`
accepts `--script`; scripting is reached through **Tools → Scripting**. This step cannot be
automated from the shell, which is why it is the one thing left for a human to trigger.

---

## Steps

1. Copy both `.lua` files next to the reader set, so the bootstrap can find its sibling
   shim and the readers:

   ```
   copy open-game-access\tools\re\platforms\gba\oga_bootstrap.lua  %LOCALAPPDATA%\Temp\pokemon-access-gb\
   copy open-game-access\tools\re\platforms\gba\mgba_compat.lua    %LOCALAPPDATA%\Temp\pokemon-access-gb\
   ```

   (`oga_bootstrap.lua` loads `mgba_compat.lua` and `pokemon.lua` from its own directory,
   so all three must sit together. Set `OGA_READER_DIR` first if you would rather keep
   them apart.)

2. Open mGBA and load the Pokémon ROM.

3. **Tools → Scripting…**, then load **`oga_bootstrap.lua`**. Nothing else — the bootstrap
   installs the shim, stubs the FFI surfaces, and loads the reader itself.

4. Watch the scripting console. Expected output:

   ```
   [oga-boot] platform: GB
   [oga-boot] loading reader: ...\pokemon-access-gb\pokemon.lua
   [oga-boot] bootstrap complete
   ```

   Then the reader's own lines, which are the actual result.

---

## What counts as success

Not "it loaded". Success is:

1. the bootstrap reaches `bootstrap complete`, **and**
2. the reader emits a real line about the game state.

## What to send back

The **contents of the scripting console**, whether or not it worked. In particular:

- any `[oga-boot] !!` line — these carry the position of a failure
- any `attempt to index a nil value` — that is a missing shim function, and the name in
  the message says which one
- any `native library requested:` line — that is an FFI dependency the stub did not cover

A failure here is a useful result: it names the next shim function to implement.

---

## The riskiest parts, so you know what to look for

**1. Footsteps.** The readers detect footsteps via `memory.registerexec`, an exec hook mGBA
does not have. The shim emulates it by polling the PC once per frame. A routine that runs
and returns within a single frame can be MISSED by a frame poll, and footsteps are the core
"walk around and hear what is ahead" feature. If walking produces no announcements, that is
this, and the fix is to poll the observable effect (position change) rather than the PC.

**2. `readbyterange`.** mGBA's `emu:readRange` returns a *string*; the readers index the
result as a 1-based table (`raw_text[i+j]`). Indexing a Lua string numerically yields
`nil`, so a wrong conversion here produces **silently empty text** rather than an error.
The shim converts explicitly, but this is the mapping most likely to be subtly wrong.

**3. The FFI stub is a stub.** `tolk` is replaced rather than emulated, so speech goes to
the mGBA console. That is intended for this stage — the Open Game Access layer (Phase C) is
what turns text into real speech.
