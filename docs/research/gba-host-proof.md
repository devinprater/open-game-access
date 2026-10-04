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

### Where to look — the chain, traced (2026-10-04)

⛔ **`readbyterange` is NOT the culprit, and it was checked rather than assumed.**
That was the first hypothesis: the reader indexes a 1-based table while mGBA's
`emu:readRange()` returns a string, and numeric indexing of a string yields nil.
The shim's own header calls it "the single most dangerous mapping", and
`docs/plans/gb-gba-mgba-board.md` (card A3b) records it as resolved.

It IS resolved, in this copy. `mgba_compat.lua:233` reads:

    memory.readbyterange = function(addr, length)
      local raw = callReal("readRange", addr, length)
      if type(raw) ~= "string" then ... end
      local out = {}
      for i = 1, length do      -- string.byte with an explicit index, 1-based
        ...

So the string is converted to the 1-based table the readers want. The hypothesis
is refuted. Recording a guess as "the likely culprit" is how a wrong lead gets
re-chased months later, so it is corrected here rather than left standing.

**The actual chain**, from the reader's own error lines:

  * `gba.lua:315` — `for i = 0, (window.width * window.height) - 1, window.width`
    and the error is **`'for' step is zero`**. The step IS `window.width`, so
    **`window.width` is 0**.
  * `gba.lua:299-302` — `get_window_tilelines` begins `if not window.data then
    return {} end`. A window with no `data` therefore yields an EMPTY tile-line
    table rather than a useful error.
  * `gba.lua:1327` — `get_window_screen().lines`, and the error is `bad argument
    #1 to 'for iterator' (table expected, got nil)`. `lines` is **nil**, i.e. the
    screen the reader built has no lines at all.
  * Raw numbers are then spoken because the reader falls back to whatever it can
    read, and `nil` where a name should be.

So the failure is **one step upstream of the window**: the window never got its
tile data. It is built from

    gba.lua:619
    local tilemap = memory.readbyterange(get_bg_tilemap_address(id), get_bg_size(id))

so the prime suspects are `get_bg_tilemap_address(id)` and `get_bg_size(id)` —
a zero or nil from either produces a zero-length read, then `window.data` empty,
then `window.width == 0`, then exactly these three errors in this order.

**These are NOT hardware registers — they are the GAME's own RAM addresses.**

The first reading of this was wrong and worth correcting: those functions do not
touch GBA I/O at all. They read address constants the reader gets from its
PER-GAME table:

    gba.lua:532  return bit.band(memory.readbyte(RAM_BGS + 16), 0x7)
    gba.lua:540  local address = memory.readdword(RAM_BG_TILEMAPS + 16 * id + 4)

`RAM_BGS` and `RAM_BG_TILEMAPS` are defined per game under
`game/<game>/<lang>/memory.lua` — FireRed/LeafGreen and Emerald each have their
own, in each language. So the question is not "is the core reporting a register
wrongly", it is **"did the reader load the right per-game address table?"**

That is a much better fit for the symptoms, and it explains all of them at once:

  * the reader says `Ready` — but readiness only means it RECOGNISED the cart, not
    that it matched the right memory table;
  * every address then reads 0 or garbage (no `RAM_BG_TILEMAPS` write is ever
    seen at the expected place), so `get_bg_size` returns 0, the tilemap read is
    empty, `window.data` is empty, `window.width` is 0 — and that is exactly the
    `'for' step is zero` at `gba.lua:315`, the nil `lines` at `:1327` and the nil
    field at `:2691`;
  * and the raw numbers spoken instead of names are the reader falling back to
    whatever it can decode out of memory that is not the structure it expects.

**Next step: print what the reader resolved for the game** — which per-game
`memory.lua` it loaded, and the first few values it reads for `RAM_BGS` and
`RAM_BG_TILEMAPS` — for FireRed, whose address table is known-good. Compare those
against a real FireRed save in a working emulator. That decides whether the bug is
in game IDENTIFICATION (wrong table selected) or in the shim's read path (right
table, wrong bytes).

⛔ Keep the log's `GBA I/O: Read from write-only I/O register: 01x` lines in mind
but do not chase them yet: they are mGBA's own chatter about the BIOS probing
write-only registers during boot, which is normal, and they were what made the
register theory look plausible in the first place.

## What is still NOT proven

  * that the speech is meaningful for any game;
  * any address or offset, beyond what booting and identifying needs;
  * performance under a long session;
  * **a device boot.** All of the above is host-side. No iOS or Android device
    has run a `.gba` end to end.
