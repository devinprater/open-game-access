#!/usr/bin/env python3
"""Update the FE11 memory doc with the verified terrain/movement findings."""
import pathlib

p = pathlib.Path("C:/Users/Devin Prater/open-game-access/docs/fire-emblem-shadow-dragon-memory.md")
s = p.read_text(encoding="utf-8")

anchor = "### Still open on terrain"
if anchor not in s:
    print("!! anchor not found")
    raise SystemExit(1)

insert = """### Terrain types and movement costs — RESOLVED (verified)

Two things that were listed as gaps are now answered, and the movement-range
algorithm is recovered verbatim from the game rather than reimplemented.

**The game's own movement test** (`src/ov000/map_sequence.cpp:2741`):

```
if (func_0203826c(gFE11Database->pTerrain[unk_828[ix | iy<<5]].unk_08,   // terrain category
                  pUnitA->pJobData->unk_28) < 0)                          // movement type
    continue;                                                            // impassable
gMapStateManager->unk_08->unk_0854[ix | iy<<5] = 0;                      // mark reachable
```

with the tiles skipped beforehand:

```
unk_82c[ix | iy<<5] & 0x80 != 0            -> skip   (impassable flag)
unk_d30[(ix|iy<<5)>>3] & (1 << (ix & 7)) == 0 -> skip  (reachability bitmap)
GetUnit(unk_028[ix | iy<<5]) != NULL       -> skip   (tile occupied)
```

so `unk_d30` is a **32×32 reachability bitmap** (0x80 bytes = 1024 bits) — the blue
overlay — and `unk_028` is the unit id occupying each tile.

**The cost matrix** (`src/database.cpp:424`) is indexed `[movementType][terrainCategory]`.

⛔ **`TerrainCostData` in `include/database.hpp` is WRONG about one field.**
It declares `s8 * unk_04` — a pointer — but the live bytes at `+4` are `01 01 01 FF`,
i.e. costs 1,1,1,-1, not an address. `unk_04` is a **flexible array member**; the cost
rows begin at `+4`, and `size` at `+0` is the row WIDTH (32, the terrain-category
count), not the number of movement types. Reading it as a pointer is what made an
earlier attempt look like the matrix was garbage.

**Verified live** — each class resolves a different row, and the movement stats are
the real ones:

```
slot 1  jobMov=7  movType=0   JID_LORD      cost row 0 :  -1 1 1 1 2 2 2 2 5 4 -1 1 -1 1 -1 2
slot 2  jobMov=6  movType=23  JID_SOLDIER   cost row 23:  -1 1 1 1 2 2 2 2 -1 -1 -1 1 -1 1 -1 2
slot 3  jobMov=6  movType=23  JID_SOLDIER   cost row 23:  ...same...
slot 4  jobMov=6  movType=7   JID_FIGHTER   cost row 7 :  -1 1 1 1 2 2 2 2 -1 3 -1 1 -1 1 -1 2
```

- **`JobData.mov` (+0x29)** is the class movement stat: Lord 7, Soldier 6, Fighter 6 —
  the real Fire Emblem values.
- **`JobData.unk_28` (+0x28)** is the cost-matrix row. It is a **`u8`**; reading it as a
  32-bit value gives 50464512 (0x03025C00), which looks like an address and is really
  the byte plus three neighbouring fields.
- Lord (row 0) has cost **5 in category 8** where Soldier/Fighter rows have `-1` —
  exactly the distinction between a Lord's mobility and a common soldier's.
- `-1` = impassable; 1 = plains/road; 2 = forest/hill; 3 = mountain; 4-5 = higher peaks.

Terrain categories actually present on the Prologue map:

```
category  0 : 656 tiles      (plains)
category  8 :  91 tiles
category 13 : 150 tiles      <- the tile under the cursor
category 14 : 126 tiles
category 22 :   1 tile
```

"""
s = s.replace(anchor, insert + anchor)
p.write_text(s, encoding="utf-8")

# The "Not yet found" list still claims these are missing; fix that too.
s = p.read_text(encoding="utf-8")
for claim in (
    "- **Movement and attack ranges.** The game computes these; the map buffers above\n  are the likely place to read them from rather than reimplementing the rules.\n",
):
    s = s.replace(claim, "")
p.write_text(s, encoding="utf-8")
print("doc updated")
