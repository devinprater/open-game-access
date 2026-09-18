# TASK: find the code that writes Dissidia's menu selection index

## Context you need

A PSP game (Dissidia Final Fantasy, `ULUS10437`) is being reverse-engineered for **screen-reader
accessibility**. The goal is to read the *currently highlighted menu row* from memory so it can be
spoken aloud. Text reading already works (menu strings are decoded in RAM). **The selection index is
the last missing piece.**

The EBOOT has been decrypted and decompiled with Ghidra. The analysis is complete; the artifacts are in
this directory. Your job is a **bounded code-tracing task**, not an open-ended reverse-engineering
effort.

## The question to answer

> **Which function writes the value that selects the render block at `0x09DEE480`?**

## What is already known (established by measurement against the live game)

1. **`0x09DEE480` is a render-node array** — stride `0x50`, 13 blocks. Each block is *16 floats (a
   transform/orientation set) followed by three pointers*; the pointer triple sits at block offset
   `+0x30`. Word offsets of the triples: `+0x30, +0x80, +0xD0, +0x120, ...` (every `0x50` from `+0x30`).

2. On a menu with two rows, pressing `down` changes **exactly 2 bytes** in that 1024-byte region:
   the low bytes of the **first two pointers** at `+0x30` / `+0x34`. They alternate between two
   geometries. So this is **derived render state** — the renderer is being driven correctly, but the
   *selection* lives somewhere else. Finding where is your task.

3. **Input path (already mapped):**
   - `FUN_000f7994` calls `FUN_0036da44(1)` and `FUN_0036da54(0x411b)` (sceCtrl sampling setup), then
     registers **`FUN_000f790c`** via `FUN_00103db0` — *the same registration mechanism the menu manager
     uses*:
     ```c
     uVar1 = FUN_00103c60(DAT_00392cd8);
     FUN_00103db0(uVar1, 0xffffffff, &DAT_00003a98, FUN_000f790c);
     ```
   - `FUN_000f790c` (136 B) dispatches to `FUN_000f76f4`, `FUN_000f778c`, `FUN_000f77f0`, and a
     callback at `*(*(int*)(iVar1+0x34)+0xc)`.
   - `DAT_003925b0` is the **global pad object**; `+0x264` holds input flags. `FUN_000f76f4` (152 B)
     and `FUN_000f68e0` (548 B) read the pad via `sceCtrl` (`FUN_0036da4c`).
   - This looks like a **generic libpad layer**, so the button *consumers* are elsewhere.

4. **Structures already ruled out** (do not re-investigate these — each has a verified identity):
   - `DAT_00392cd8` — engine-service pointers, plus a constant `290` that never moved across presses.
   - `DAT_00397770` — a **chapter-name table**: 36-byte records `{int id; char name[32]}` holding
     `one00`, `two00`, `thr00`, ... (`+0x08` is the name field; 7/7 records parse).
   - `DAT_00397770 + 0` — holds a **heap pointer** (`0x08C08EB0`) written at run time. That object's
     header (`+0x20 = -1`, `+0x24 = 1`, `+0x28` = head, `+0x2C` = tail, `+0x30` = 35, `+0x3C` = 20)
     matches the manager layout, and its **35-node doubly-linked list is completely static** across
     verified presses. It is a **definition table**, not the visible list.
   - `0x08BE0000-0x08C20000` — contains **no wrapping field** (20 verified presses, period ≤ 12).
   - Three handlers registered by the menu manager constructor (`FUN_0036926c`, `FUN_00369290`,
     `FUN_003692b4`) are **draw callbacks** over a 4,776-byte renderer; not navigation code.
   - `FUN_00103c60` is an 8-byte accessor `return *(undefined4 *)(param_1 + 0x2000);` — a global engine
     handle, 28 call sites.

5. **Addressing convention (important):** Ghidra shows the ELF's own vaddr. The **RAM address =
   `0x08804000 + vaddr`**. So Ghidra `0x002489d0` is RAM `0x08B0C9D0`... unless the value is itself a
   pointer read from RAM, in which case it is already a RAM address.

## Artifacts in this directory

- `input-report.txt` — references to the sceCtrl/sceDisplay stubs, and the decompiled input-path
  candidates (`FUN_000f7994`, `FUN_00351410`, `FUN_002602c8`, `FUN_002d0450`, `FUN_002d6110`, ...).
- `input-handler-report.txt` — `FUN_000f790c` and its transitive callees with sizes and decompiled
  bodies.
- `menu-handlers-report.txt` — the three registered draw callbacks and the registration function.
- `decompile2-report.txt`, `decompile3-report.txt`, `decompile4-report.txt` — the menu manager
  constructor, its dispatcher, and the node list routines.
- `menuinit-report.txt` — `FUN_002489d0` (constructor) and `FUN_00246d64`.

## What would count as an answer

Any **specific, testable** claim of the form:

- a function address that writes a selection value, and the field/offset it writes; or
- the code path from a d-pad press to that write; or
- a reasoned elimination that the selection is *not* written by code reachable from the mapped input
  path (with the argument why).

Prefer naming **addresses and offsets** over prose. If you find candidate functions, list them ranked
with the evidence for each.

## Constraints

- **Read-only analysis.** Do not propose writing to game memory.
- Do not re-derive the eliminated structures listed above; assume them true.
- Keep the answer concrete: function addresses, offsets, byte values.
