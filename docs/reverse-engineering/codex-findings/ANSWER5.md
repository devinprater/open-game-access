# TASK5 — board cursor owner and direction-press commit (static analysis)

All addresses below are ELF vaddrs. For an image/static address only, PSP RAM is `vaddr + 0x08804000`. No emulator or ROM was used.

## Executive result

The board table and cursor are reached through the active board object at `M+0x118`:

```text
M = [RAM 0x08B98940]                       // [vaddr 0x00394940]
P = [M + 0x118]                            // active board/piece controller
B = [P + 0x04]                             // board-data bundle
T = [B + 0x0C]                             // live coordinate-node table
D = [B + 0x10]                             // board input/state dispatcher object

highlight_x = *(u8 *)(D + 0x194)
highlight_y = *(u8 *)(D + 0x195)
highlight_id_cache = *(s16 *)(D + 0x1A0)   // -1 or result of lookup

committed/origin_x = *(u8 *)([D + 0x38] + 0x02)
committed/origin_y = *(u8 *)([D + 0x38] + 0x03)

node_count = *(u16 *)[T + 0x00]            // FUN_001c4740([T+0])
nodes = [T + 0x04]                         // stride 0x10
```

This chain is **VERIFIED** by the accessor and tick path: `FUN_001c218c(P)` calls `FUN_001c418c([P+4])`; `FUN_001c418c(B)` returns `[B+0x10]`, which is passed to `FUN_001b97c8(D)`. The sibling accessor `FUN_001c4184(B)` returns `[B+0x0C]`, and board code passes that value to `FUN_001c5bb4`/`FUN_001c5db0`. Inside `FUN_001b97c8`, the same table is cached at `D+0x40` and is repeatedly passed to those lookup functions.

The ordinary move is not cursor-then-confirm. In state/case `0x27`, a direction changes `D+0x194/+0x195`, resolves the coordinate through `[D+0x40]`, and conditionally calls `FUN_001b6084(D,-1)` immediately when the highlight leaves the saved/home coordinate. Returning to that coordinate calls it with `+1`. The third site, also `+1`, belongs to cancel/back restoration, not a direction press.

## 1. Live `T` owner and node count

### Exact owner — VERIFIED

```text
T = [ [ [M + 0x118] + 0x04 ] + 0x0C ]
```

Equivalently, with named intermediates, `P=[M+0x118]`, `B=[P+4]`, `T=[B+0x0C]`.

Evidence:

- `FUN_001c4184(B)` at `0x001C4184` is exactly `return *(u32 *)(B+0x0C)`.
- `FUN_001c218c(P)` at `0x001C218C` obtains `D=FUN_001c418c([P+4])` and ticks `FUN_001b97c8(D)`.
- `FUN_001c418c(B)` at `0x001C418C` is exactly `return *(u32 *)(B+0x10)`.
- Other methods on the same `P` call `FUN_001c4184([P+4])` and pass the result directly to `FUN_001c5db0`, proving that `[B+0x0C]` is the live table, not a render object.
- In `FUN_001b97c8`, all live highlight lookups use `[D+0x40]`. This is the dispatcher's cached copy of the same board table; the owner chain above is preferable because it comes from the board bundle's typed accessor.

The count chain is:

```text
count_object = [T + 0x00]
node_count = FUN_001c4740(count_object) = *(u16 *)count_object
```

`FUN_001c5bb4` calls `FUN_001c4740(*T)` on every scan and reads nodes from `[T+4] + id*0x10`.

### Prologue-board live test

Starting from RAM `0x08B98940`, every intermediate above should be a valid PSP user-RAM pointer. Expected table invariants are: `node_count > 0`; `[T+4]` is valid; each node's signed coordinate bytes are at `+2/+3`; and the node at `highlight_id_cache` (when nonnegative) has coordinates equal to `D+0x194/+0x195`. The static image does not contain the run-specific heap pointers or a trustworthy fixed prologue node count, so no numeric heap address/count is fabricated here.

## 2. The three `FUN_001b6084` sites

All three are in `FUN_001b97c8`'s board state `0x27`, but they are not equivalent.

| call PC | delta | context | rank |
|---|---:|---|---|
| `0x001BB8BC` | `+1` | Cancel/back input (`DAT_00377618/1C` test). If the live committed coordinate in `[D+0x38]+2/+3` differs from the saved progress coordinate (`FUN_001ae57c/594`), it refunds one, clears the saved movement flag with `FUN_001ae5cc(...,0)`, restores the saved x/y into `[D+0x38]+2/+3`, and updates the two world-position objects. | **VERIFIED restore/refund path; not a direction press.** |
| `0x001BC344` | `+1` | After cursor processing, `D+0x194/+0x195` equals the saved/home coordinate and `FUN_001ae5c4(...) == 1`; clears that flag and refunds one. | **VERIFIED direction reconciliation/refund.** Fires when a direction returns the highlight to home/origin. |
| `0x001BC394` | `-1` | After cursor processing, `D+0x194/+0x195` differs from saved/home and `FUN_001ae5c4(...) == 0`; sets the flag and subtracts one. | **VERIFIED direction spend site.** This is the site expected on the prologue board's first legal direction away from home (`1 -> 0`). |

The MIPS delay slots independently prove the constants: `ori a1,zero,1` at `0x001BB8C0` and `0x001BC348`, and `li a1,-1` at `0x001BC398`.

These calls toggle cost only on crossing the home/non-home boundary. Further directions while already away do not repeatedly subtract: the saved flag is already one. Thus none of these three is a later CONFIRM commit. `0x001BC394` spends as part of the direction update; `0x001BC344` reverses that spend on an inverse return; `0x001BB8BC` is the cancel/back restoration path.

For completeness, `FUN_001bffa8` has a distinct `FUN_001b6084(...,-2)` call for another board event. It is not one of the three queried dispatcher sites and is **REJECTED** as the ordinary one-step direction spend.

## 3. Cursor position variable

### Current highlight — VERIFIED

```text
D = [ [ [M + 0x118] + 0x04 ] + 0x10 ]
x = *(u8 *)(D + 0x194)
y = *(u8 *)(D + 0x195)
id = *(s16 *)(D + 0x1A0)
```

This is adapter-readable persistent state, not a transient render calculation. Direct evidence includes:

- lookup call `0x001BC918`: `FUN_001c5bb4([D+0x40], D[0x194], D[0x195])`, then `sh v0,0x1A0(D)`;
- the same tuple at `0x001BCAA4` and `0x001BCAE4` in adjacent board states;
- state `0x27` invokes `FUN_001b8728(D, [D+0x38][2], [D+0x38][3])` to find a legal neighboring node, then the subsequent state logic consumes `D+0x194/+0x195` as the live highlight;
- candidate coordinates at `D+0x196/+0x198` are separately resolved at `0x001BD454`, showing those halfwords are transient/preview coordinates, not the ordinary current highlight.

`D+0x1A0` is only a cached lookup result and can be `-1`; x/y are the stable primary values. To verify on the prologue board, require `x` and `y` to be small grid coordinates and confirm that `FUN_001c5bb4(T,x,y)` would return `id`. A legal press should change exactly one coordinate by one grid unit (subject to the game's lookup/selection rules), and the exact inverse should restore both bytes.

### Related coordinate sets

- `[D+0x38]+2/+3`: **VERIFIED committed/current-piece or home-origin coordinate** used as the starting point for neighbor search and compared with persistent saved coordinates.
- `D+0x196/+0x198`: **VERIFIED alternate candidate/preview coordinates**, resolved through `T` in state `0x19` and at call `0x001BD454`; not the normal free-highlight pair.
- world-coordinate floats and render nodes: **REJECTED as canonical cursor state**, because lookup and movement logic directly persist and consume the byte pair above before rendering.

## 4. Home-area YES/NO prompt

The two-option confirmation consumer is the `FUN_00267CB8` family, not one of the board manager's six generic `FUN_00250538` call sites.

### Consumer mechanics — VERIFIED

`FUN_00267740` constructs a two-item list at `object+0x12C4`:

```text
FUN_002501ac(list, 0x3B, -1, 0)
FUN_002501ac(list, 0x3A, -1, 0)
```

`FUN_00267CB8` then reads that list with `FUN_00250538(object+0x12C4)`. A selected tag of `0x3A` takes the affirmative branch (`0x2711` sound, sets `object+0x159A=1`, calls `FUN_00267A2C`); every other returned tag takes the negative sound branch (`0x2712`). Therefore tag `0x3A` is the **VERIFIED affirmative/YES item** of this confirm family, with `0x3B` the other item.

The board-local calls at `0x001BA3A4` and `0x001BA5F0` instead read the pause/list widget at `M+0x234`; their constructed/handled tags include `9`, `0x13`, `0x14`, and the `8..0x39` switch range. They do not construct the `0x3A/0x3B` two-item list and are **REJECTED** as the home-area YES/NO widget.

### Prompt identity and ownership — CANDIDATE

Static code proves that the `FUN_00267CB8` object owns and consumes the `0x3A/0x3B` confirmation, but the executable alone does not expose the localized sentence "Make this area your home area?" or a direct call edge from `D`: `FUN_00267CB8` is callback/root-reached and its owner is held by static `DAT_003982F0` (RAM `0x08B9C2F0`), not by `M` or `U`. Accordingly:

- `FUN_00267CB8`/tag `0x3A` as the displayed home-area prompt consumer: **CANDIDATE (strong; exact two-choice affirmative behavior proven, localized text association not code-proven)**.
- The prompt object as a neighbor/field of the cursor dispatcher `D`: **REJECTED**. It is a separate global modal/system object.
- The nearest code-proven board-side neighbor remains the board-local menu at `M+0x234`, but it is not this prompt.

## Static holder/RAM summary

Only the holder has a fixed RAM address:

```text
vaddr 0x00394940 -> RAM 0x08B98940   // M holder
vaddr 0x003982F0 -> RAM 0x08B9C2F0   // global confirm-family object holder/value
```

Adapter discovery for the board should use:

```text
M = read32(0x08B98940)
P = read32(M + 0x118)
B = read32(P + 0x04)
T = read32(B + 0x0C)
D = read32(B + 0x10)
count = read16(read32(T + 0x00))
x = read8(D + 0x194)
y = read8(D + 0x195)
id = readS16(D + 0x1A0)
```

On the prologue board, expected invariants are `M/P/B/T/D != 0`, `count > 0`, `id == -1` only during a nonresolved transition, and otherwise `nodes[id].x == x`, `nodes[id].y == y`. On the first legal direction away from home, expect `FUN_001b6084` PC `0x001BC394`, delta `-1`, and DP `1 -> 0`; on the exact inverse, expect PC `0x001BC344`, delta `+1`, DP `0 -> 1`.

## Final ranking

- **VERIFIED:** live table chain `M+0x118 -> P+4 -> B+0x0C`; node count via `u16 *[T+0]`; dispatcher sibling at `B+0x10`; live highlight bytes `D+0x194/+0x195`; cached id `D+0x1A0`; direction spend at `0x001BC394 (-1)`; inverse refund at `0x001BC344 (+1)`; cancel refund at `0x001BB8BC (+1)`; `0x3A/0x3B` two-option modal construction and `0x3A` affirmative handling.
- **CANDIDATE:** the `FUN_00267CB8` confirmation is specifically the localized home-area prompt (behavior and tag match, but text-to-code provenance is absent).
- **REJECTED:** any fixed heap address for `T`/`D`; render floats as the canonical cursor; `D+0x196/+0x198` as the normal highlight; `M+0x234`'s six `FUN_00250538` uses as the home-area YES/NO consumer; any claim that the ordinary direction waits for a separate confirm before spending.
