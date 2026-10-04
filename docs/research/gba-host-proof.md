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

## What is still NOT proven

  * that the speech is meaningful for any game;
  * any address or offset, beyond what booting and identifying needs;
  * performance under a long session;
  * **a device boot.** All of the above is host-side. No iOS or Android device
    has run a `.gba` end to end.
