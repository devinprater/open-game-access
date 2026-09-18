# TASK: identify the SELECTION / SCROLL-WINDOW structure in Dissidia's draw path

## Context

A PSP game (Dissidia Final Fantasy, `ULUS10437`) is being reverse-engineered for **screen-reader
accessibility**: reading the currently highlighted menu row aloud. Text reading already works. **The
selection index is the last missing piece** and it has resisted every memory-side search.

You are being asked a **bounded code question**. Artifacts are in this directory. Do not re-derive
what is listed as already known.

## THE QUESTION

In `FUN_0025595c` (Ghidra `0x0025595C`, the function that writes the render block array) there is this
computation:

```c
iVar2 = (uint)*pbVar5 - (int)*(short *)(param_2 + 0x18);
if (iVar2 < *(int *)(param_2 + 0x1c)) {
    iVar2 = *(int *)(*(int *)(param_2 + 0xc) + 0xc) + iVar2 * 0x1c;
}
else { iVar2 = 0; }
```

Read structurally this is: **subtract a base (`param_2+0x18`), bound it by a count (`param_2+0x1c`), and
index a `0x1c`-stride table whose base is `*(int*)(*(int*)(param_2+0xc)+0xc)`.** That is the shape of a
**visible-window / scroll-offset** computation — exactly the neighbourhood a selection or scroll
position would live in.

**Answer these, with addresses and offsets:**

1. **What is `param_2`?** Trace it. `FUN_0025595c` is called from `FUN_0024aee0` as
   `FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16)`. Identify the object passed as the
   second argument and how a caller obtains it.
2. **What are `param_2+0x18`, `param_2+0x1c` and `param_2+0xc` semantically?** Which is the scroll
   base / first-visible-row, which is the visible count, and which points at the index table?
3. **Is `param_2` reachable from the per-screen manager object?** The manager is at RAM `0x08C08EB0`
   (pointer stored at Ghidra `0x00397770` + 0). Its header, measured in RAM:
   `+0x14` render-array base, `+0x18` render block count, `+0x1C` (12 on the pause menu, 0 elsewhere),
   `+0x20` (-1 or 1), `+0x24` (1), `+0x28`/`+0x2C` node-list head/tail, `+0x30` node count.
4. **If `param_2` is a distinct object, what static or pointer chain reaches it** so its fields can be
   read in RAM at run time? A concrete RAM address recipe is the ideal answer.

## What is ALREADY KNOWN — assume true, do not re-derive

**Addressing:** Ghidra shows the ELF vaddr. **RAM address = `0x08804000 + vaddr`** for code/data in the
ELF; a value read FROM RAM is already a RAM address.

**Verified by measurement against the live game:**

- `FUN_0025595c` (size 2108, 32 stores) WRITES the render blocks:
  ```c
  if (*(int *)(DAT_00397770 + 0x18) < 0xaa) {
      puVar12 = (*(int *)(DAT_00397770 + 0x14) + *(int *)(DAT_00397770 + 0x18) * 0x50);
      *(int *)(DAT_00397770 + 0x18) += 1;
  }
  *puVar12 = pbVar5;  puVar12[1] = psVar4;  puVar12[2] = param_5;  puVar12[3] = fVar15;
  ```
  The measured array matches exactly: **stride `0x50`**, three leading pointers per block, 13 blocks on
  the pause menu.
- `FUN_0024aee0` (size 4776) is the draw callback that READS those blocks via renderer `+0x14`.
- **The render array is DERIVED state**, not the selection: on a 2-item menu its pointers merely
  alternate with the highlight.
- The manager object (`RAM 0x08C08EB0`) header is **completely static across 12 verified presses**,
  including `+0x24 = 1`.
- A full-RAM scan found **no wrapping ordinal** in `0x08800000-0x0A000000`; and no wrapping *byte*
  either beyond period-2 toggles.
- The node list (35 / 11 / 9 nodes on different screens) showed **no field change** across presses in
  the unverified runs, but every such run was VOID (no press moved the display), so that question is
  **open**, not settled.
- The pause menu's highlight DOES move with `down` (verified twice by OCR of the changed panel:
  `"4 Return to Game"` <-> `"4 Retry"`), but that screen cannot be reached by blind input.

**Ruled out (each has a verified different identity):** `DAT_00392cd8` (engine-service pointers + a
constant 290); `DAT_00397770` itself (a 36-byte-record chapter-name table: `one00`, `two00`, ... with
names at `+0x08`); the three handlers registered by the menu constructor (draw callbacks over the
4,776-byte renderer); `FUN_00103c60` (8-byte accessor `*(param_1 + 0x2000)`).

## Artifacts in this directory

- `render-writer-report.txt` — `FUN_0025595c`, `FUN_00255304`, `FUN_00255170` and callees, with sizes and
  store counts.
- `menu-handlers-report.txt` — `FUN_0024aee0` (the draw callback) and the registered handlers.
- `decompile2-report.txt`, `decompile3-report.txt`, `decompile4-report.txt`, `menuinit-report.txt` — the
  menu constructor, its dispatcher, node list routines.
- `input-report.txt`, `input-handler-report.txt` — the input path (`FUN_000f7994` ->
  `FUN_000f790c` -> libpad layer; `DAT_003925b0` is the global pad object, `+0x264` its flags).
- `select-refs-report.txt` — the only two functions referencing both the menu static and the pad object.

## What counts as an answer

A **specific, testable** claim:

- the object identity of `param_2`, with the pointer chain or static that reaches it;
- which field is the scroll base / visible count / index table;
- ideally: a RAM address recipe I can read at run time to obtain the current selection.

Prefer addresses and offsets over prose. Rank candidates with the evidence for each. If the chain
cannot be resolved from the supplied bodies, say so explicitly — a clear "not resolvable from these
artifacts, and here is what is missing" is a valid and useful answer.

## Constraints

- **Read-only analysis.** No writes to game memory.
- Do not re-derive the "already known" list; assume it.
- Concrete addresses, offsets, byte values.
