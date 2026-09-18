# Answer: `FUN_0025595c`'s `param_2` is a render-tree node, not a scroll-window object

## Bottom line

`param_2` in `FUN_0025595c` is the current **top-level render/menu node** selected by `FUN_0024aee0` from one of the manager's two linked lists. It is directly reachable from the manager at runtime:

```text
manager static slot = 0x08804000 + 0x00397770 = 0x08B9B770
M = read32(0x08B9B770)                         // observed: 0x08C08EB0
N = read32(M + 0x28)                           // first-list head
// or N = read32(M + 0x34)                     // second-list head
param_2 in FUN_0025595c = N
```

For the measured manager `M = 0x08C08EB0`, the two head-pointer slots are therefore **`0x08C08ED8`** (`M+0x28`) and **`0x08C08EE4`** (`M+0x34`).

The supplied artifact does **not** contain the computation quoted in the question. The actual decompile in `render-writer-report.txt` is:

```c
if ((short)(ushort)*pbVar5 < *(short *)(param_2 + 0x18)) {
    iVar2 = 0;
} else {
    iVar2 = (uint)*pbVar5 - (int)*(short *)(param_2 + 0x18);
    if (iVar2 < *(int *)(*(int *)(param_2 + 0xc) + 0x2c)) {
        iVar2 = *(int *)(*(int *)(param_2 + 0xc) + 0xc) + iVar2 * 0x1c;
    } else {
        iVar2 = 0;
    }
}
```

Thus **`param_2+0x1c` is not the bound/count**. The count is at **`(*(param_2+0xc))+0x2c`**. This invalidates the proposed contiguous `+0x18/+0x1c/+0xc` scroll-window interpretation.

## 1. Identity and call chain of `param_2`

The relevant RAM function addresses are:

| Function | Ghidra | RAM |
|---|---:|---:|
| first-list wrapper `FUN_00248dec` | `0x00248DEC` | `0x08A4CDEC` |
| second-list wrapper `FUN_00248ea8` | `0x00248EA8` | `0x08A4CEA8` |
| renderer `FUN_0024aee0` | `0x0024AEE0` | `0x08A4EEE0` |
| writer `FUN_0025595c` | `0x0025595C` | `0x08A5995C` |

The wrappers call:

```c
FUN_0024aee0(M, M + 0x28);   // FUN_00248dec
FUN_0024aee0(M, M + 0x34);   // FUN_00248ea8
```

At entry, `FUN_0024aee0` immediately dereferences that list-head slot:

```c
param_2 = (int *)*param_2;
```

It then treats the result as a node: it tests flags at node `+0x14`, a byte at `+0x17`, tests the pointer at `+0x10`, reads node `+0x0c`, walks the node's child list from node `+0x00` through child `+0x3c`, and finally calls:

```c
FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16);
```

Consequently the writer's second argument is that current outer node, not the manager and not a separately obtained selection structure.

## 2. Actual meanings of the questioned fields

Let `N = param_2` and `R = read32(N+0x0c)`.

| Field | Proven use | Best semantic description |
|---|---|---|
| `N+0x0c` | Dereferenced as `R`; `R+0x0c` is used as a table pointer and `R+0x2c` as its count | Pointer to the node's resource/animation descriptor |
| `N+0x18` | Read as a signed 16-bit value and subtracted from the byte index `*pbVar5` before indexing the resource table | Resource-table index origin/bias for this node; it may behave like a first index, but the artifacts do not establish menu scrolling semantics |
| `N+0x1c` | Not read by this indexing expression; `FUN_00255170` calls it indirectly as `(*(code **)(N+0x1c))(read32(N+0x20), ...)` | Function/callback pointer, with callback context at `N+0x20`; definitely not a visible-row count |
| `R+0x0c` | Base plus `(byte_index - read_s16(N+0x18))*0x1c` | Base of a `0x1c`-stride resource/index table |
| `R+0x2c` | Upper bound for the adjusted index | Number of entries in that table |

There is independent confirmation in `FUN_0024aee0`: it sets `local_50 = (int *)N[3]` (that is, `R`), compares a child node's signed-short index at child `+0x0c` against `local_50[0xb]` (`R+0x2c`), and addresses `local_50[3] + index*0x1c` (`read32(R+0x0c) + index*0x1c`). `FUN_00254e2c` in `decompile4-report.txt` repeats the same `R+0x0c`/`R+0x2c` table pattern.

## 3. Reachability from the manager

Yes, the render node passed as `param_2` is reachable from the manager. A concrete read-only recipe for either list is:

```text
M     = read32(0x08B9B770)
N     = read32(M + 0x28)       // list rendered by FUN_00248dec
  or  = read32(M + 0x34)       // list rendered by FUN_00248ea8

bias  = read_s16(N + 0x18)
cb    = read32(N + 0x1c)
R     = read32(N + 0x0c)
table = read32(R + 0x0c)
count = read32(R + 0x2c)
```

For an element byte `k = *pbVar5`, its associated resource record is:

```text
j = k - bias
record = (k >= bias && j < count) ? table + j*0x1c : 0
```

This is testable in RAM and fully resolves the pointer chain for the object used by the draw path.

## 4. Does this yield the current selection or a scroll window?

No such conclusion is supported by these artifacts. The code proves an index-remapping window over a render/resource table, but it does not prove that `N+0x18` is a menu's first visible row or that `R+0x2c` is its visible-row count. In particular:

- the alleged `N+0x1c` count is actually a callback pointer;
- the real count resides in the separately pointed-to resource descriptor at `R+0x2c`;
- the indexed value is a byte embedded in render/animation data (`*pbVar5`), not a demonstrated logical menu-row ordinal;
- no supplied body shows pad input updating `N+0x18`, `R+0x2c`, or another field in this chain as selection moves.

Therefore there is **no justified current-selection RAM recipe** in the supplied artifacts. What is missing is the menu-side input consumer/state-update routine (or a verified moving-menu before/after watchpoint trace) showing which field changes when Up/Down changes the highlighted logical item. Watchpoints on `N+0x18` can test the weaker scroll-bias candidate, but treating it as the selection without such a trace would be speculation.
