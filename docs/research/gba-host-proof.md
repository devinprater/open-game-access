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
| FireRed | `BPRE` | boots, identified, 240x160, 64 colours, speaks |
| LeafGreen | `BPGE` | same |
| Emerald | `BPEE` | same |
| Ruby | `AXVE` | `game_not_supported` — **CORRECT** |
| Sapphire | `AXPE` | `game_not_supported` — correct |

Ruby/Sapphire are in v3.1.0's reject list on purpose. Refusing them is the shim
being honest, not a failure.

## ⛔ Measured: THE SPEECH IS NOT YET MEANINGFUL, AND THAT IS THE REAL DEFECT

The reader boots, identifies the cartridge and says `Ready`. Then it says things
like:

    136197397
    50345284
    nil
    150995016:1107296326

and its effect hooks fail inside `gba.lua`:

    [oga-shim] effect hook errored: gba.lua:315: 'for' step is zero
    [oga-shim] effect hook errored: gba.lua:1327: bad argument #1 to 'for iterator' (table expected, got nil)
    [oga-shim] effect hook errored: gba.lua:2691: attempt to index a nil value (field '?')

**This is the shim handing the reader bad data, not an emulation problem and not a
regression.** The reader is running against live memory and getting values it
cannot interpret, so it speaks raw numbers and `nil` where a place name should be.

This was invisible before because no harness could boot the game. It is now the
top of the Game Boy queue, and the harness exists to keep it visible: a reader
that runs silently is worse than one that crashes, because nothing reports it.

### Where to look

The three failures point at one thing — a table the reader expects to be populated
and finds nil:

  * `gba.lua:315` — a numeric `for` over something that evaluates to 0 step
  * `gba.lua:1327` — `for` over a `nil` where a table is expected
  * `gba.lua:2691` — indexing a nil field

`gba.lua:1327` firing repeatedly suggests a per-frame table lookup (map data or
the tile/event table) that the shim's read path returns nil for. The likely
culprit is `readbyterange`: the reader indexes a 1-BASED table and mGBA returns a
string, and numeric indexing of a string yields nil. That exact hazard is already
recorded as resolved in `docs/plans/gb-gba-mgba-board.md` (card A3b) — so the
first thing to check is whether the fix is present in THIS copy of the shim
(`Sources/OpenGameAccess/Resources/gba-lua/mgba_compat.lua`) or only in the one
that was proven on Windows mGBA.

## What is still NOT proven

  * that the speech is meaningful for any game;
  * any address or offset, beyond what booting and identifying needs;
  * performance under a long session;
  * **a device boot.** All of the above is host-side. No iOS or Android device
    has run a `.gba` end to end.
