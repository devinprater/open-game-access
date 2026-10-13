# Chrono Trigger (SNES): the decompilation and recompilation landscape

**State: surveyed, cloned and inspected on 2026-10-13. Nothing was built or run.**
Method: search, then clone and read the trees. Five projects now sit in
`third-party/` (gitignored), beside the existing `DQ9-Access`, `DragonWarriorAccess`,
`Zelda1Access` and `pokemon-ds` reference checkouts.

## ⭐ The ROM we already have is the exact dump every one of them requires

```text
C:\Users\Devin Prater\Dropbox\games\SNES\Chrono Trigger (USA).sfc
  size      4,194,304 bytes   (4 MiB, headerless — no copier header to strip)
  SHA-1     de5822f4f2f7a55acb8926d4c0eaa63d5d989312
  SHA-256   06d1c2b06b716052c5596aaa0c2e5632a027fee1a9a28439e509f813c30829a9
```

Both hashes were computed here, not copied from a readme, and each is the exact
value a project demands:

- `BelthasarProject` checks **SHA-1 `de5822f4f2f7a55acb8926d4c0eaa63d5d989312`** and
  CRC32 `2D206BF7` before it will build anything.
- `ChronoRET` checks **SHA-256 `06d1c2b06b716052c5596aaa0c2e5632a027fee1a9a28439e509f813c30829a9`**.

⭐ **So none of this needs a new dump, and there is no region or revision problem.**
The cartridge Devin already dumped is the supported No-Intro USA release, and
`snes9x2010` independently reported the same CRC32 (`2d206bf7`, "Checksum OK") when
it loaded the ROM for the probe work.

⛔ Nothing below ships game data. Every project requires the user's own dump, and
nothing ROM-derived is committed here: `third-party/` is ignored (`.gitignore` line
165), so OGA's history and its pre-commit ROM guard are untouched.

## What exists

| Project | What it is | ★ | Licence | Created |
|---|---|---|---|---|
| `ThisIsAkill/ChronoRET` | **The matching decompilation.** Byte-exact, labelled, human-readable 65816 assembly that reassembles to an identical ROM | 0 | — | — |
| `ThisIsAkill/ct-recomp` | **The recompiler.** 65816 → native C, plus a C runtime and SDL frontend | 1 | MIT | 2026-09-23 |
| `TemplarServant/BelthasarProject` | **The native C engine.** A re-implementation in the `zelda3` style, validated routine by routine against an embedded LakeSnes "oracle" | 17 | Other | 2026-10-11 |
| `GitExl/CTViewer` | **The map viewer.** Renders scene and world maps with collision, exits, doors, treasures and actor data, for the SNES and Steam versions | 2 | MIT | 2025-07-21 |
| `dscotton/ct_disassembly` | A plain 65816 disassembly, `bank_XX` files, "most things are labelled now" | 2 | — | 2026-07-10 |

Cloned into `third-party/`: `chronoret` (7.8 MB), `ct_disassembly` (13 MB),
`belthasar` (35 MB), `ctviewer` (4.9 MB), `ct-recomp` (2.2 MB).

### ChronoRET — the decompilation proper

```
asm/bank00 bankC0 bankC1 bankC2 bankCC bankCF bankFD bankFF   symbols/  tools/
Makefile  rom.sha256  STATUS.md  NEXT.md  REVIEW.md  STYLE.md  CONTRIBUTING.md
```

The `STATUS.md` shows the method is real work, not a dump: routines are matched one
at a time and *independently reviewed*, with the bugs found on the way written down.
A sample entry names `BattleSys_RunAction`, the action loaders at
`$C1:4058–$C1:49FE` (2,471 bytes), `BattleAct_LoadScript`, and the four loaders it
dispatches on `!Battle_ActKind`; it also records ROM oddities such as three bytes at
`$CF:FFFD–FFFF` that can force the action flags, and a `BRA` to the next instruction
in the "Near" PC attack path. `symbols/functions.csv` is the generated symbol list.

### BelthasarProject — the one that speaks to our missing layer

This is the project that matters most for navigation, because it has already
re-derived the field layer and written it down:

- **Code**: `src/field.c` (7,359 lines), `src/player.h`, `src/move.h`,
  `src/collision.h`, `src/ct_vm.c` (the script VM), `src/battle.c`, `src/menu.c`.
- **284 notes** under `disasm/notes/`, including `collision.md`, `field_door_list.md`,
  `field_doors_occluders_decisionbox.md`, `field_exit_scan_guard.md`,
  `field_d9_party_walk.md`, `field_dash_run.md`, `field_camera_unification.md`,
  `field_trigger_latch_a947.md`, `field_draw_list_order.md`, `field_bg_tilemap.md`.
- The notes carry **disassembled instruction sequences and RAM structure layouts**,
  not prose. The overworld-follower note, for example, gives the lead's trail ring as
  `$1B5A` (cursor), `$1B5B` (16 words of X), `$1B7B` (16 words of Y), the reset at
  `C2/34F8` and the push at `C2/3934`, measured against the cartridge for 396 frames.

Its README is explicit about method — the ROM in an embedded LakeSnes core is the only
source of truth, a routine counts as ported only when the oracle and the C agree, and
nothing is guessed or tuned by eye. It reports 253 of 255 enemy fights matching, and
the field script interpreter covering 99.9% of script sites. ⛔ It also says plainly
that the game is "not yet verified playable from start to finish".

### ct-recomp — a runnable native Chrono Trigger

```
Routines recompiled                 1414 / 2036 known functions   (69.4%)
Native at runtime                   97.6% of instructions in a 1500-frame boot
Opcodes implemented                 251 / 256
Tests                               67 / 67 passing
Open issues                         74
```

`game/ct/` holds the game-specific half: `funcs.toml` (10,900 lines, named from
ChronoRET labels), `unresolved.toml`, `hooks.c`, `game.toml` (the ROM identity check)
and `PROGRESS.md`. The recompiler and runtime are game-agnostic.

### CTViewer — the eyes we do not have

Rust, MIT, and it reads the collision, exit, door, treasure and actor data straight
out of the ROM or the Steam `resources.bin`. Its key bindings are the useful part for
us: `x` renders player collision data, `c` NPC and battle collision, `v` Z-plane data
and flags, `b` tile movement data, `n` door data, `,` exits, `.` treasures, `/` actor
data. Devin cannot look at a rendered map, but the dumps it writes to `debug_output/`
are text and bitmaps a tool can consume.

## What this changes for the adapter

The blocker recorded in `TODO.md` is the missing layer: **the passability graph, the
heading-aware search, counted legs, and a generated target catalog**. Before this
survey we had addresses and no structure. Now:

- **Belthasar gives the structure**, as code and as measured notes: collision, doors,
  exits, the trigger latch, the field tick, the camera, the script VM.
- **CTViewer gives the data** for any scene, from the ROM, including the collision
  field that the passability graph needs.
- **ct-recomp gives a native CT with named functions to hook**, instead of a
  black-box emulator — which is the difference between guessing at the game's state
  and asking it.
- **ChronoRET gives the vocabulary** (`symbols/functions.csv`) for reading any of the
  other three.

⛔ **Judge by running, not by stars.** ChronoRET has 0 stars, ct-recomp 1, CTViewer 2
— and those three are the most complete Chrono Trigger reverse-engineering artifacts
found. BelthasarProject, with 17 stars and two days of history, is the youngest.

⛔ **Already tracked by OGA before this survey:** `ct-recomp` is named in
`docs/research/port-availability-devin-library.md` line 99, and `snesrecomp` at line
114. **`BelthasarProject`, `CTViewer` and `ChronoRET` were not.**

⛔ **`snesrecomp` is missing from the console table** in
`docs/research/static-recompilation-landscape.md`, which states that every major
console now has a translator and names eight of them. The SNES has one
(`mstan/snesrecomp`, 63★, with Super Mario World, A Link to the Past and Mega Man X
companion projects). That table should gain a SNES row.

## Next, untested

None of this has been built here, so the first step is a build, not a port. In order
of information per hour:

1. **ChronoRET** — it ships a `Makefile` and `rom.sha256`; it wants the ROM at
   `roms/chrono_trigger.sfc`. Reassembling to a byte-identical ROM would prove the
   decompilation complete for the banks it covers, and `symbols/functions.csv` can be
   diffed against the addresses our probe work already reads.
2. **CTViewer** — build it, dump the collision and exit data for Crono's house
   (scene index unknown; it is the loc `0x02` room), and see whether the player's
   start tile and the room's blocked tiles are there. That is the missing layer
   arriving as data.
3. **BelthasarProject** — read `disasm/notes/collision.md` and
   `disasm/notes/movement_pinned_open.md` before writing any passability code of our
   own; the answer may already be recorded. (It already describes a `$011F` control
   lock that freezes the player in Crono's bedroom — see Probe 7.)
4. ⛔ **Only then** return to the stuck-Crono question. If the passability data says
   tile `(8,8)` in loc `02` is walled on every side, the four earlier probes were
   never a bug at all.

## Resolved by Probe 6 (2026-10-13)

Item 4 of the next-steps list is answered, and not by the passability data. The
controller disassembly in `third-party/belthasar/disasm/notes/collision.md` decodes the
d-pad gate at `C0/8833`, and the live read in Crono's house shows `$011F == 0`: **control is
locked**, so the d-pad is discarded before any intent is formed. `$011F` is in fact a usable
control oracle. The player's real position is `$1800,X`/`$1880,X` (`X = $0197`), not the
`$0102`/`$0103` pair the older notes called the tile. Detail:
`reverse-engineering/ctds/snes-verify.md`, Probe 6.

⛔ What clears `$011F` is still unknown — and the BelthasarProject notes record the same
gap, so this is a shared edge of the reverse engineering, not an oversight of ours.
