# What is verified, and what is not — the honest ledger

Last updated after **running the reader on real mGBA**. Read this before believing any other
file in this directory.

---

## VERIFIED BY EXECUTION ON REAL mGBA 0.11 (dev)

**The unmodified v3.1.0 reader boots, identifies the cartridge, and SPEAKS inside a real
emulator.** Measured, via `mgba-dev --script`:

```
[oga-shim] shim installed; host platform = 0
[oga-boot] loading reader: .../lua/pokemon.lua
[oga-shim] Ready
```

and the captured speech sink received exactly:

```
Ready
```

This is the A5 deliverable: no stub host, no simulated frames — a real emulator, a real ROM,
the real reader.

### The five bugs that made this work, each requiring a real emulator to find

| # | Symptom | Root cause |
|---|---|---|
| 1 | `oga_bootstrap.lua:321: Error calling function (invoking failed)` | Real mGBA's `emu` is **userdata**, not a table. `emu.platform = fn` raises `Invalid key`. |
| 2 | `Function called from invalid context` (intermittent) | **Reassigning `emu`** (`emu = {}`) breaks the core binding. Nor may the metatable be patched. |
| 3 | Same, at a second unrelated line | `input` is **also** mGBA-owned userdata in 0.11. Same class of mistake, different name. |
| 4 | `message.lua:4: attempt to concatenate a nil value (global 'scriptpath')` | Loading the reader chunk with a placeholder chunk name makes `debug.getinfo(1,"S").source` useless. Pass the real path. |
| 5 | `Function called from invalid context` on the 2nd frame advance | **Registering a `frame` callback breaks `runFrame`.** mGBA switches to callback-driven emulation and `runFrame` stops being legal. |

**The one design that works** (measured: 100/100 consecutive frames advanced):

> Leave the global `emu` completely alone. Build a **separate** BizHawk-shaped table
> (`oga_biz_emu`), and give it to the reader by loading the reader chunk with its own
> environment. Reads of other globals and **all writes** proxy to the real `_G`, so the
> reader's cross-file globals behave exactly as under BizHawk.

And: **never register a `frame` callback.** Hook polling rides on `frameadvance`, so every frame
the reader requests also samples the PC and the write watches.

### mGBA globals the shim MUST NOT touch (0.11)

Measured with `type()` at script load:

```
emu (userdata)   input (userdata)   callbacks (userdata)   console (userdata)
util  storage  image  canvas  system            (all userdata)
socket (table)

memory  (nil — genuinely free, so the shim may create it)
```

⛔ Assigning to any userdata raises `Invalid key`, and the traceback points at the assignment
rather than the real problem. Always `type()` a global before writing to it.

### Also verified by execution

- **`--script FILE`** runs a script at startup (this is what removed the "needs a human at the
  GUI" blocker).
- `emu:runFrame()` works **6/6 consecutively** raw, and 100/100 through the separate table.
- `setBreakpoint` / `setRangeWatchpoint` / `clearBreakpoint` exist in 0.11 and are callable
  (`setRangeWatchpoint` returned id 1 and cleared cleanly) — available if the frame-poll
  approximation ever needs replacing.
- Game identification from real ROM headers, 6 of 9 supported games (see the matrix below).
- 31 mapping checks, 14 CRC checks, 17 exec-hook checks, 10 register-width checks — all passing.

### The identification matrix (stub host, real ROM bytes)

| Game | Platform | Result |
|---|---|---|
| Gold / Silver / Crystal | GBC | **PASS** — `Ready` |
| Emerald / FireRed / LeafGreen | GBA | **PASS** — `Ready` |
| Red | GB | reaches `Ready` at frame 60,001 |
| Blue / Yellow | GB | not run to completion (harness time/memory) |
| Ruby / Sapphire | GBA | correctly **rejected** — not in v3.1.0's support list |

**GBA 4/4 supported games. GBC 3/3.** And now, independently, **FireRed verified on real mGBA**.

---

## NOT VERIFIED

Everything that depends on reading live game state during play:

- **Any address, offset or length.** The boot path and identification are exercised; the actual
  gameplay readers have not been checked against a real save.
- **Whether footsteps fire.** The *mechanism* is verified (17 checks) and the PC now decodes at
  full width instead of being truncated to its low byte — but whether a real footstep routine is
  caught by a one-sample-per-frame poll is open. A routine that runs and returns inside a single
  frame could still be missed.
- **Whether the speech is meaningful.** `Ready` is real output, but it proves identification
  only.
- **Performance.** The reader reads the whole 360-byte screen every frame; in mGBA that is on
  the emulator's main thread. Untested at scale.

---

## Misdiagnoses and harness bugs worth not repeating

**0. The harness LEAKED memory and had to be force-killed at ~6.9 GB RSS.**

`lua55.exe` reached **6,929,880 K** over ~40 minutes of simulated frames. `host-sim-rom.lua`'s
`readRange` builds a fresh table of single-character strings per call, and `gb.lua` calls it
once per frame with 360 bytes — ~21.6 million tiny strings. **A harness bug, not a reader bug**,
but it is also the first thing to suspect if a real run is slow. Give long runs a memory ceiling.

**1. "The GB games hang."** They do not. A 200-frame budget cut the reader off with **no output
at all**, which looks exactly like a hang. It needs ~60,001 frames because `main_loop` reads the
full screen every frame. **A test that produces no output is not evidence of a hang until the
budget is shown to be large enough.**

**2. "`audio.` is never called — verified by grep."** Recorded after searching only
`pokemon.lua`. The full set has **42 `audio.play` call sites**. The DLL load had been bypassed on
that false basis, and the GB reader then died at `gb.lua:516`.

**3. A `strings` check that could not detect anything.** I concluded `readRange` was absent from
the binary because `strings -n 5 mgba.exe | grep -c readRange` returned 0 — but `strings` had
produced **zero output in total**, on a 41 MB binary. The tool was broken, and I nearly recorded
a false negative as a finding. **Check that a probe returns anything at all before trusting a
zero.**

**4. A register decoded at the wrong width.** mGBA's `readRegister` returns a **little-endian
byte string**. The shim did `v:byte(1)` — low byte only — turning a PC of `0x08000123` into
`0x23` (35). Footstep detection compares the polled PC against a registered address, so a
truncated PC can **never** match and walking would be silent while everything else worked. Fixed
at full width; `test-register-width.lua` asserts the value is *not* `0x23`.

**5. The harness was MORE PERMISSIVE THAN REALITY.** This is the big one. The stub host defined
`emu` as a plain Lua table, so every shim design that assigns to `emu` **passed all 70+ checks**
and was wrong. Real mGBA's `emu` is userdata. A stub that models the host's *shape* incorrectly
will validate an architecture that cannot work, and no amount of testing against it will find
that — only the real emulator will. **Stub the constraints, not just the API.**

⚠️ **`vba.ini`'s `luaDir` does not auto-run scripts.** It is only the file dialog's start
directory; `pokemon.lua` is loaded by hand. Believing otherwise once motivated a "cleanup" that
moved the loader out of `lua\` and broke the reader.
