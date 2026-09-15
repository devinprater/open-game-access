# Running the reader in mGBA — the working procedure

**Status: VERIFIED working on real mGBA.** The unmodified v3.1.0 reader boots, identifies the
cartridge, says `Ready`, and runs its main loop — measured, not assumed.

## What changed: install mGBA **dev (0.11)**, not 0.10.5

```bash
scoop install mgba-dev
```

⛔ **This is not optional.** mGBA 0.10.5 cannot run this reader headlessly, and the reason is a
single missing flag. `mgba-dev` provides:

| Feature | Why it matters here |
|---|---|
| **`--script FILE`** | Runs a script on start. Without it the reader can only be launched by hand from Tools → Scripting, which blocks all automated verification. |
| Scripting in **0.11** | Also adds `setBreakpoint`/`setRangeWatchpoint` (real execution hooks) if the frame-poll approximation ever needs replacing. |

`mgba-dev` is a portable install: it ships `portable.ini` and its settings live in
`qt.ini` next to the executable.

## The exact command

```bash
MGBA="C:/Users/Devin Prater/scoop/apps/mgba-dev/current/mgba.exe"
"$MGBA" --script "<reader-dir>/oga_bootstrap.lua" "<rom>"
```

The bootstrap resolves everything else relative to **its own directory** (`scriptpath`), so the
loader files must sit **beside the readers in `lua\`** — see the note further down.

## Where the files live

| What | Where |
|---|---|
| Loader + shim | `Dropbox\programs\pokemon-access\lua\` — **must sit beside the readers** |
| The readers (v3.1.0) | same directory |
| ROMs | `Dropbox\Games\GBA\` (GB, GBC and GBA all live here) |
| VBA reference (known-good) | `Dropbox\programs\pokemon-access\vba.exe` |

⛔ **The loader MUST live in `lua\`, beside the readers.** Moving it to a clean subdirectory
**breaks the reader**:

```
[oga-shim] data_not_found
!! reader raised: ./gba.lua:2711: table index is nil
```

The reader resolves `game/`, `message/` and `sounds/` relative to its own location, and those
subtrees exist only under `lua\`. The cost of a mixed directory is clutter; the cost of
separating it is a dead reader.

## The load path, from the readme

`readme.txt` line 39: *"Once the rom is loaded, load the lua script (tools, lua, New Lua script
window). From there, load **pokemon.lua**, press run."*

⚠️ **Correction to an earlier note:** `luaDir` in `vba.ini` does **not** auto-run anything — it
is only where VBA's file dialog opens. Stray files in `lua\` are clutter, not a hazard.

## What a good run looks like

```
[oga-boot] pure-Lua `bit` library installed
[oga-boot] `audio` cue stub installed (42 call sites; real playback needs a host sink)
[oga-boot] pure-Lua crc32 + encoding installed
[oga-shim] shim installed; host platform = 0
[oga-boot] platform: 0
[oga-boot] loading reader: .../lua/pokemon.lua
[oga-boot] native library requested: kernel32 (stubbed — no FFI in mGBA)
[oga-boot] native library requested: win-controls (stubbed — no FFI in mGBA)
[oga-boot] audio.dll load bypassed — using the Lua cue stub instead (see oga_audio.lua)
[oga-shim] Ready                      <-- identification succeeded, and this line is SPOKEN
```

Then the reader enters its main loop and never returns — expected, since it owns the loop.

## Capturing what it says

Speech goes to the global `oga_say`. Install a sink **before** loading the bootstrap:

```lua
_G.oga_say = function(text, interrupt) ... end
dofile("<reader-dir>/oga_bootstrap.lua")
```

The bootstrap preserves a pre-installed sink across the shim load (the shim defines its own
`oga_say` and would otherwise overwrite it). Measured output:

```
Ready
```

## Troubleshooting — by symptom

| Symptom | Cause |
|---|---|
| `attempt to index a nil value (global 'write_hooks')` / `Invalid key` / `Function called from invalid context` | The shim was written for a stub host. See `VERIFIED.md` — `emu`, `input`, `callbacks`, `console` are mGBA **userdata**, and modifying or reassigning them fails. |
| `message.lua:4: attempt to concatenate a nil value (global 'scriptpath')` | The reader chunk was loaded with a placeholder chunk name. Pass the real entry path so `debug.getinfo` yields `@<path>`. |
| Reader says `game_not_supported` | Check the ROM is in v3.1.0's list: Red/Blue/Yellow, Gold/Silver/Crystal, FireRed/LeafGreen, Emerald. Ruby/Sapphire are correctly rejected. |
| The Qt window stops repainting | Expected. The reader owns `while true do emu.frameadvance(); main_loop() end`, so the script never yields to the event loop. |
| Silent, but the log reaches `Ready` | No speech sink installed — that is the integration point, not a bug. |

## What is NOT verified

No address, offset or length has been checked against live game state beyond what booting and
identifying requires. Whether footsteps fire, whether the speech is *meaningful*, and whether
all reader features work need a real play session. See `VERIFIED.md`.
