# Pokemon Stadium 1 desktop reader (prototype)

Reads the live game through RetroArch's MCP server and speaks screen and menu
changes through NVDA. Input still goes to the game; the reader only observes.

This is a research harness for the OGA N64 adapter. It runs on Windows against
RetroArch's `mupen64plus_next` core, which declares RDRAM at `0x80000000` and
answers `READ_CORE_MEMORY`.

## Run it

1. Start RetroArch with the MCP config and the Stadium 1 ROM:

   ```
   run-ra.cmd
   ```

   That uses the RetroArch nightly under `Downloads\ra-mcp-test\RASETUP`, the
   hand-written config beside it, the MCP server on port 54123, and the ROM in
   `Dropbox\games\N64`.

2. In a second terminal:

   ```
   python reader.py
   ```

Speech goes through the NVDA controller client. If NVDA is not running, the
same lines print to the console.

## What it speaks

- The screen name when it changes: title, area select, cup select, Stadium
  sub-menus, rental list.
- Area select: the highlighted facility.
- Cup select: the highlighted cup with its level range.
- Rental list: the highlighted Pokemon and its Pokedex number, in dex order.

Every value must read the same on four consecutive polls before it is spoken,
so a transient during a screen load cannot become speech.

## Measured addresses (Pokemon Stadium USA Rev 2)

| Value | Address | Notes |
|---|---|---|
| `gCurrentGameState` | `0x800757A8` | 2 title, 4 menu select, 0x10 area select, 0x20 Stadium flow |
| Area cursor (s16) | `0x80117EFA` | 0 Stadium, 1 Free Battle, 2 Gym Leader Castle, 3 VS Mewtwo, 4 Kids Club, 5 Victory Palace, 6 Pokemon Lab, 7 GB Tower, 8 back |
| Area cursor, previous | `0x80117EF8` | |
| Cup cursor (u32) | `0x8008157C` | 5 Poke, 2 Pika, 1 Prime, 3 Petit; 6 cancels |
| Fragment table (`gFragments`) | `0x800A58F0` | 240 entries, 8 bytes each: vaddr, size |
| Cup-screen fragment id | 87 | Its presence separates cup select from the other `0x20` screens |
| Team-select fragment id | 88 | Rental list and the stat card |
| Rental cursor struct | team base + `0x168D2` | scroll `u8` at +3, row `s16` at +0xA, column `s16` at +0xC |

The decompilation targets US 1.0 and its addresses are shifted by `+0x140` here.
The area cursor sits inside fragment37's heap-allocated overlay: the reader
assumes the base measured on this route (`0x80114D20`) and validates the value
range. Locate the fragment by scanning for its ten-pointer table at fragment
offset `0x2FBC` if the base ever moves.

### Rental grid math

The list is 3 columns by 4 visible rows in Pokedex order. Scrolling past the
bottom row adds one to the scroll byte while the row field stays on the bottom
row, so the absolute index is:

```
index = (scroll + row) * 3 + column
species = index + 1        # verified: index 14 = Beedrill, 17 = Pidgeot
```

The fragment base moves between screens, so read it from `gFragments[88]` every
time instead of hardcoding it. When the team fragment is not loaded the entry
is absent, which is what separates the rental screen from the cup screen.

## Limits

- The stat card's numbers and move names are **not in RAM**. Exact-value scans
  for Bulbasaur, Metapod and Pidgeot stats and moves returned zero hits; the
  card computes them from ROM tables at draw time. Speaking them needs a
  pre-baked per-species table extracted from the ROM, which is the next step.
- The intermediate Stadium sub-menus (round select, Battle/Rules/Registration,
  entry source) are named only as "Stadium menu"; their cursors are unmapped.
- The rental order is assumed to be Pokedex order for all 151 entries. That is
  verified for the Poke Cup list; a filtered cup may break the index mapping.
- No battle reads yet.

## Scripting notes

- Hold a d-pad press for 4-6 frames. A 20-frame hold lets the game's key repeat
  fire and moves two cells.
- `ps.py` is the client: `status`, `frames N`, `press BUTTON [hold] [settle]`,
  `read ADDR N`, `u32 ADDR`, `dump FILE`, `state`, `save N`, `load N`.
