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

## Not yet found

- **Terrain under the cursor.** `MapStateManager` has plausible map buffers at
  `+0x028`, `+0x428` and `+0x830` (each 0x400 bytes = 32×32 — exactly the tile
  grid) and byte offsets `+0x828`/`+0x82C` (pointers). One of these is the terrain
  layer. **The adapter currently says "Terrain: unknown" rather than guess**, since
  a wrong terrain name is worse than an admitted gap.
- **Chapter / map identifier.**
- **Allegiance as a small enum.** Grouping by the `Force*` pointer works and was
  used, but the faction *number* has not been located.
- **Movement and attack ranges.** The game computes these; the map buffers above
  are the likely place to read them from rather than reimplementing the rules.
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
