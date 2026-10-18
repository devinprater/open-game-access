# Pokémon Stadium 1 vs 2: which adapter is easier

Checked 2026-10-18. Both ROMs were booted and their RDRAM read through RetroArch's
MCP harness; the decompilation numbers come from the two `pret` repositories cloned
the same day. Nothing was added to OGA's source tree.

## Verdict

**Pokémon Stadium 1 is the easier adapter, and it is not close.** The deciding
factor is documentation, not the game: `pret/pokestadium` has decompiled the game to
C, and `pret/pokestadiumgs` (Stadium 2) has not. The second factor is size — Stadium 1
has half the ROM, 151 species instead of 251, Gen 1 battle rules instead of Gen 2, and
nine minigames instead of twelve.

The one point in Stadium 2's favour is that Devin's ROM is a byte-exact match for the
decompilation's target. That is a real advantage, but it buys symbol addresses without
buying any readable code, which is the smaller half of the work.

## What is shared, and what blocks both

- **OGA has no N64 core.** `Core/systems.cpp` lists `OGA_SYS_N64` as
  `OGA_BACKEND_PLANNED`, and `Core/n64_adapter.cpp` is a seam that refuses on purpose.
  Any adapter work is gated on a core, for either game.
- **The N64 iOS problem is unchanged.** The platform plan records N64 as needing a
  recompiling core and notes that ARM64 iOS sideloads get no JIT entitlement, so this
  is interpreter-or-nothing on a phone. Stadium 2's 64 MB image and heavier scenes make
  that worse than Stadium 1's 32 MB, but neither is proven on device.
- **A readable channel exists today on the desktop.** RetroArch's `mupen64plus_next`
  declares RDRAM at `0x80000000` (8 MiB) and answers `READ_CORE_MEMORY`. An adapter can
  therefore be researched and proved against a real running game now, before an OGA
  core exists.

## Measured today

Both ROMs are the ones in `Dropbox/games/N64`:

| ROM | Size | MD5 |
|---|---|---|
| Pokémon Stadium (USA) (Rev 2) | 32 MB | `6dc6820cef755fc1253d06df45c9bd2a` |
| Pokémon Stadium 2 (USA) | 64 MB | `1561c75d11cedf356a8ddb1a4a5f9d5d` |

- Both boot to the title screen under `mupen64plus_next`, report `PLAYING`, and return
  non-zero RDRAM. Screenshots of both are on disk.
- Stadium 1's state pair was found and confirmed live: pressing Start at the title
  moved the game to the **Game Pak Check** screen, and the word pair at
  `0x80075768`/`0x8007576C` changed from `2`/`1` to `4`/`2`. Those are
  `STATE_TITLE_SCREEN` and `STATE_MENU_SELECT` from the decompilation's own `GameState`
  enum, and `gCurrentGameState`/`gLastGameState` are adjacent in the same order.
- ⛔ The decompilation's address for that pair is `0x80075668` — the live Rev 2 address
  is **`0x80075768`, a +0x100 shift**. Reading the decompilation's address on Rev 2
  returns unrelated data, so the symbol table is a design reference here, not an
  address list. Every address still needs a live check on the ROM that will ship.
- Stadium 2's ROM md5 matches `pret/pokestadiumgs` exactly; Stadium 1's does not match
  `pret/pokestadium` (that project targets US 1.0, md5 `ed1378bc…`).

## Documentation comparison

Cloned 2026-10-18; counts are from the repositories themselves.

| | Stadium 1 (`pret/pokestadium`) | Stadium 2 (`pret/pokestadiumgs`) |
|---|---|---|
| Last commit | `6995f32`, 2026-10-03 | `c0e10f2`, 2026-07-10 |
| C source files | 346 | 425 |
| Decompiled C functions | ~7,314 (199 empty bodies) | 96 |
| Remaining `GLOBAL_ASM` pragmas | 73 | 12,814 |
| `typedef struct` definitions | 933 | 2 |
| Named game states | Yes — `GameState` enum names every screen | No |
| Battle-adjacent C | Yes — stat calculation, damage/status fragments, a species base-stat table | Assembly only |
| Native recompilation | `mstan/PokemonStadiumRecomp` (US 1.0) | None found |

The practical reading: in Stadium 1 the reader author can open the game's own code and
learn what a screen is, what a struct holds, and how a stat is derived. In Stadium 2
every one of those questions is a disassembly session against `func_XXXXXXXX`.

## Game-surface comparison

Both games are 3v3 turn-based battles wrapped in a lot of menus, plus real-time
minigames that are a separate design problem in each.

| | Stadium 1 | Stadium 2 |
|---|---|---|
| Species available | 151 | 251 |
| Battle rules | Gen 1: one Special stat, no held items | Gen 2: Special split, held items, more status and moves |
| Cups | 4 (Pika, Petit, Poké, Prime) | 4 |
| Gym Leader Castle | 8 Kanto gyms, Elite Four, Mewtwo | 16 gyms across Johto and Kanto, Elite Four, Red |
| Minigames | 9 | 12 |
| Extra modes | Free Battle, VS Mewtwo, GB Tower, Gallery | All of the above plus Academy/quiz modes, GB Tower |
| Rental Pokémon | Yes, no Transfer Pak needed | Yes, no Transfer Pak needed |

For an adapter the menu tree is the first job and the battle reader is the second. A
151-entry rental list with one stat block per Pokémon is a smaller selection screen
than a 251-entry list, and Gen 1 removes held items, the Special split and a chunk of
status handling from the battle model.

## Recommended shape of the work

1. **Build the reader against the RetroArch harness first**, not against OGA. It is the
   only readable N64 channel that exists today, and the address work transfers.
2. **Find the screen state and the menu cursor/list length** for Stadium 1's Rev 2 ROM.
   The `GameState` enum gives the names to look for; the live game gives the addresses.
3. **Rental team selection** before battle: species name, level, stats, moves. This is
   the screen a blind player cannot get through by trial and error.
4. **Battle reader**: both active Pokémon, HP, status, stat stages, the four moves with
   PP, turn order, and the result of each turn. Speak discrete events only; keep HP and
   other continuous values as tones.
5. **Progression**: cup rounds, Gym Leader Castle, save data. Controller Pak saves are a
   separate memory window if the core exposes one.
6. Minigames are out of scope for a first reader. Say so rather than half-doing nine of
   them.

## Risks and open questions

- **Rev 2 offset.** Measured at +0x100 for the state pair. Whether the shift is uniform
  across data and code is unknown; assume it is not until several symbols are checked.
- **No core in OGA.** Until an N64 core lands, `n64_adapter` stays a refusal and none of
  this ships on a phone. The research is still worth doing because the harness proves
  the addresses now.
- **The recomp is not the OGA path.** `PokemonStadiumRecomp` runs natively on desktop and
  targets US 1.0, which Devin does not have. It is useful as a symbol reference at most.
- **Text encoding is unmapped.** Stadium's message text is not plain ASCII in the
  decompilation's evidence so far; the reader needs either the decoded message buffer or
  a table extracted from the ROM. This was not solved in this pass.
