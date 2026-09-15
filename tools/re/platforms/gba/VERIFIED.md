# What is verified, and what is not — the honest ledger

Last updated after the real-ROM identification work. **Read this before believing any other
file in this directory.**

---

## Verified by execution (safe to rely on)

### The host shim works, on six of nine supported games

Game identification reads the **cartridge header**, which is a file — so a stubbed host can
serve real ROM bytes and the reader's own logic runs for real. Results:

| Game | Platform | Result | Reader said |
|---|---|---|---|
| Gold | GBC | **PASS** | `Ready` |
| Silver | GBC | **PASS** | `Ready` |
| Crystal | GBC | **PASS** | `Ready` |
| Emerald | GBA | **PASS** | `Ready` |
| FireRed | GBA | **PASS** | `Ready` |
| LeafGreen | GBA | **PASS** | `Ready` |
| Red | GB | **reaches `Ready`** | measured at frame 60,001 |
| Blue | GB | not run to completion | harness runs out of time/memory first |
| Yellow | GB | not run to completion | — |

**GBA 4/4 supported games. GBC 3/3.** FireRed's run in full:

```
rom:    Pokemon - FireRed Version (USA).gba
title:  "POKEMON FIRE"    code: "BPRE"
[oga-shim] Ready
ROM-ID PASS
```

### Five LuaJIT-isms found and handled

The readers are written for **BizHawk's LuaJIT**; mGBA embeds **stock Lua 5.4**. Every one
of these was found by *running* the reader, not by reading code:

| # | LuaJIT thing | Lua 5.4 status | fix |
|---|---|---|---|
| 1 | `require "ffi"` | absent | stub registered in `package.loaded`/`preload` |
| 2 | `module("astar", package.seeall)` | **removed in 5.2** | reimplemented in `oga_bootstrap.lua` |
| 3 | `ffi.load("tolk")` | impossible | `tolk` stubbed + registered as a module |
| 4 | `ffi.load("audio.dll")` | impossible | DLL load bypassed; `audio` cue stub installed |
| 5 | **`bit.*`** (119 call sites) | **absent** (5.3 removed `bit32`) | reimplemented, LuaJIT signed-32-bit semantics |

`bit` matters most: the readers test display-register flags with `bit.band`/`bit.lshift`, so
without it they cannot read display state at all.

### The shim's conversions are correct (31 checks)

`test-shim-mappings.lua`, all passing:
- `readbyterange` → **1-based table**, matching `readRange`'s string byte-for-byte, including
  `gb.lua`'s real `for i = 1, 360, 20` row/column pattern
- high bytes `0xED`/`0xEE` preserved (these are the reader's **menu and scroll markers** — a
  sign-extension slip would silently hide menus)
- sign extension: byte/word/dword `-1`, boundary `-128`
- register mapping `A→a`, `BC→bc`, `HL→hl`, `PC→pc`, `SP→sp`
- little-endian `readword`

### `crc32` is correct (14 checks)

Reimplemented in pure Lua rather than stubbed, because a stub returning nil makes the reader
report "game not supported" for a supported game — a **silent wrong answer**. Verified
against published CRC-32 vectors **including table input**, which is the form
`readbyterange` returns.

### The reader boots and runs its main loop

`host-sim.lua` reaches `HOST-SIM PASS`: platform identified, game scripts loaded, main loop
entered, speech path working. Confirmed against both the Temp copy and the **real v3.1.0
tree**.

---

## NOT verified (do not assume)

- **Any memory address or length against live RAM.** The harness returns zeros for RAM, so
  everything past game identification — party, position, map, text on screen — is untested.
- **Footstep detection.** `memory.registerexec` has no mGBA equivalent; it is emulated by a
  per-frame PC poll. **The MECHANISM is now verified** — `test-exec-hook.lua` (17 checks, all
  passing) proves the callback fires when the PC matches a registered address, that
  `registerexec(addr, nil)` unregisters (which `pokemon.lua:819` relies on), that multiple
  hooks stay independent, that a throwing callback does not kill the session, and that
  `registerwrite` fires on value CHANGE rather than every frame.
  **What remains unverified is whether a real footstep routine is caught** — a routine that
  runs and RETURNS INSIDE ONE FRAME can be missed by a frame poll, and that can only be
  settled against a real game.
- **Whether the speech is meaningful.** It has said `Ready` and `game_not_supported`. No
  line of actual gameplay narration has been produced.
- **The reader's main loop under mGBA's threading.** The reader owns `while true do
  emu.frameadvance() … end`, which is BizHawk's model. mGBA runs scripts on the main thread.
  This has only ever run against a stub.
- **Real audio.** 42 `audio.play` calls are recorded, not played.
- **Red/Blue/Yellow to completion.** The GB path reaches `Ready`, but the harness needs
  ~60,001 frames to get there and my runs timed out before finishing. **Not a failure** — see
  the GB note — but not a completed run either.

---

## Misdiagnoses and harness bugs worth not repeating

**0. The harness LEAKED memory and had to be force-killed at ~6.9 GB RSS.**

`lua55.exe` climbed to **6,929,880 K (~6.9 GB)** over about 40 minutes of simulated frames.
The cause is `host-sim-rom.lua`'s `readRange`: it builds a fresh table of single-character
strings on every call, and `gb.lua` calls it once per frame with 360 bytes. Over 60,000
frames that is ~21.6 million tiny strings with nothing reclaiming them.

⛔ **A harness bug, not a reader bug — but it matters for the mGBA run.** The per-frame
full-screen read is genuinely expensive: `get_screen()` calls
`readbyterange(RAM_TEXT, 360)` unconditionally, then does string work over the result. In
mGBA that runs on the emulator's main thread. If performance is poor on a real game, this is
the first thing to look at — and the fix belongs in the **shim** (avoid per-frame
allocation), not the reader. Give long runs a memory ceiling.

**1. "The GB games hang."** They do not. `gb.lua`'s `main_loop` calls `get_screen()` every
frame, which reads the whole 360-byte screen — so the reader legitimately runs ~1.2M host
calls before it starts. My harness's 200-frame budget cut it off with **no output at all**,
which looked exactly like a hang. It needs ~60,001 frames. A test harness for a
screen-reading loop must be sized for a screen-reading loop.

**2. "`audio.` is never called."** I recorded that after grepping only `pokemon.lua`, and
bypassed the DLL load on that basis. Grepping the whole set shows **42 `audio.play` calls**
plus `audio.stop` and `audio.pitch`. Leaving `audio` nil made `gb.lua` die at line 516. The
cues are **positional** (pan −100..100) — direction encoded as sound — so they are not
decoration. `oga_audio.lua` now records them and accepts a real host sink.

Both mistakes had the same shape: **a grep or a budget that was too narrow, producing a
confident wrong conclusion.** The fix in both cases was to measure the whole surface.

---

## What would move this from "verified identification" to "verified gameplay"

Running it in mGBA against a real ROM **with a save**, and comparing the spoken output to
VBA v3.1.0 on the same save. VBA is the known-good reference. That is card A6, and it is the
only remaining step that requires a human at the machine.
