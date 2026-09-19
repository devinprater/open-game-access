# TASK4 — Dissidia story-board state (static analysis)

All addresses below are ELF vaddrs. PSP RAM address = vaddr + `0x08804000` only for static image addresses; heap objects must instead be reached through the stated holder. No emulator or ROM was used.

## Executive result

The region/story-board owner is `[[0x00394940]]` (the same battle/region manager independently established by the pause-menu work). Its board-data service is `[[0x00394900]]`; persistent story progress is rooted at `[[0x00395338]]`.

The observed word at RAM `0x08C0403C` is **VERIFIED as the 32-bit HUD/cache copy of DP**, reached as:

```text
M = [RAM 0x08B98940]                 // [vaddr 0x00394940]
U = [M + 0x56C]                     // region HUD/work object
DP_cache = U + 0xD78                // s32
```

Using the previously observed live `M = 0x08C168F0`, this implies `U = 0x08C032C4` and `U+0xD78 = 0x08C0403C`, exactly the first hunted word. `0x08C0413C` is `U+0xE78`; it is **not** the field read by the DP formatting code and is rejected as the canonical DP address. It is an adjacent HUD/work value that happened to mirror the transition; its precise rendering role is not established statically.

The authoritative DP is a **signed 16-bit field** at `progress_record+0x06`, not either word:

```text
G = [RAM 0x08B99338]                 // [vaddr 0x00395338]
C = G + 0x1AE80 + chapter_id*0xF74
slot = C[+0x02]                      // active sub-record, u8
R = C + slot*0x314 + 0x08
DP = *(s16 *)(R + 0x06)
```

`FUN_001c52cc` (`0x001C52CC`) proves the chapter stride/base; `FUN_001c52f8` (`0x001C52F8`) proves the slot stride and `+0x08` record header. `FUN_001d42e8` (`0x001D42E8`) reads `s16 [R+0x06]`, sign-extends it, clamps values below `-9`, and writes `s32 [U+0xD78]`. The same function reads `s16 [R+0x08]` into `[U+0xD74]`; that field is the second allowance/count used to build the row of movement markers, not DP.

## 1. DP variable and writers

### HUD read/format path — VERIFIED

`FUN_001d42e8` is the decisive formatter/initializer:

- obtains the current chapter at manager `M+0x120`;
- calls `FUN_001c52cc(…, chapter)`;
- reads the active slot byte at chapter record `+0x02`;
- calls `FUN_001c52f8(…, chapter, slot)`;
- copies signed halfword `R+0x06` to word `U+0xD78`;
- splits `[U+0xD78]` into decimal digits in `FUN_001d4918` (`0x001D4918`) using `/10` and `%10`, writing the two HUD digit sprites at `U+0x94` and `U+0x98`.

This is stronger than a string xref. `"Destiny Points"` itself is language-pack text and is absent from the executable image, so it has no static code xref. The digit formatter supplies the required proof.

### Writes

| Field | Writer(s) | Rank and meaning |
|---|---|---|
| `s16 [R+0x06]` | `FUN_001ca7ec` at `0x001CA7EC` | **VERIFIED writer.** Resolves the current persistent record via the same save/progress service, subtracts its argument, clamps to `[0, max-1]`, stores the halfword, then updates the cached derived quantity. This is the spend/reduction primitive. |
| `s32 [U+0xD78]` | `FUN_001d42e8` at `0x001D42E8`; `FUN_001ca124` at `0x001CA124`; initialization path inside `FUN_001e25fc` at `0x001E25FC`; zeroing paths in `FUN_001d42e8`/`FUN_001d4e10` | **VERIFIED cache writers.** `0x001CA124` is a direct setter used by board transitions and clamps to `-9`. These write the presentation/work copy, not the saved halfword. |
| restore of `s16 [R+0x06]` | no independent incrementing writer isolated | **CANDIDATE/UNRESOLVED.** Static evidence found the subtracting commit primitive but not a distinct undo/restore primitive. Do not claim one without a write watch on `R+0x06`. |

Thus, for an adapter, prefer the rediscoverable authoritative halfword `R+0x06`; the easier board-only address is the verified cache `[M+0x56C]+0xD78`.

## 2. Board structures

### Region manager `M = [[vaddr 0x00394940]]` — VERIFIED

| offset | size | role/evidence |
|---:|---:|---|
| `+0x118` | 4 | active board/piece object pointer; extensively passed to `FUN_001becac`, `FUN_001bf…`; its byte `+0x40` is the current story/tile class used by board logic. |
| `+0x120` | 1 | active chapter/board id used to select the persistent `0xF74` record and board assets. It is **not proven to be the moving cursor id**. |
| `+0x234` | embedded | pause list widget (preserved result from TASK3). |
| `+0x55C` | 4 | pointer to a board mapping table; `FUN_001cad14` indexes it using `chapter-map-index*0x15 + movement_index + 0x2A`. |
| `+0x56C` | 4 | region HUD/work object `U`. |

### Persistent chapter/progress records — VERIFIED

```text
root holder:       vaddr 0x00395338
chapter array:     G + 0x1AE80
chapter stride:    0xF74
sub-record count:  5
sub-record stride: 0x314
sub-record base:   chapter + slot*0x314
active slot:       chapter + 0x02 (u8)
logical R:         sub-record + 0x08
R+0x06:            DP (s16)
R+0x08:            movement-marker/count field (s16)
R+0x114:           32 entries of stride 0x10 (flags/state; initialization proven by `0x001C5070`)
```

### Coordinate tile table — VERIFIED generic board table, CANDIDATE as highlight table

The table object accepted by `FUN_001c5bb4` (`0x001C5BB4`) is:

```text
T+0x00  backing/count object
T+0x04  node-array pointer
node stride 0x10
node+0x02  signed x/grid coordinate
node+0x03  signed y/grid coordinate
node+0x0C  flags (bits 0,1,2,4 exclude selection)
count = FUN_001c4740([T+0])
```

`FUN_001c5bb4(T,x,y)` linearly finds the node whose coordinates match and whose blocking bits are clear. `FUN_001c5db0(T,id)` returns `T.nodes + id*0x10`. This is a real logical tile table, unlike the rejected `0x096BBxxx` render graph. The exact manager field holding the live `T` for the glowing highlight remains a **candidate**, because several gameplay subsystems use this shared table type.

No fixed array base/count can honestly be given: the array is heap-backed and rediscovered from an owner pointer. Any answer presenting `0x096BBxxx` as that base is rejected by its matrix/render-node layout.

## 3. Movement validation

The code-proven primitive is `FUN_001c5bb4` at `0x001C5BB4`: proposed `(x,y)` is accepted only if a `0x10`-byte node exists and flags `0x01|0x02|0x04|0x10` are all clear. It reads the heap table `[T+0x04]`; it does **not** read a four-entry adjacency list.

Therefore the best static answer to right-down-right is **coordinate lookup plus per-node blocking flags**, not a simple free cursor ordinal and not a proven directional adjacency graph. Direction handlers call this lookup after changing x/y; a failed right means the coordinate to the right either has no node or its node has one of the blocking bits. Moving down can place the cursor on a row where the next right coordinate exists and is unblocked.

Rank: the table format and lookup are **VERIFIED**; naming the particular call site that consumes the live D-pad event as the single board-cursor validator is **CANDIDATE**, because static call sites include field, placement, and battle-board users and no live PC trace was allowed.

## 4. Confirmed-move commit

The statically supported commit chain is:

1. board state machines in the `0x001Dxxxx` range consume a selected movement index held at `U+0xD78`;
2. `FUN_001cad14` (`0x001CAD14`) maps that index through `[M+0x55C]` and the current piece/story byte `[M+0x118]+0x40`;
3. `FUN_001cad64` (`0x001CAD64`) resolves that mapping to a concrete board node/object id;
4. `FUN_001bf078` commits the resolved id to the active board/piece object;
5. `FUN_001ae3b4` records the chosen mapping in persistent story state;
6. `FUN_001ca7ec` is the code-proven subtract/write primitive for the authoritative halfword at `R+0x06`.

`FUN_001da298` (`0x001DA298`), particularly states `0x15–0x16`, is the best **CANDIDATE confirmed-move state machine**: it calls `FUN_001cad14`, `FUN_001cad64`, `FUN_001bf078`, and `FUN_001ae3b4` together and then advances animation/state. Static analysis does not justify claiming that the observed single direction press enters this path directly rather than via a prior selection state.

The three converging live words (`0x096C0540`, `0x096C0B90`, `0x096C2560`) are **CANDIDATE render/animation node ids**, not canonical tile state: they lie in the already identified scene/render allocation band and no logic-level writer or stable owner chain was found for them.

## Ranked live tests (minutes, not hunts)

1. **VERIFY DP cache (highest value):** read `[0x08B98940] -> M`, then `[M+0x56C] -> U`; require `U+0xD78 == 0x08C0403C` in the recorded boot. Expected: `s32` equals HUD DP and changes `1→0`; `U+0xE78` (`0x08C0413C`) may mirror but is not read by the formatter.
2. **VERIFY authoritative DP:** read `[0x08B99338] -> G`; get `chapter=*(u8 *)(M+0x120)`, `C=G+0x1AE80+chapter*0xF74`, `slot=*(u8 *)(C+2)`, `R=C+slot*0x314+8`. Expected: `s16 [R+6]` is `1`, then `0`, and remains the source from which reopening/rebuilding the HUD repopulates `U+0xD78`.
3. **WRITE-PC confirmation:** watch `R+6` for writes. Expected spend PC is within/calls `0x001CA7EC`; watch `U+0xD78` separately and expect refresh PCs `0x001D42E8`, `0x001CA124`, or the `0x001E25FC` initialization path. This separates authoritative state from HUD cache in one move.
4. **Reject the twin:** watch RAM `0x08C0413C` and break on read. Expected: no `/10,%10` DP digit path; its writer/readers should remain HUD/work logic only.
5. **Tile lookup:** break at `0x001C5BB4` during a display-verified cursor press; record `a0=T,a1=x,a2=y`, then inspect `[T+4]`, count, and returned id. On blocked right expect `-1`; after down, right should return a nonnegative id. This directly tests the coordinate/flags explanation.
6. **Piece commit:** break on `0x001BF078`, `0x001AE3B4`, and `0x001CA7EC` during a DP-positive move. Expected: all occur only on the confirmed piece move, not on free DP-zero highlight movement.
7. **Reject traps:** leave `0x09B3FED8` and the four camera floats out of the adapter unless a write PC ties them to the above owner chain. Their existing evidence is timer/camera behavior, not logical cursor state.

## Final ranking

- **VERIFIED:** manager holder `0x00394940`; progress root holder `0x00395338`; HUD/work pointer `M+0x56C`; DP cache `U+0xD78`; authoritative DP `s16 R+0x06`; progress strides `0xF74/0x314`; DP formatter `0x001D42E8`; digit path `0x001D4918`; subtract writer `0x001CA7EC`; coordinate-node lookup `0x001C5BB4`, stride `0x10`, coordinate/flag fields.
- **CANDIDATE:** `0x001DA298` as the observed confirmed-move state machine; the specific live `T` owner and highlight id; exact semantic name of `U+0xE78`; the three `0x096Cxxxx` words as animation ids.
- **REJECTED:** `0x0004013C` as canonical DP; `0x09B3FED8` as cursor; `0x096BBxxx` as logical board structs; camera-Y floats as cursor ids; a plain wrapping RAM ordinal model for the board cursor.
