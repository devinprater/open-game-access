# Running the reader in mGBA — the exact procedure

## Where the files live

| What | Where |
|---|---|
| mGBA loader files | `Dropbox\programs\pokemon-access\lua\` — **must sit beside the readers** |
| The readers (v3.1.0) | `Dropbox\programs\pokemon-access\lua\` |
| ROMs | `Dropbox\Games\GBA\` (GB, GBC and GBA all live here) |
| VBA reference (known-good) | `Dropbox\programs\pokemon-access\vba.exe` |

⛔ **The loader MUST live in `lua\`, beside the readers. There is no separate directory.**
I tried moving it into a clean `mgba\` subdirectory and **it broke the reader**:

```
[oga-shim] data_not_found
[oga-boot] !! reader raised: ./gba.lua:2711: table index is nil
```

The reader resolves its own data (`game/`, `message/`, `sounds/`) relative to **its own
location**, and those subtrees exist only under `lua\`. Separating the loader changes that
resolution and the reader loads no game data. Copying the readers into a second directory
does not fix it either — the subtrees have to come too.

So: loader in `lua\`, and **do not "tidy" it elsewhere**. The cost of a mixed directory is
a little clutter; the cost of separating it is a reader that does not work. I caused this
regression by cleaning up something that was not broken — an earlier note of mine claimed
`luaDir` made VBA auto-run whatever sits in `lua\`, which is **false** (see below), so the
"hazard" I was removing did not exist.

## The load path, verified from the readme

`readme.txt` line 39 is explicit:

> Once the rom is loaded, load the lua script (tools, lua, New Lua script window). From
> there, load **pokemon.lua**, press run.

So the entry point is **`pokemon.lua`**, loaded by hand. ⚠️ **Correction to an earlier note
of mine:** I recorded that `luaDir` in `vba.ini` makes VBA auto-run the reader. It does not
— `luaDir` is only a saved folder for the file dialog, and the script is started manually.
The distinction matters: it means stray files in `lua\` are *clutter*, not a live hazard.

## mGBA: the steps

1. Launch mGBA (`mgba` from a shell, or the Start-menu entry).
2. **File → Load ROM…** → a ROM from `Dropbox\Games\GBA\`. Load a **save** too, if you have
   one — `File → Load State` or let the battery save load automatically.
3. **Tools → Scripting…**
4. Click **Load script…** and choose:
   `Dropbox\programs\pokemon-access\lua\oga_bootstrap.lua`
5. The console pane should show the boot sequence, ending in `[oga-shim] Ready` and then
   speech.

## What you should see on a good run

```
[oga-boot] pure-Lua `bit` library installed
[oga-boot] `audio` cue stub installed (42 call sites; real playback needs a host sink)
[oga-boot] pure-Lua crc32 + encoding installed
[oga-shim] shim installed; host platform = 0
[oga-boot] platform: 0
[oga-boot] loading reader: ./pokemon.lua
[oga-boot] native library requested: kernel32 (stubbed — no FFI in mGBA)
[oga-boot] native library requested: win-controls (stubbed — no FFI in mGBA)
[oga-boot] audio.dll load bypassed — using the Lua cue stub instead (see oga_audio.lua)
[oga-shim] Ready
```

Every one of those lines has been reproduced outside mGBA against real ROM bytes, so a
difference here is informative.

## Troubleshooting — by symptom

| Symptom | Likely cause |
|---|---|
| **The window freezes / stops responding** | The reader owns `while true do emu.frameadvance() … end` — BizHawk's model, where the script drives emulation. If mGBA's scripting runs on the UI thread, this blocks it. **The fix is in the shim**: convert the loop to a frame callback. This is the single most likely failure. |
| Console shows `game_not_supported` | Check the ROM is in v3.1.0's list: Red/Blue/Yellow, Gold/Silver/Crystal, FireRed/LeafGreen, Emerald. Ruby/Sapphire and anything else are correctly rejected. |
| Console stops after `audio.dll load bypassed` with no `Ready` | Identification runs long. The GB path needs ~60,001 frames because `main_loop` reads the full 360-byte screen every frame. Give it time before concluding it is stuck. |
| Speech is silent but the console reaches `Ready` | `tolk` is stubbed — there is no speech sink in mGBA yet. **Getting one is the point of this run.** |
| Speech is audible but the text is *wrong* | That is a memory-map question, and the interesting result. Compare with VBA on the same save. |

## The comparison that actually settles it

Run **VBA v3.1.0** with `pokemon.lua` on the *same save*, and diff what it speaks against
what mGBA speaks. VBA is the known-good reference. Matching output means the shim is
faithful; differing output points at a specific wrong address or length.

## What is NOT verified

No address, offset or length has been checked against live RAM. The harness returns zeros
for RAM, so everything that depends on *reading game state* is unproven — identification
(which reads the cartridge header) is the only part exercised end-to-end. See `VERIFIED.md`
in `tools/re/platforms/gba/` for the full ledger.
