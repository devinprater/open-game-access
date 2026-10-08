# Game Boy / GBA on the host — what actually runs, measured

Written after the first successful host boot of a real Game Boy ROM through the
app's own code path. Everything below is measured, not inferred. The harness that
produced it is `scripts/gba-host-proof.sh`.

## What the harness does

It links the real `Core/pokecore.cpp` against the real mGBA core and the real
Pokémon Access reader set, then drives it exactly as the app does:

    poke_load_rom -> poke_set_script_dir -> poke_start -> poke_frame

and prints every line the reader speaks plus the framebuffer it produced.

⛔ **This is not what `gba-adapter-test.sh` does.** That test builds a STUB host
and checks selection, readiness and the refusal paths — it never boots a game, so
it cannot see a reader that runs but says nothing useful. Only a real boot can.

## ⛔ A host harness COULD NOT bot a Game Boy ROM before this, and the cause was a comment

`scripts/core-sources.sh` said mGBA's `third-party/lzma` was unneeded because
"nothing in the kept mGBA subset references it (verified by object scan)".

That is false. `src/util/vfs/vfs-lzma.c` IS in that subset, and it is the `.7z`
archive support behind `VDirOpenArchive` — which `mCoreFind` calls on EVERY ROM
path, not just archives. On the app those symbols come from PPSSPP's identical
SDK copy, so the claim was accidentally true there. On the host there is no
PPSSPP, so they fell through to the abort-on-call stubs in
`Core/host_harness_stub.cpp` and the first `.gba` boot died with:

    !! host harness: InFile_Open is not available in NDS harness builds

Nothing was wrong with the emulator. One build list was missing and a stub was
hiding it. Fixed by compiling the real SDK for host builds (`MGBA_LZMA`) and
deleting the 7z stubs; the PSP stubs stay, because nothing in a host NDS/GBA
harness should ever boot a PSP game and an abort says so.

## Measured: the core works

2500 frames per ROM, real reader set:

| ROM | Code | Result |
|---|---|---|
| FireRed | `BPRE` | boots, identified, 240x160, 64 colours, speaks (incl. real naming-screen content) |
| LeafGreen | `BPGE` | same |
| Emerald | `BPEE` | same |
| Ruby | `AXVE` | `game_not_supported` — **CORRECT** |
| Sapphire | `AXPE` | `game_not_supported` — correct |

Ruby/Sapphire are in v3.1.0's reject list on purpose. Refusing them is the shim
being honest, not a failure.

## ⛔ CORRECTION (2026-10-04): the READER IS FINE. The HARNESS is the blocker.

An earlier version of this file said "the speech is not yet meaningful" and blamed
the shim for handing the reader bad data. **Both parts of that were wrong**, and
they are corrected here rather than left standing, because the difference decides
where the next hour goes.

### The reader read real content

Run past the title screen and the reader speaks the NAMING SCREEN's own UI:

    [SPEAK] A
    [SPEAK] OK
    [SPEAK] a

That is a real Game Boy Advance game's screen decoded from live RAM. It is the
first meaningful speech this project has had from a `.gba`, and it proves the
chain end to end: cart identified, per-game memory table loaded, screen read,
text decoded, speech out.

### The numbers and `nil` are the reader describing a game that has not started

Every remaining error is in a MAP function, failing because there is no map yet:

    gba.lua:2691   play_footsteps():
                     local blocks = get_map_blocks()
                     play_tile_sound(get_block_type(blocks[player_y][player_x]), ...)
                   -> "attempt to index a nil value" is `blocks[player_y]`:
                      the block table has no row for the player.
    gba.lua:315    a `for` whose STEP is `window.width` -> width is 0, because the
                   window was built from an empty tilemap.
    gba.lua:1327   get_window_screen().lines is nil -> no screen was built.

All three are "the game is on a title or naming screen". They are NOT
data-corruption symptoms, and they are not a shim fault.

### ⛔ The actual blocker: no harness gets the game into the world

    A only          -> types a letter forever; never confirms the name.
    A + START       -> still stalled after 12000 frames.

START is the confirm button on the naming screen by hand, so a script has to walk
a screen sequence (naming -> OK -> dialogue -> into the world) that this probe
does not know. **So the reader is UNVERIFIED IN THE WORLD, because nothing has got
it there.** That is a TEST-HARNESS LIMITATION and it must not be reported as a
reader defect — they need completely different work.

### What would settle it, in order of cost

  1. **A ready save.** A `.sav` standing in the world, loaded via the core's
     existing save support, removes the intro entirely and is one run. There is no
     `.sav` anywhere in the ROM tree today.
  2. **A savestate.** `gba_save_state`/`gba_load_state` exist, so a state captured
     once at the point of entering the world would make every later run cheap.
     Bootstrapping it still needs one successful walk through the intro.
  3. **A scripted intro.** Feasible but trial-and-error over ~15-minute runs; the
     probe's button script is the thing to improve.

Until one of those lands, treat any run that starts at a title screen as expected
to say `Ready` and then numbers.

## ⛔ MEASURED ON A REAL GAME BOY ROM: the reader is MEANINGFUL, and the shim had a bug

The section above was written from the GBA path and is correct for it. It undersold the Game
Boy path, which was then actually run (Pokémon Red, 40000 frames) with a traceback-instrumented
speech sink instead of inference.

### The Game Boy reader speaks real game content, end to end

    [SPEAK] ->NEW GAME
    [SPEAK] Hello there! Welcome to the
    [SPEAK] world of POKéMON!
    [SPEAK] People call me the POKéMON PROF!
    ...
    [SPEAK] First, what is your name?
    [SPEAK] ->NEW NAME
    [SPEAK] Right! So your name is AJR!
    ...
    [SPEAK] That's right! I remember now! His name is BKJ!
    [SPEAK] AJR is playing the SNES! ...Okay!

That is Oak's opening speech, read line by line out of live RAM, plus the naming screen's own
UI and the name the walk typed. **Zero `nil` and zero raw-number emissions, and zero hook
errors** — the two symptoms the GBA title-screen path shows do not appear here at all. So the
"numbers and nil" report is a GBA-title-screen artefact, not a Game Boy reader defect.

### ⛔ The real bug this found: a RESET hook was firing as a MOVEMENT hook

The same run said `Ready` **729 times** in 40000 frames. Instrumenting the speech sink with
`debug.traceback` gave the caller directly, not a guess:

    pokemon.lua:893   (inside init_script)
      <- mgba_compat.lua:342  (pollExecEffects)
      <- mgba_compat.lua:183  (frameadvance)

`pokemon.lua:1057-1058` registers `init_script` at the CPU's **entry vector** (0x100 on Game
Boy, 0x8000000 on GBA) so the reader survives a **soft reset**. That is a RESET handler. The
shim's `registerexec` folds every registration into one table, and its movement poll fires that
table whenever the player's position changes — so `init_script` ran on **every step**. It calls
`get_game()` -> `load_game()` and ends by `tolk.output("Ready")`, which is why the whole reader
was being rebuilt and re-announced while the player walked.

Fixed in `mgba_compat.lua`: entry vectors (0x100, 0x8000000) are held in their own table and are
never fired by the movement poll; they stay reachable from the PC sample, the only mechanism
that can legitimately observe a reset. Re-measured on the same ROM: `Ready` 729 -> **1**, with
the dialogue above preserved. Guarded by `scripts/registerexec-kind-test.sh` (3 sabotage
mutations, CI).

⛔ The GBA path is UNCHANGED by this: its pre-world `nil`/numbers come from
`register_common_callbacks` genuinely registering ~38 map predicates, and the harness still
cannot walk FRLG's intro into the world. That remains a harness limitation, as documented above.

## ✅ THE WORLD IS REACHABLE NOW — and what in-world speech actually shows

The section above says the harness "cannot currently arrange" the game standing in the world.
That was true of the harness, not of the emulator, and it is now fixed.

### The trick: walk the intro with a no-op script, then replay the world with the real reader

The real reader reads the whole 360-byte screen and runs ~38 predicates EVERY FRAME. Measured:
~1000 frames in ~5 minutes. Emerald's intro is minutes of emulated time, so walking it with the
reader loaded costs HOURS per attempt — which is why every earlier run gave up at the title.

But the emulation speed is the same either way. So `scripts/gba-reach-world.sh`:

1. points `OGA_READER_DIR` at a directory holding the REAL `mgba_compat.lua` (so `emu`, `memory`
   and `frameadvance` all work) plus a three-line `pokemon.lua` that only advances frames. The
   core is byte-identical to the app's; only the script is a stand-in;
2. drives the intro with the probe's button profile and captures a savestate.
   **Measured: 120000 frames in ~20 seconds** (vs hours), reaching the player's HOUSE;
3. resumes that state with the REAL reader (`--state-in`, no `OGA_READER_DIR`), which boots
   straight into the world with meaningful state.

⛔ Verified by LOOKING, not by inferring: the probe's `--shot-every` writes PPMs and
`scripts/ppm2png.py` converts them. The frame at the walk's end shows the player's bedroom with
the player standing in it, and a dialogue box reading "It's a POKéMON brand moving and
delivery" — the genuine in-game message for the moving box. The savestate is 397 KB (not the
all-zeros shape that the save-state bug produced).

### ⛔ What in-world speech shows: text is fine, the register-dependent hooks are not

Resumed in the world, the reader produces a MIX, and the split is diagnostic:

    [SPEAK] 49154:58718      <- set_wall_clock, string.format("%d:%02d", hours, minutes)
    [SPEAK] 4                <- read_how_many, tostring(memory.getregister("r1"))
    [SPEAK] <spaces>         <- read_mainmenu_item, lines[position + 17]

Every one of those comes from a hook whose body reads a CPU REGISTER
(`memory.getregister(...)`) — the traceback shows them firing from the movement poll. On the
GBA the reader obtains TEXT by intercepting ROM_RENDER_TEXT and reading the register that
points at the glyph run, and it obtains MENU STATE the same way. In BizHawk those hooks fire at
the exact instruction; mGBA has no exec hook, so the shim approximates them by firing on player
movement (see the long note in `mgba_compat.lua`), and a register sampled a frame later holds
unrelated data. Measured across the reader: **18 of gba.lua's 68 hook functions and 6 of
rse.lua's 19 read `getregister`** — that is the set that cannot work this way.

⛔ SO THE GBA PATH IS NOT "BROKEN BY THE SHIM" AND NOT "FIXED EITHER". The screen-buffer path
(Game Boy, and the GBA naming/title screens that read a window) works and is meaningful. The
GBA in-world text/menu path depends on exec hooks mGBA does not expose, and that is a DESIGN
gap for the GBA reader, not a defect in the file. The shim's own header predicted exactly this
and named the fallback: poll the observable EFFECT rather than the PC. Doing that per-hook means
knowing, for each of those 24 functions, what observable state proves it ran — which is real
per-hook design work and is NOT attempted here.

## ✅ MEASURED: mGBA HAS REAL EXEC HOOKS, AND OUR HOST CAN USE THEM

The in-world section above concludes that the register-reading hooks "need each of those hooks
rebuilt around an OBSERVABLE EFFECT", because "mGBA exposes no exec hook". **That premise is
half wrong, and the correction is the whole fix.** The shim's header said it because mGBA's *Lua*
API has no exec hook. mGBA the emulator has one:

    src/arm/debugger/debugger.c — ARMDebuggerCheckBreakpoints() checks every installed
    breakpoint against the PC, and the debugger's run loop calls core->step() +
    checkBreakpoints() per instruction.

The reason it never fired is OUR HOST, not the emulator: `gba_frame()` calls
`core->runFrame()`, and `runFrame -> ARMRunLoop` never checks breakpoints. Only
`mDebuggerRunFrame()` does. So the capability was always compiled in (`ENABLE_DEBUGGERS` is in
`MGBA_DEFS`, and the debugger TUs are in the audited source list) and simply never driven.

### The spike, and what it measured

`tools/psp/…`-style throwaway: a small C program that attaches an `mDebugger`, sets a
`BREAKPOINT_HARDWARE`, and runs frames through `mDebuggerRunFrame`. Measured on Emerald:

    baseline  runFrame              : 0.146 ms/frame
    debugger + breakpoint installed : 0.328 ms/frame    (~2.2x, still ~50x faster than real time)
    breakpoint at 0x08000000        : HITS=1, firstPC=0x08000000, r1 readable at that instant
    breakpoint at 0x08010000        : HITS=0
    breakpoint at 0x08020000        : HITS=0

So: **the hook fires at the exact instruction, the CPU registers are correct at that moment, the
hook is address-specific, and it costs about 2.2x baseline.** That is precisely what the reader's
24 register-dependent hooks need, and it fixes them ALL AT ONCE rather than one design at a time.

### ⛔ THE ONE TRAP, AND IT COSTS AN AFTERNOON: CLEAR `isPaused` OR THE EMULATOR HANGS

`mDebuggerEnter()` sets `module->isPaused = true` BEFORE calling the callback, and
`mDebuggerUpdatePaused()` then moves the debugger to `DEBUGGER_PAUSED`. `mDebuggerRunTimeout()`
in that state waits on a condition with a timeout instead of executing, so the run stops dead at
the first breakpoint with no output and no error. mGBA's own scripting layer does this at the end
of `_scriptDebuggerEntered`:

    debugger->isPaused = false;

A callback that forgets that one line hangs the emulator. It reads as "the probe crashed or hung
for no reason", which is why it is written down here rather than left in the spike.

### What this changes about the plan

The GBA in-world work is no longer "design an observable-effect substitute for 24 hooks". It is:

  1. drive frames through a debugger so breakpoints are checked;
  2. translate the reader's `memory.registerexec(address, fn)` calls into real breakpoints;
  3. make the callback clear `isPaused` and then invoke the reader's function with the registers
     live (the shim's `getregister` already reads them through the core API).

The 18 gba.lua + 6 rse.lua register hooks then work as written, unmodified — which is the
project's standing rule for the readers. ⚠ Not yet wired: the above is a measured capability, and
the shim integration is the next step, not a finished feature.

## What is still NOT proven

  * that the speech is meaningful for any game;
  * any address or offset, beyond what booting and identifying needs;
  * performance under a long session;
  * **a device boot.** All of the above is host-side. No iOS or Android device
    has run a `.gba` end to end.
