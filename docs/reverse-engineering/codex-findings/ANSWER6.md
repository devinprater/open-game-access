# TASK6 — board nodes, traversal filters, and object catalog (static analysis)

All addresses are ELF vaddrs. Add `0x08804000` only for static-image addresses. Heap data is reached only through the holder chains below. No emulator or ROM was used.

## Executive correction

The new live observations correctly reject ANSWER4's claim that `FUN_001c5bb4` alone explains movement, but they do **not** imply a stored neighbor-id graph. Static code proves the opposite:

- `FUN_001b8728(D,ox,oy)` explicitly probes exactly `(ox-1,oy)`, `(ox+1,oy)`, `(ox,oy-1)`, `(ox,oy+1)` by calling `FUN_001c5bb4` four times (`0x001B8780`, `0x001B87E0`, `0x001B8824`, `0x001B8868`).
- Neither `FUN_001b8728`, its filter `FUN_001b8930`, nor the `0x10`-byte node accessors read a neighbor-list pointer, neighbor count, or neighbor ids.
- The apparent directed edges arise in the **destination filter/state layer** after coordinate lookup: the candidate node must have node flag `0x40`, an allowed catalog type, and pass story gates. Some of these flags are mutated at runtime. Thus the same physical node can cease to be an eligible destination after the cursor/state transition.

Accordingly, a claim that node `(4,2)` contains `(4,1)`'s id in a neighbor list is **REJECTED**. The live facts to expect are instead that the four-probe/filter pipeline accepts `(4,1)` while at `(4,2)`, then rejects the `(4,2)` candidate after the transition because one of the exact filters below changed or failed.

## 1. Board node array and catalog

### Holder chain — VERIFIED

```text
M = read32(0x08B98940)                    // [vaddr 0x00394940]
P = read32(M + 0x118)
B = read32(P + 0x04)
T = read32(B + 0x0C)                      // board tile/table facade
D = read32(B + 0x10)                      // board dispatcher

C = read32(T + 0x00)                      // indexed object catalog/container
N = read32(T + 0x04)                      // node array
count = read16(C + 0x00)                  // FUN_001c4740(C)
node(i) = N + i*0x10
```

`FUN_001c4184(B)` returns `[B+0x0C]`; `FUN_001c418c(B)` returns `[B+0x10]`. `FUN_001c218c(P)` ticks `FUN_001b97c8(D)` and, on a selected node, independently obtains `T` through `FUN_001c4184([P+4])`. This preserves ANSWER5's owner result.

### Node entry, stride `0x10` — VERIFIED fields

| Offset | Size | Meaning/evidence |
|---:|---:|---|
| `+0x00` | 1 | catalog object key. `FUN_001c5cd4(T,i)` returns it; `FUN_001c5d90(T,key)` resolves it through `C`. |
| `+0x01` | 1 | unresolved. |
| `+0x02` | 1 | signed grid X; read by `FUN_001c5bb4` and `FUN_001c6a2c`. `-1` suppresses construction in `FUN_001b22d4`. |
| `+0x03` | 1 | signed grid Y; same evidence. |
| `+0x04..+0x0B` | 8 | unresolved per-node payload. No neighbor pointer/count access was found. |
| `+0x0C` | 1 | node state flags; exact bits below. |
| `+0x0D` | 1 | variant/state byte. Types `2/7` and `9/0x10` branch on it in `FUN_001b22d4`; semantic name unresolved. |
| `+0x0E` | 2 | signed selection priority. `FUN_001b8930` selects the eligible candidate having the lowest value. |

The earlier label “`R+0x114` marker array” was incomplete. `FUN_001c5070` initializes 32 records beginning at subrecord `+0x11C`, which is logical `R+0x114`, with exactly this `0x10` layout: bytes `0..3`, words `+4/+8`, flags `+0x0C`, and halfword `+0x0E`. It is the persistent 32-slot **node-state array**, not a separate catalog of screen markers. A live observation that only one slot is active means only one node-state slot currently satisfies the observed active-bit criterion; it is not proof that the board contains one object.

### Object catalog indirection — VERIFIED

Given `key=node[i+0]`, `FUN_001c5d90(T,key)` ultimately does:

```text
offset = read32(read32(C + 0x04) + key*4)
O = read32(C + 0x08) + offset
```

Catalog object fields proven by accessors/consumers are:

| Offset | Size | Meaning |
|---:|---:|---|
| `O+0x00` | 4 | resource/model id (`FUN_001c5dec`). |
| `O+0x04` | 2 | tile/object type (`FUN_001c5e0c`). |
| `O+0x08` | 2 | catalog state used by `FUN_001c6014` to set node blocked flags. |
| `O+0x14` | 2 | first associated object/item id (`FUN_001c5e5c`). |
| `O+0x16` | 2 | second associated object/item id (`FUN_001c5eac`). |
| `O+0x24`, `O+0x2C` | 4 each | render scale/resource parameters consumed by `FUN_001b22d4`; exact semantics unresolved. |
| `O+0x30` | 1 | mutable catalog/object enable byte (`FUN_001c4828`). |

Therefore the adapter's answer to “what is here?” is **node coordinate + node flags + resolved catalog object**, not the node record alone and not a separate marker list.

## 2. Types and the `(6,2)` entry

### Code-observed type values — VERIFIED numerically

`FUN_001b22d4` has explicit render/setup cases for types:

```text
0, 1, 2, 3, 4, 5, 7, 8, 9, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11
```

Additional exact classifications visible in code:

- `0,1,0x0B,0x0C,0x0D` are the only types accepted by `FUN_001b8930`'s four-neighbor candidate filter.
- `0,1,0x0B,0x0C,0x0D,0x0F` are handled as the same broad selectable/event family in `FUN_001c218c`.
- type `8` is non-rendered/disabled as a board object: `FUN_001b22d4` sets node flag `0x20`, and `FUN_001c68e8` returns false only for type `8`.
- types `9` and `0x10` set node flag `0x04` when node byte `+0x0D` is nonzero.
- type `5` has unique two-part animation/setup and is specially enumerated by `FUN_001b1d08`.
- paired presentation families are `(0,0x0B,0x0C)`, `(1,0x0D)`, and `(2,7)`.

Static code in the queried executable does **not** attach localized names “token”, “chest”, “battle”, “Stigma exit”, and “home” to those numbers. Assigning those names from shape/animation alone would be guessing, so those semantic mappings remain **CANDIDATE/UNRESOLVED**, not fabricated. What is verified and sufficient for an adapter is to expose the numeric `s16 O+4`, associated ids `O+0x14/+0x16`, node `+0x0D`, and node flags.

### What the live `(6,2)` 0x10-byte entry denotes

It denotes a node-state record whose coordinate is `(6,2)`, not a self-contained object marker. Decode all 16 bytes as:

```text
key       = u8  [entry+0x00]
unknown1  = u8  [entry+0x01]
x,y       = s8  [entry+0x02], s8 [entry+0x03]     // expected 6,2
payload4  = u32 [entry+0x04]
payload8  = u32 [entry+0x08]
flags     = u8  [entry+0x0C]
variant   = u8  [entry+0x0D]
priority  = s16 [entry+0x0E]
O         = C.base + C.offset[key]
type      = s16 [O+0x04]
assoc_a   = s16 [O+0x14]
assoc_b   = s16 [O+0x16]
```

The object identity cannot be obtained from the 16 bytes without following `key` into `C`; this is the important correction for the adapter.

## 3. Exact block reasons

There are two distinct rejection stages.

### Coordinate/node rejection — VERIFIED (`FUN_001c5bb4`, `0x001C5BB4`)

The scan returns `-1` when no `(x,y)` node exists or when the matching node has any of these flags:

```text
node.flags & (0x01 | 0x02 | 0x04 | 0x10)
```

`FUN_001c6014` proves that catalog state `1` sets node bit `0x02`, state `2` sets bit `0x04`, and other positive states also set `0x02`. Type/state construction supplies further dynamic bits.

### Traversal eligibility rejection — VERIFIED (`FUN_001b8930`, `0x001B8930`)

Even if coordinate lookup returns a node id, the candidate is accepted only if all of the following hold:

1. the catalog object resolved by `node+0` exists;
2. `s16 [O+4]` is one of `0,1,0x0B,0x0C,0x0D`;
3. `node.flags & 0x40` is nonzero (`FUN_001c693c`);
4. persistent-story gates `FUN_001ae5e8(...,6)` and `FUN_001ae5e8(...,8)` permit it;
5. its `s16 node+0x0E` priority beats the current best.

Some failed story-gate paths explicitly clear node flag `0x40` through `FUN_001c6994(T,id,0)`. This is the precise code mechanism by which a destination accepted on one step can be unavailable on the next. There is no “neighbor id absent” branch because there is no neighbor-id list.

For the reported west-to-`(5,2)` failure, static analysis alone cannot choose between “no coordinate node”, base block flag, wrong catalog type, absent `0x40`, and story gate without the live bytes. The adapter can distinguish them by running the same read-only decision tree:

```text
id = raw scan N for x,y (ignoring flags)
if none:                         NO_NODE
else if flags & 0x17:            NODE_BLOCKED   // 1|2|4|0x10
else if type not in allowed set: WRONG_TYPE
else if !(flags & 0x40):         NOT_TRAVERSABLE_NOW
else:                            story-gated/eligible (story flags needed)
```

## 4. Available-directions primitive

`FUN_001b8728(D,ox,oy)` is the closest primitive, but it does **not** return a set. It probes all four adjacent coordinates, calls `FUN_001b8930` for each, performs side effects (`FUN_001b6d04`, story/node-flag updates), and returns only the single eligible id with the smallest `node+0x0E` priority, or `-1`.

Thus:

- **VERIFIED:** there is a four-direction enumerator/filter internally (`FUN_001b8728`).
- **REJECTED:** there is a stored or returned neighbor set suitable for direct reading.
- **Adapter rule:** enumerate the four coordinate candidates and reproduce the two filter stages from memory. Calling `FUN_001b8728` merely to query availability is unsafe because it has side effects and collapses multiple candidates to one.

## Same-day live checks

1. Resolve `M/P/B/T/D` exactly as above; require `D+0x40 == T` during the board state.
2. Dump `count=read16(C)`, then all `count` entries at `N+i*0x10`. Expect coordinates `(4,2)`, `(4,1)`, `(5,1)`, and `(5,2)` if they exist physically. Do not expect neighbor ids anywhere in these records.
3. For each of those four records, log `id,key,x,y,flags,priority,type,assoc_a,assoc_b` before and after each displayed move.
4. Expected `(4,2)->(4,1)`: raw `(4,1)` lookup succeeds; flags lack `0x17`; type is in `{0,1,0x0B,0x0C,0x0D}`; bit `0x40` is set; story gates allow it.
5. Expected failure `(4,1)->(4,2)`: identify the first failed test in the ordered tree above. The strongest static candidate is loss/absence of traversal bit `0x40` or a story gate, not a missing reverse-neighbor id.
6. Expected `(4,2)->(5,2)` failure: first test distinguishes whether `(5,2)` physically exists. If it exists, report its exact base flags/type/bit-`0x40`; that yields the block reason without another hunt.
7. For the `(6,2)` slot, follow `key` through `C+4/C+8`; verify that `type=[O+4]`. Reading only its 16 node bytes cannot identify token/chest/battle/exit/home.

## Final ranking

- **VERIFIED:** holder chain `M -> P -> B -> T/D`; node array `[T+4]`, count `u16 *[T]`, stride `0x10`; coordinates `+2/+3`; catalog key `+0`; flags `+0x0C`; priority `+0x0E`; catalog resolver and type `O+4`; four coordinate probes in `FUN_001b8728`; post-lookup traversal filter in `FUN_001b8930`; dynamic clearing of traversal bit `0x40`.
- **CANDIDATE/UNRESOLVED:** localized semantic names for numeric type ids; exact meanings of node bytes `+1`, `+4..+0x0B`, and `+0x0D`; which exact filter caused each recorded blocked move until the operator reads the supplied fields.
- **REJECTED:** per-node neighbor lists; neighbor ids/count in the `0x10` node; treating the one visibly active `(6,2)` record as a separate one-entry object table; calling `FUN_001b8728` as a pure available-directions API; claiming a missing reverse edge from static storage.
