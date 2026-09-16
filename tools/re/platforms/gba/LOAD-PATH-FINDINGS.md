# What the readers actually need (measured, not assumed)

This is the load-path analysis for running the Pokémon Access GB/GBC/GBA reader set in
mGBA. Every item below was found by executing the reader against a stubbed host
(`host-sim.lua`), not by reading the code and predicting.

## The three runtime mismatches

### 1. LuaJIT → stock Lua 5.4

The readers are written for **BizHawk's LuaJIT**. mGBA embeds stock **Lua 5.4**. Four things
break, and only one of them is obvious:

| LuaJIT thing | Status in Lua 5.4 | Disposition |
|---|---|---|
| `require "ffi"` | no such module | stub registered in `package.loaded`/`preload` |
| `module("astar", package.seeall)` | **`module()` REMOVED in 5.2** | reimplemented in `oga_bootstrap.lua` |
| `ffi.load("audio.dll")` | impossible (native C module) | `package.loadlib` intercepted |
| `ffi.load("tolk")` | impossible | `tolk` stubbed and registered as a module |

⛔ **`module()` was the one that mattered most and was least expected.** `a-star.lua`
line 26 uses it, and `pokemon.lua` line 1 is `require "a-star"` — so the reader failed on
its **very first statement** with `attempt to call a nil value (global 'module')`. Only
`a-star.lua` uses it, so a reimplementation was enough; no reader edits were needed.

### 2. `emu.platform()` returns a NUMBER, not a string

```lua
function get_device()          -- pokemon.lua:897
  local id = emu.platform()
  if id == 0 then return "gba"
  elseif id == 1 then return "gb" end
  return nil
end
```

My first shim returned `"GB"`/`"GBA"` strings. That made `get_device()` return `nil`, and
the failure surfaced **hundreds of lines away** as:

```
attempt to concatenate a nil value (global 'device')
```

mGBA uses the same numeric values as BizHawk (0 = GBA, 1 = GB), so the shim now returns the
number unchanged. A string that "looks nicer" broke the reader.

### 3. The unguarded native `audio.dll` load

```lua
tolk = require "tolk"                                    -- pokemon.lua:1052
assert(package.loadlib("audio.dll", "luaopen_audio"))()  -- pokemon.lua:1053
```

Two problems in two lines, both at **top level before the main loop**, so either one aborts
the reader before it can speak:

- `require "tolk"` — needed module registration, not just a global.
- `audio.dll` — a native C module that is not in the reader set. Safe to bypass because
  **`audio.` is never referenced anywhere in 166 files** (verified by grep). It is loaded
  and then unused. `assert()` needs `loadlib` to return a *function*, so the stub returns a
  callable.

## The shim's method-capture bug

`emu` **is** mGBA's CoreAdapter — the host object itself. So:

```lua
local mgba_emu = HOST                     -- HOST == emu, the same table
emu.platform = function() return mgba_emu:platform() end
                                             -- ^ now calls emu.platform → infinite recursion
```

Every overwritten method called itself: `mgba_compat.lua:106: stack overflow`. Fixed by
capturing mGBA's methods **by value** into locals before overwriting anything. Found by
`host-sim.lua`, not by reading the code — this is why the harness exists.

## What `host-sim.lua` proves, and what it does not

**Proves** (by running the real reader against a stubbed host):

- the load path resolves: `require "a-star"`, `require "crc32"`, `ffi`, `tolk`
- `module()` is restored well enough for `a-star.lua` to load
- `audio.dll` no longer aborts the boot
- the reader identifies the platform (`device`), loads its game scripts, and enters
  `while true do emu.frameadvance(); main_loop() end`
- the speech path works: reader text reaches the console via the Tolk stub

**Does NOT prove** — no emulator, no ROM, no memory, reads return zero:

- any memory address or length is correct
- footstep detection (`registerexec` → frame-poll) actually fires
- `readbyterange`'s string→table conversion matches real data
- speech says anything *meaningful* about a game
- the reader's main loop cooperates with mGBA's threading

The last one is a live risk: mGBA runs scripts on the main thread, and its changelog notes
*"Qt: Disable sync while running scripts from main thread."* If mGBA's window freezes when
the script loads, the reader's blocking loop is why — and the fix belongs in the **shim**
(converting `frameadvance` into a frame callback), not in the reader.

## Result of the harness run

```
[oga-boot] pure-Lua crc32 + encoding installed
[oga-shim] shim installed; host platform = 1
[oga-boot] platform: 1
[oga-boot] loading reader: ./pokemon.lua
[oga-boot] native library requested: kernel32 (stubbed)
[oga-boot] native library requested: win-controls (stubbed)
[oga-boot] audio.dll load bypassed
[oga-shim] game_not_supported          <- REAL reader output
[oga-boot] !! reader raised: frame budget 600 reached — reader main loop is running

  reader reached its MAIN LOOP (600 frames simulated)
  no load errors
HOST-SIM PASS
```

`game_not_supported` is the **correct** answer from a fake host that returns zeros for
every read — the reader looked for a ROM header and found nothing. That it produced a
sensible message at all means the boot chain and the speech path both work.
