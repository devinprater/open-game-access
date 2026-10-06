# DBZ Another Road — menu address routes, both tried (2026-10-06)

Both routes were run head-to-head on a live main menu. Result: **the decompile route wins
decisively for identifying WHICH screen you are on; the pattern route remains the fallback for
the per-list selection.**

## Experiment A — anchor on the engine's menu struct (WINNER)

**Locator (no hardcoded address, no candidate list).** Find the word `W` such that:

1. `W` points into the menu descriptor table (RAM `0x089EBBA0`, row stride `0x14`), and
2. the word **after** `W` equals the row index.

**Exactly one match** was found while a menu was on screen. That word is the engine's
active-menu pointer from the decompile (`FUN_000e1afc`):

```
+0x000  descriptor table + menuId*0x14     (the active screen's handler row)
+0x004  menuId                            <- SCREEN ID
+0x070  screen id                          (decomp's field)
+0x074  selection                          (decomp's field; -1 = nothing selected)
```

**Measured**, one session, entering and leaving a submenu:

| step | struct address | menuId | +0x70 | +0x74 |
|---|---|---|---|---|
| main menu | `0x08BFC758` | **1** | 2 | 0 |
| after `cross` (enter) | `0x08BFC758` | **3** | 3 | -51 |
| after `circle` (back) | `0x08BFC758` | **1** | 2 | 0 |

- the address was **stable across screen changes** (unlike the heap pair), and the locator
  re-found the same base on a second pass;
- **`menuId` changed 1 -> 3 -> 1**, so it is a genuine SCREEN DISCRIMINATOR — the reader can
  name the screen instead of inferring it from a list length.

⛔ The address is still **per-boot** (heap). The signature is stable; the address is not, so
the locator runs each session.

⛔ **`+0x74` is NOT the per-list selection.** It stayed `0` across repeated `down` presses on
the main menu and only changed when the screen itself changed. The decomp's "selection"
field is real but is not what a list cursor reads.

## Experiment B — the (list_len, index) heap pair (fallback)

- The pair moved on **every boot and every screen change**, and it is reached by **no pointer**
  in RAM (checked: 0 hits for all three words).
- Found by pattern alone, the test `len in 2..32, index < len` returns **3,921 candidates** —
  so pattern matching cannot identify it.
- The **5-snapshot ordinal hunt** (start / no-press / down / down / up, keeping values that are
  byte-stable with no press and step +1/+1/-1) narrows it to **1-2 candidates** on a screen
  where the list is focused. That is the method to keep.

## Verdict

| need | route | why |
|---|---|---|
| **which screen** | **A (decompile anchor)** | unique signature, stable address, menuId discriminates. B offered no screen id at all. |
| **selected item** | **B (ordinal hunt)** | A's struct field does not track a list cursor. B works when the list is focused. |

So the reader uses **A for the screen and B for the selection**. `psp-ar-reader.mjs` implements
A, and reports honestly when `menuId` is one it has not mapped.

## Also learned this pass

- The d-pad does NOT move the selection on every menu screen (confirmed again: `down` changed
  nothing while on `menuId=1`). A selection hunt must first confirm the screen scrolls.
- The debugger allows **one client only**; repeated connect/disconnect leaves sockets in
  `TIME_WAIT` and the next attempt fails with "connect timeout" indefinitely. Run each
  experiment as ONE connection, or restart the emulator.
- The `(pointer->table, id)` pair and the `(len, index)` pair sit in the **same heap block**
  (`0x08BFCF54` vs `0x08BFC758`), which is why both move together per boot.

## Tooling (in `oga-work/scripts/`)

| script | purpose |
|---|---|
| `psp-ar-both.mjs` | runs both routes in ONE connection and prints the verdict inputs |
| `psp-ar-reader.mjs` | the reader built on route A |
| `psp-ar-verify2.mjs` | struct field check across a press sequence |
| `psp-ar-screenid3.mjs` | proves menuId changes with the screen |
| `psp-ar-struct2.mjs` | dumps the located struct |
| `psp-ar-active.mjs` / `psp-ar-active2.mjs` | the pointer-into-table searches (strict / loose) |
| `psp-ar-desc.mjs` | reads the 24-row descriptor table live |
| `psp-ar-nav.mjs` | deliberate navigation with a screenshot after every press |
