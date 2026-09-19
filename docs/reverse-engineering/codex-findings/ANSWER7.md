# TASK7 — board bounds, dense tile source, and the `(6,2)` marker

All addresses below are ELF vaddrs. Add `0x08804000` only to static-image addresses. Heap objects are named only through holder chains. This is static analysis of the existing Ghidra database; no emulator or game image was used.

## Executive result

The previous answers conflated two sibling objects. The corrected board bundle is:

```text
M = read32(0x08B98940)                 // static holder vaddr 0x00394940
P = read32(M + 0x118)
B = read32(P + 0x04)                   // allocated size 0x18

G = read32(B + 0x08)                   // dense terrain-grid facade
T = read32(B + 0x0C)                   // sparse marker/catalog facade
D = read32(B + 0x10)                   // cursor dispatcher

read32(D + 0x3C) == G
read32(D + 0x40) == T
```

This separation is **VERIFIED** by `FUN_001c3ff0` (`0x001C3FF0`). It constructs `B+8`, `B+0x0C`, and `B+0x10`, then calls `FUN_001b8ef4(D,G,T,...)`; that constructor writes its second and third arguments to `D+0x3C` and `D+0x40`. `FUN_001c417c` (`0x001C417C`) is the `B+8` accessor, while `FUN_001c4184` and `FUN_001c418c` are the `B+0x0C` and `B+0x10` accessors.

Thus the one `(6,2)` entry in `T` is perfectly consistent with movement over many unmarked cells: `T` contains sparse interactive markers, while `G` contains one byte for every board cell.

## 1. Bounds and the west edge

### Stored rectangular bounds — VERIFIED

`G` has this code-proven layout:

```text
A     = read32(G + 0x00)               // parsed RMFD grid header
cells = read32(G + 0x04)               // dense mutable cell bytes

width  = read8(A + 0x00)
height = read8(A + 0x01)
cell(x,y) = read8(cells + y*width + x)
```

Evidence:

- `FUN_001c6df4` (`0x001C6DF4`) returns `u8 [A+0]` through `FUN_001c4bb8` (`0x001C4BB8`).
- `FUN_001c6e10` (`0x001C6E10`) returns `u8 [A+1]` through `FUN_001c4bc0` (`0x001C4BC0`).
- `FUN_001c6fa8` (`0x001C6FA8`) computes `x + y*width` and returns the byte at `[G+4] + index` through `FUN_001c7000` (`0x001C7000`).
- `FUN_001b72e4` (`0x001B72E4`) rejects negative coordinates and coordinates `x >= width` or `y >= height` before reading the cell.

The complete dimension bounds are therefore stored as:

```text
left   = 0
right  = width  - 1
top    = 0
bottom = height - 1
```

There are no separately stored per-row minima/maxima in this path.

### Why row 2 stops at x=1 — VERIFIED mechanism; live byte value CANDIDATE

At `(1,2)`, LEFT proposes `(0,2)`. `(0,2)` is inside the rectangular bounds whenever `width > 0`, but `FUN_001b72e4` additionally validates its dense cell byte:

```text
f = cell(x,y)

legal = in_bounds && (
    (f & 0x02) != 0 ||
    (f != 0 && (f & (0x04 | 0x08 | 0x20)) == 0)
)
```

The call used by ordinary directional input is at `0x001BBF7C` in `FUN_001b97c8`. If this test fails, the candidate cursor bytes are discarded before the sparse marker lookup.

Consequently, the observed west edge is stored at exactly:

```text
west_test_byte = read8(read32(G+4) + 2*read8(read32(G)+0) + 0)
```

It must be either zero or have one of `0x04/0x08/0x20` set while lacking `0x02`. Which of those encodings the live prologue uses cannot be selected from the executable alone; the exact byte is a same-day read item. A claim that the board stores `xmin=1` is **REJECTED**.

### Asset/record provenance — VERIFIED

`FUN_001c4944` (`0x001C4944`) parses the `rmfd` asset into three sibling objects. `FUN_001c3ff0` takes the middle object (`FUN_001c4ae4`, source bundle `+4`) and initializes `G` with `FUN_001c6d90` (`0x001C6D90`). That initializer sets:

```text
G+0 = parsed RMFD grid object
G+4 = R+0x12                         // R = active chapter/board record
```

The sparse marker facade is initialized separately by `FUN_001c5b4c` (`0x001C5B4C`) from the third parsed RMFD object and `R+0x114`.

## 2. Real tile data and rejected candidates

### Dense tile array — VERIFIED

The real per-visible-cell source is `G`, not `T`:

```text
G = read32(read32(read32(M+0x118)+4)+8)
A = read32(G+0)
width  = read8(A+0)
height = read8(A+1)
cells  = read32(G+4)
flags(x,y) = read8(cells + y*width + x)
```

It contains exactly `width*height` directly indexed flag bytes. It is consumed both by movement (`FUN_001b72e4`) and by the render/update family (`FUN_001af3b0`, `FUN_001b04fc`, and related callers of `FUN_001c6fa8`). This explains the broad field of ordinary coin cells without requiring marker records for them.

### `B+0x100` — REJECTED

The bundle `B` itself is allocated with `FUN_001c4194(0x18)` in `FUN_001be9a4`, so `B+0x100` is outside the object. The observed pointer there is adjacent heap memory and has no ownership relation to `B`. The genuine pointer into `R` is `G+4 = R+0x12`, established by `FUN_001c6d90`; this likely explains why the accidental `B+0x100` read happened to reveal that value.

### Sparse marker array as terrain — REJECTED

`T+4 = R+0x114` is initialized by `FUN_001c5b4c`. It is the sparse `0x10`-byte marker/state array described in ANSWER6. Its count of one and sole `(6,2)` entry do not constrain cursor movement. The ordinary input path calls the dense-grid validator first and only then looks for an optional marker with `FUN_001c5bb4(T,x,y)`.

## 3. Meaning of the `(6,2)` marker

### Full structural decode — VERIFIED

For the sole marker `E = read32(T+4)`:

```text
key      = read8(E+0)
x,y      = readS8(E+2), readS8(E+3)       // (6,2)
flags    = read8(E+0x0C)
variant  = read8(E+0x0D)
priority = readS16(E+0x0E)

C        = read32(T+0)
off      = read32(read32(C+4) + key*4)
O        = read32(C+8) + off
type     = readS16(O+4)
assoc_a  = readS16(O+0x14)
assoc_b  = readS16(O+0x16)
```

The live `key/type/assoc` values were not supplied in TASK7 and are heap/asset data, so a specific semantic name for this instance is **UNRESOLVED STATICALLY**. It would be fabrication to call it a chest, battle, exit, or home marker without those five reads.

### Code-consumed meaning — VERIFIED

Simply moving onto `(6,2)` only highlights the marker. CROSS is consumed in state `0x28` of `FUN_001b97c8`, beginning at call site `0x001BC648`. The consumer first reads `cell(6,2)`. If terrain bit `0x02` is clear, it resolves `O` with `FUN_001c5d14(T,6,2)`, branches on `type=[O+4]`, and calls `FUN_001b78a8` (`0x001B78A8`) with this event class:

| marker `type` | class passed to `FUN_001b78a8` | event armed by `FUN_001cf97c(M,1,event,-1)` |
|---:|---:|---:|
| `1`, `0x0D` | `10` if `FUN_001b8e40` rejects it, otherwise `1` | `0xDD` or `0x0A` |
| `2`, `7`, `0x10` | `5` | `0x9E` |
| `4` | `8` | `0xD3` |
| `5` | `6` | `0xCC` |
| `9` | `7` | `0xD2` |
| `0x0E` | `9` | `0xDC` |
| `0x0F`, `0x11` | `10` | `0xDD` |
| all other types | `1` | `0x0A` |

This is the exact static answer to “what event it arms”; the instance row becomes known as soon as `type` is read through the chain above. `O+0x14/+0x16` are associated ids consumed by the later marker/event handlers (including the `FUN_001b6abc`/`FUN_001be6d4` path); they are payload, not movement neighbors.

## 4. Cheapest read-only available-directions recipe

Use the dense grid only. Do **not** scan `T`, reproduce ANSWER6's marker filter, or call a game function.

```text
M = read32(0x08B98940)
P = read32(M+0x118)
B = read32(P+4)
G = read32(B+8)
A = read32(G+0)
cells = read32(G+4)
w = read8(A+0)
h = read8(A+1)

legal(x,y):
    if x < 0 or y < 0 or x >= w or y >= h: return false
    f = read8(cells + y*w + x)
    return (f & 2) != 0 || (f != 0 && (f & 0x2C) == 0)

available from (x,y):
    west  = legal(x-1,y)
    east  = legal(x+1,y)
    north = legal(x,y-1)
    south = legal(x,y+1)
```

This exactly mirrors the pure validation performed by `FUN_001b72e4`. It costs a one-time holder walk and header read, then one byte per in-bounds direction. `T` should be consulted separately only to describe an interactive marker on a legal cell.

## Final ranking

- **VERIFIED:** `B+8` dense grid, `B+0x0C` sparse markers, `B+0x10` dispatcher; `D+0x3C=G`, `D+0x40=T`; width/height at `u8 [G[0]+0/+1]`; dense row-major flags at `[G+4]`; full mask test; rectangular bounds `0..w-1`, `0..h-1`; CROSS's type-to-event dispatch.
- **CANDIDATE pending one live byte:** the precise disqualifying bit pattern at `(0,2)`; its exact holder address is supplied above.
- **UNRESOLVED pending supplied live fields:** the human semantic label of the sole `(6,2)` marker. The complete decode chain and exhaustive event branch table are supplied, so no further code tracing is required.
- **REJECTED:** a stored `xmin=1`; per-row range records; using sparse `T` as the terrain map; ANSWER6's marker decision tree for ordinary directions; any neighbor list; `B+0x100` as a valid bundle field.
