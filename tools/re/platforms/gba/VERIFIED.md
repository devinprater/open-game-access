# What is verified, and what is not — the honest ledger

Last updated after **live gameplay verification on real mGBA 0.11 (dev)**. Read this before
believing any other file in this directory.

---

## VERIFIED BY EXECUTION — LIVE GAME STATE, REAL EMULATOR

The unmodified v3.1.0 readers now **read live game state and speak it**, verified on real ROMs
in real mGBA. This is the milestone that matters: it means the memory addresses themselves are
correct, not merely that the software loads.

### GBA path — FireRed, driven to the overworld

Pressed real hotkeys via the shim's frame hook. Every one produced correct narration:

```
Ready
x 6, y 6                  <- Y: player position
OSCAR's House             <- M: map name
x 6, y 6                  <- Y
Now on0 (3); Up0 (0); Down0 (3); Left0 (3); Right0 (3)   <- E: surrounding tiles
OSCAR's House             <- M
x 6, y 6                  <- Y
```

Position, map name and the four surrounding tiles are all correct for the player's bedroom at
the start of FireRed. **`Y`, `M` and `E` are verified end-to-end.**

### GB/GBC path — Gold, on the intro cutscene

The GB path behaves differently: `gb.lua`'s `main_loop` **auto-reads text when the screen
content changes**, so it speaks without any hotkey. It read real dialogue:

```
Ready
This world is in- habited by crea-
Not on a map.
```

`This world is inhabited by creatures...` is Gold's opening dialogue, read off the live screen
(tiles split across lines, hence `in- habited`). `Not on a map.` is the reader's **correct**
answer to a hotkey pressed while still in the intro cutscene — the same
`if needs_map and not on_map()` guard that behaves properly.

**This verifies the GB path's screen-text reader against a real game**, which is a different
code path from the GBA one.

---

## The five bugs that made this work

| # | Symptom | Root cause |
|---|---|---|
| 1 | `oga_bootstrap.lua:321: Error calling function (invoking failed)` | Real mGBA's `emu` is **userdata**, not a table. `emu.platform = fn` raises `Invalid key`. |
| 2 | `Function called from invalid context` (intermittent) | **Reassigning `emu`** (`emu = {}`) breaks the core binding. Patching its metatable also fails. |
| 3 | Same, at a second unrelated line | `input` is **also** mGBA-owned userdata in 0.11. |
| 4 | `message.lua:4: attempt to concatenate a nil value (global 'scriptpath')` | Loading the reader chunk with a placeholder chunk name makes `debug.getinfo(1,"S").source` useless. Pass the real path. |
| 5 | `Function called from invalid context` on the 2nd frame advance | **Registering a `frame` callback breaks `runFrame`.** mGBA switches to callback-driven emulation. |

Plus two that made the reader **run but never respond**:

| # | Symptom | Root cause |
|---|---|---|
| 6 | Reader runs, says `Ready`, ignores every key, **no error** | **`unpack` is nil.** Removed in Lua 5.2 (now `table.unpack`). `pokemon.lua:492` — the line decoding *every* hotkey — raises inside `main_loop`, so it is swallowed. 7 call sites. |
| 7 | Same silence, second cause | A **phantom key**: `input.read()` returned an extra `mask` field. The reader iterates every truthy entry, so the key set became `{"Y","mask"}` and matched no command. |

### The design that works (measured: 100/100 consecutive frames)

> Leave the global `emu` alone. Build a **separate** BizHawk-shaped table (`oga_biz_emu`), and
> give it to the reader by loading the reader chunk with its own environment. Reads of other
> globals and **all writes** proxy to the real `_G`, so the reader's cross-file globals behave
> as under BizHawk.

And: **never register a `frame` callback.** Hook polling rides on `frameadvance`.

### Implicit LuaJIT-era globals the shim must restore

Found by *running*, never by reading. Under BizHawk these are simply part of the runtime, which
is why none appear in BizHawk's documentation:

| Global | Lua status | Consequence if missing |
|---|---|---|
| `ffi` | absent | `require "ffi"` fails; Tolk/DLL layer dies |
| `module()` | **removed in 5.2** | `a-star.lua` fails on the reader's first statement |
| `bit` | absent (LuaJIT builtin) | **119 call sites**; display-register tests break |
| `unpack` | **removed in 5.2** | **every hotkey silently dead** (7 call sites) |

### mGBA globals the shim MUST NOT touch (0.11)

```
emu  input  callbacks  console  util  storage  image  canvas  system   -> userdata
socket                                                                 -> table
memory                                                                 -> nil (free!)
```

Assigning to any userdata raises `Invalid key`, and the traceback points at the assignment.

---

## OTHER VERIFIED RESULTS

- **`--script FILE`** runs a script at startup (this removed the "needs a human at the GUI"
  blocker). `scoop install mgba-dev` provides it; stable 0.10.x has no CLI scripting entry.
- Game identification from real ROM headers: **6 of 9 supported games** (Gold/Silver/Crystal
  GBC; Emerald/FireRed/LeafGreen GBA). Ruby/Sapphire are correctly *rejected*.
- `setBreakpoint` / `setRangeWatchpoint` / `clearBreakpoint` exist in 0.11 and are callable.
- 31 mapping checks, 14 CRC checks, 17 exec-hook checks, 10 register-width checks — all passing.
- **A 131,072-byte FireRed `.sav` that is 100% `0xFF` is a BLANK cartridge.** Every run against
  it is a new game. Two other saves in the same folder: one for a *hack build*, one only 4.4%
  populated. Check save content before trusting a save-based test.

---

## A CONFIRMED DEFECT: exec hooks CANNOT fire on ROM addresses

Measured on real FireRed while the reader ran, with hooks installed and the game live:

```
[hooks] f=1 registered=38 pc=134219948 hit=false
[hooks] f=2 registered=38 pc=134219948 hit=false
[hooks] f=3 registered=38 pc=134219948 hit=false
[hooks] f=4 registered=38 pc=134219952 hit=false
...
```

**38 hooks registered, 0 hits over thousands of frames.** The sampled PC spans only
`0x08000148`-`0x0800016C` — a ~60-byte window, which is the CPU's end-of-frame resting place,
not a record of execution.

The readers register these hooks at **ROM addresses in `0x08000000`-range** (e.g.
`ROM_CPU_SET = 0x82e7084`, `ROM_RENDER_TEXT = 0x800587c`, `ROM_FREE = 0x8002bc4`). Frame-polling
the PC after `runFrame()` cannot observe that code, because:

1. ROM code is reached through **mirrors** and executes from cached addresses, and
2. by the time a frame completes, the PC has moved on — `runFrame()` runs an entire frame, so
   sampling after it samples the resting place, never the execution.

⛔ **Consequence:** the features driven by `registerexec` do not work under this shim —
**footstep detection**, and the VRAM-DMA hook that tracks screen updates (`cpu_set` /
`cpu_fast_set` read `r1`/`r2` at the instruction to rebuild the tilemap). This is a real,
measured defect, **not** the "maybe a routine runs within one frame" concern previously
recorded — the addresses are wrong for polling at all.

**The fix is available but unverified:** mGBA 0.11 exposes **real** breakpoints and range
watchpoints (`emu:setBreakpoint(fn, addr, -1)`, `emu:setRangeWatchpoint`), confirmed present in
the binary and callable in an earlier probe. The shim should map `memory.registerexec` onto
`setBreakpoint` instead of polling. That would give true execution hooks. **It has not been
implemented or tested yet**, and a naive attempt while probing interfered with `runFrame`, so the
integration needs care (install once, at the right time, and confirm `runFrame` still works).

## NOT VERIFIED

- **Footsteps — now CONFIRMED BROKEN, see the section above.** Not merely "unproven": measured
  0 hits from 38 registered hooks on a live game, because they target ROM addresses that a
  frame poll cannot observe. The fix (map `registerexec` onto mGBA's real `setBreakpoint`) is
  available but not yet implemented.
- **The remaining GBA hotkeys** — `P` (pathfind), `H`/`Shift+H` (battle health), `J`/`K`/`L`
  (item cycling), `T` (read text), the camera keys. Each needs specific game state (a battle, an
  item menu, a two-object map).
- **Performance.** The reader reads the whole 360-byte screen every frame, on mGBA's main
  thread. Untested at scale.

---

## Misdiagnoses and harness bugs worth not repeating

**0. A stub host MORE PERMISSIVE THAN REALITY validates an impossible design.** This is the
biggest lesson. The stub defined `emu` as a plain Lua table, so every shim that *assigns* to
`emu` passed **70+ checks** — while all of them were unfixable on the real emulator. **Stub the
CONSTRAINTS, not just the API:** model userdata-ness, re-entrancy, and any mode switch.

**1. The harness LEAKED memory** — ~6.9 GB RSS over 40 minutes of simulated frames. `readRange`
built a fresh table of single-character strings per call, 360 bytes at a time. A harness bug, and
also the first thing to suspect if a real run is slow.

**2. "The GB games hang."** They do not. A 200-frame budget cut the reader off with no output.
It needs ~60,001 frames because `main_loop` reads the full screen every frame.

**3. "`audio.` is never called — verified by grep."** Recorded after searching only
`pokemon.lua`. There are **42 `audio.play` call sites**.

**4. A `strings` probe that could not detect anything.** `strings -n 5 mgba.exe | grep -c
readRange` returned 0 — and `strings` had produced **zero output in total** on a 41 MB binary.
Nearly recorded a false negative as a finding.

**5. A register decoded at the wrong width.** `readRegister` returns a little-endian **byte
string**; `v:byte(1)` truncates it, turning PC `0x08000123` into `0x23`. Footsteps compare the
polled PC against a registered address, so a truncated PC can never match.

**6. Instrumentation that became the bug.** Wrapping `handle_user_actions` and calling
`_G.input.read()` touched mGBA's **userdata** `input` and raised `Invalid key` — so the
diagnostic broke the reader. Read the shim's own `BIZ_INPUT`, never the global.

⚠️ **`vba.ini`'s `luaDir` does not auto-run scripts** — it is only the file dialog's start
directory. Believing otherwise once motivated a "cleanup" that moved the loader out of `lua\`
and broke the reader.
