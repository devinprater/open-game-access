# Fire Emblem: Shadow Dragon (USA, `YFEE`) — memory map

Every entry states how it was verified and how confident it is. Nothing here is
presented as confirmed on the strength of a plausibility argument alone.

**ROM under test:** `Fire Emblem - Shadow Dragon (USA).nds`, game code `YFEE`,
maker `01`, 64 MB. This matters: the published Action Replay lists are
region-specific and Shadow Dragon has US/EU/JP releases with different layouts.

**Primary source:** the `Eebit/fe11-us` decompilation, which ships
`config/YFEE01/arm9/symbols.txt` (~9,000 named function and data symbols with real
addresses) and the game's class layouts in `include/*.hpp`. This is the difference
between reverse engineering a binary and reading a specification — all symbol
addresses below are quoted from that file, not derived.

## Boot behaviour (measured)

- Boots correctly with FreeBIOS + generated firmware → direct boot. Real
  BIOS/firmware is not needed and is known to hang the BIOS path.
- Renders and animates: `VRAMCNT_A = 131`, 296 distinct colours, 8/12 frame
  samples changing.
- The game opens with a **Prologue cutscene**, not a map. `gMapStateManager` is
  NULL until a map loads. Any experiment that skips this step measures nothing —
  see "Method notes" at the end.

## Confirmed: cursor

| Field | Location | Verified by |
|---|---|---|
| `gMapStateManager` | `0x021E3328` (bss) | symbol `config/YFEE01/arm9/ov000/symbols.txt:2224` |
| `MapStateManager.cursor` | `+0x010` | `include/map.hpp`, `class MapStateManager` |
| `Cursor.xTile` | `+0x008` (u8) | see below |
| `Cursor.yTile` | `+0x009` (u8) | see below |
| `Cursor.isVisible` | `+0x00A` (u8) | reads 1 on a map |
| `Cursor.xDisplay` | `+0x004` (s16) | pixel position, see below |
| `Cursor.yDisplay` | `+0x006` (s16) | pixel position |

**Confidence: high.** Two independent confirmations:

1. **Direct correlation.** The cursor object was identified at a stable address
   across seven snapshots. Moving the D-pad changed `+0x08`/`+0x09` and nothing
   else in the object; the object's address did not change.
2. **Internal consistency.** `+0x04`/`+0x06` are signed 16-bit pixel positions, and
   dividing by the camera's `tileSize` (24, read from `Camera + 0x0C`) reproduces
   `+0x08`/`+0x09` exactly at every sample:

   | snapshot | xDisplay/24 | xTile | yDisplay/24 | yTile |
   |---|---|---|---|---|
   | c0-base | 1 | 1 | 20 | 20 |
   | c2-right | 7 | 7 | 20 | 20 |
   | c3-down | 7 | 7 | 21 | 21 |
   | c5-up | 4 | 4 | 18 | 18 |
   | c6-up3 | 4 | 4 | 9 | 9 |

   A coincidence cannot survive that relationship across five snapshots.

**⚠ A trap that cost a run:** the observed step size was sometimes +3 rather than
+1. That is not scaling — it is **D-pad auto-repeat**. An 18-frame hold is long
enough for the game to repeat the movement. A single-tile test needs a hold of
roughly 4–8 frames. Anyone re-running this experiment should expect that and not
conclude the coordinate is scaled.

## Confirmed: the unit array

| Field | Location | Notes |
|---|---|---|
| `gUnitList` | `0x021974D8` (bss) | symbol `symbols.txt:8640` |
| `gForces` | `0x021974DC` (bss) | symbol `symbols.txt:8641` |
| record stride | **`0xA8`** | **measured, not assumed** |
| slot count | 24 reachable | slots beyond that leave the 4 MiB window |

**`gUnitList` is an ARRAY BASE, not a linked-list head.** The decompilation is
explicit: `inline Unit * GetUnit(s32 unitId) { return gUnitList + unitId - 1; }`
(`include/unit.hpp`) — 1-based indexing, so `slot 1` is the first real unit.

**The stride is 0xA8, and that was determined empirically.** `sizeof(Unit)` from
the class layout is 0xA4, which is *wrong for the runtime allocation* — reading at
0xA4 strides produces records that look almost right and drift. A plausibility
score over the first 8 records at each candidate stride gave:

```
stride 0x9C -> 14/32      stride 0xA4 -> 25/32      stride 0xB0 -> 24/32
stride 0xA0 -> 20/32      stride 0xA8 -> 32/32  <-- chosen
```

32/32 is the only one where every record has a plausible level, HP and in-map
coordinate. **Lesson: do not trust `sizeof` from a header for an array the game
allocates itself; score candidate strides against the live data.**

### Unit field offsets (relative to the record)

| Offset | Field | Size | Verified |
|---|---|---|---|
| `+0x40` | `PersonData *` | 4 | points into RAM; `*(+0x40)` is the `pid` string pointer |
| `+0x44` | `JobData *` | 4 | points into RAM; `*(+0x44)` is the `jid` string pointer |
| `+0x4C` | `Force *` | 4 | `Unit.force` per `include/unit.hpp` |
| `+0x6A` | level | u8 | reads 1 for the lead unit on the first map |
| `+0x6C` | **HP** | s8 | reads 18; matches the published AR code offset `2000006C` |
| `+0x6D` | movement | s8 | reads 0 at this point (not yet populated) |
| `+0x6E` | **x position** | s8 | reads 1 |
| `+0x6F` | **y position** | s8 | reads 20 |
| `+0x70` | items[5] | 10 | first item reads `0x000A` |
| `+0x98` | `state1` | s32 | see below |

**Character identity, confirmed by reading the strings:**

```
slot 1 @ 0x02275330   level 1  HP 18  at (1,20)
  PersonData 0x0225B734 -> pid 'PID_MARS'
  JobData    0x02262C14 -> jid 'JID_LORD'
```

`PID_MARS` / `JID_LORD` is Marth as a Lord. **This is read from the game's own
identifier strings**, so it is exact rather than inferred — and it agrees with the
map: the lord starts on the first map, and the cursor was on that same tile.

### ⚠ `state1` does NOT mean what the decompilation guesses

The decompilation lists `US_ACTED = 1<<0` and `US_NOT_PRESENT = 1<<12`. The live
lead unit's `state1` is **`0x04001002`** — i.e. bit 12 (supposedly "not present")
is SET on a unit that is demonstrably present and alive at the cursor, and bit 1
is set on a unit that has not acted.

**Consequence: do not filter units on these bits.** Doing so reported "0 live
units" for a map with an army on it. The current adapter filters on plausibility
(there is no such thing as a unit with 0 HP *and* at 0,0 — that is an empty slot).

**Not yet determined:** which bit actually tracks "has acted". Reading `+0x98`
for a unit before and after acting, across a turn boundary, is the experiment that
settles it. Until then the adapter must not claim to know.

## Terrain — RESOLVED (verified)

The question "which of these buffers is the terrain layer?" is answered. All three
candidates were inspected; two are not terrain at all, and the tile source is a
*pointer*, not an inline array.

```
MapStateManager (include/map.hpp)
  +0x028  u8 unk_028[0x400]   inline 0x400 buffer — reads ZERO, not terrain
  +0x428  u8 unk_428[0x400]   inline 0x400 buffer — reads ZERO, not terrain
  +0x828  u8 * unk_828        POINTER to the raw tile-id array  <-- the tile source
  +0x82C  u8 * unk_82c        POINTER (used by MapStateManager::tst())
  +0x830  u8 unk_830[0x400]   derived terrain CATEGORY per tile
```

The chain (fe11-us):

```
src/ov000/map_state.cpp:697
    u8 tile = unk_828[x | (y<<5)];
    unk_830[x | (y<<5)] = GetTerrainCategoryDBIndex(pTerrain[tile].unk_08);
include/database.hpp   FE11Database.pTerrain +0x20,  .unk_24 +0x24
src/database.cpp:419   GetTerrainCategoryDBIndex(p) = (p - db->unk_24) / 4
```

⛔ **`unk_828` and `unk_82c` ARE POINTERS.** Reading `msm+0x828` as tile data reads
the pointer's own little-endian bytes and yields plausible garbage — tiles 48, 106,
38 instead of 14. Always dereference.

**The mapping is self-checking**, which is why it can be called verified rather than
assumed: `(pTerrain[tile].unk_08 − db->unk_24) / 4` must equal the category the game
itself stored in `unk_830`. Live at the cursor, both give **13** with tile **14**.

### Terrain types and movement costs — RESOLVED (verified)

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

### Still open on terrain

- **No category→name table located.** `pTerrain[tile].unk_04` points at `"BBG01"` /
  `"BBG02"` — a *background graphic* name, not a terrain name — and
  `db.unk_24[category]` is a null pointer. So the game's own data does not hold
  words like "Plains" for this; the adapter reports the category number and says it
  is a number. Naming it is a separate enhancement, not a blocker.

## Allegiance — RESOLVED (verified)

Allegiance is read from `Force.id`, not inferred by comparing `Force*` pointers.

```
src/force.cpp
    struct Force * gForces = NULL;
    gForces = new Force[6];       // ARRAY OF STRUCTS, stride 0x0C — not Force*[]
    gForces[i].Init(i);           // therefore Force.id == the array INDEX

Force { Unit* head +0x00, Unit* tail +0x04, s32 id +0x08 }   (include/unit.hpp)
Unit.force  +0x4C  -> the Force this unit belongs to
```

⛔ **`gForces` is not an array of pointers.** Dereferencing each element reads a
struct's first field as a pointer and yields `id=6558208`, `head=FFFF0008`, and
`force[0] == a unit address` — all plausible-looking and all wrong.

Faction meanings, from the call sites in `src/ov000/disposition.cpp`:

| id | meaning | evidence |
|---|---|---|
| 0 | player | live: Marth reads 0 |
| 1 | enemy | `Force::Get(1+2)` is the opposing force |
| 2 | player (scenario) | `Force::Get(2)` = the player army; `faction + 2` |
| 3 | enemy (scenario) | `faction + 2`, opposing |
| 4 | unassigned reserve | `ResetAllForces()` seeds **every** unit here; `Force::Get(4)` |
| 5 | other | not observed |

Live confirmation on the booted map — every `id` matches its index, and the reserve
holds exactly the 60 unused slots:

```
faction 0 (player)      id=0  head=02275324 tail=02275324  units=1     <- Marth
faction 1 (enemy)       id=1  head=00000000 tail=00000000  units=0
faction 2 (player sc.)  id=2  head=00000000 tail=00000000  units=0
faction 3 (enemy sc.)   id=3  head=00000000 tail=00000000  units=0
faction 4 (unassigned)  id=4  head=022753CC tail=0227527C  units=60
faction 5 (other)       id=5  head=00000000 tail=00000000  units=0
```

**This fixed a real bug.** The previous `Next enemy` used "a different `Force*` than
the leader". That also matches faction 4 — the 60-slot reserve — so it would have
reported phantom enemies on any map where those slots were plausible. It now tests
`faction == 1 || faction == 3`.

## Not yet found

- **Enemy detection — VERIFIED END TO END** (no save file needed).

  The blocker was never the reader. It was input: the Prologue's "Waiting" tutorial
  popup swallows map input, so a plan that never presses B re-reads the popup forever
  and the enemy phase is never reached — which is why enemy counts stayed at zero for
  tens of thousands of frames and looked like "this map has no enemies".

  The popup's own text gives the fix: *"You can also press B or touch the B icon at the
  top of the screen to cancel the move."* Adding a **B press to dismiss the popup**
  after each move unblocks it. Enemies then appear at ~frame 5900 of `fe/plans/tutorial2.txt`.

  Live output, `core/fe_access.cpp` against the USA ROM with no save loaded:

  ```
    slot addr       Lv HP Mov   X   Y  act dead fac name
      1    0x02275324  1 18   0  11  20   0    0    0 Marth               PID_MARS           JID_LORD
      2    0x022753CC  1 16   0   8   3   0    0    1 PID_P01_GRA_SLDR    PID_P01_GRA_SLDR    JID_SOLDIER
      3    0x02275474  2 17   0   9   7   0    0    1 PID_P01_GRA_SLDR_1  PID_P01_GRA_SLDR_1  JID_SOLDIER
      4    0x0227551C  1 14   0  11  11   0    0    1 PID_P01_GRA_SLDR_2  PID_P01_GRA_SLDR_2  JID_FIGHTER
      5    0x022755C4  1 14   0  10  13   0    0    1 PID_P01_GRA_SLDR_3  PID_P01_GRA_SLDR_3  JID_FIGHTER

    Next enemy   -> PID_P01_GRA_SLDR_3, 14 HP, position 10, 13, 7.1 tiles away.
    Where am I?  -> Cursor 11, 20. Terrain category 13 (tile 14, verified). Unit here: Marth, 18 HP, unacted.
  ```

  Confirms in one run: four enemy units read by faction (`fac 1`), by their class
  identifier (`JID_SOLDIER` / `JID_FIGHTER`), with live HP and positions; `Next enemy`
  selecting nearest-first by distance from the cursor; and that enemies and the player
  are distinguished by the game's own faction number rather than by pointer identity.

  The earlier save-file work still stands as a second, independent confirmation (it
  populated forces 2 and 3 with 9 player / 22 enemy units); the Prologue route is the
  reproducible one because it needs no external data. See `docs/save-files.md`.

- **Chapter / map identifier.**
- **Objectives.**

## Method notes (things that went wrong, so they don't again)

- **Diffing RAM was the wrong first tool.** A live DS game rewrites thousands of
  bytes per second; "which bytes changed" is dominated by frame counters, audio and
  RNG. Reading the game's actual structures via the decompilation's symbols found
  the cursor in one run. Use diffs only to confirm a hypothesis, never to find one.
- **Skipping the intro silently invalidates everything.** The first cursor plan
  pressed a few buttons, stayed in the Prologue cutscene, and reported a NULL
  cursor for all seven snapshots — which looks like a bug in the reader.
- **A harness that doesn't rebuild runs the previous revision.** `fe-run.sh` now
  rebuilds unconditionally; before that, a "successful" run printed output from the
  old source and the new instrumentation was simply absent.
- **Bracket every RAM snapshot with a screenshot.** A snapshot says what memory
  held; only the picture says what the game was *doing*. The intro-skip problem
  above was diagnosed in seconds because the screenshots showed a cutscene.
