# DBZ Another Road — menu system, from the decompile (2026-10-06)

This answers "how does the menu work". Everything below comes from the Ghidra project
`oga-ghidra-dbzar` (program `EBOOT.dec`, 6,551 functions) and was cross-checked against live
RAM where stated.

## 1. The engine is a named-task system (verified in code)

Every UI module registers itself the same way:

```c
FUN_001408a8("[MENU] MESSAGE", 0x21, 4, 0)   // create a named task -> handle
FUN_00140a30(handle, FUN_000e2374)           // attach a callback at handle+0x20
```

`FUN_001408a8(name, id, 4, 0)` -> `FUN_00141698(0, name, id, 4)`. Attaching writes the
function pointer at `handle + 0x20`.

Modules found: `[MENU]` (MESSAGE/UPDATE/DRAW), `[TITLE]` (UPDATE/DRAW), `[SHOP]`
(CURSOR/MONEY), plus `[SYS]`, `[AR]`, `[BTL]`, `[SCR]`.

- `FUN_000e1afc` (512 B) — **menu module init**; registers the three `[MENU]` tasks.
- `FUN_00120d40` (336 B) — title init; `FUN_000ebbb8` (672 B) — shop init (allocates
  `0x5064` bytes of shop state).

## 2. The menu module's state struct is behind a POINTER

The disassembly of `FUN_000e1afc` is explicit:

```asm
lui  s0, 0xC
lw   a0, 0x139c(s0)      ; a0 = *(0xC139C)  -- a POINTER, loaded from that address
sw   s2, 0x0(a0)         ; store the MESSAGE task handle at struct+0
...
lw   a0, 0x139c(s0)
sw   s2, 0x4(a0)         ; UPDATE handle at struct+4
```

So `0xC139C` is a **pointer slot**, not the struct. Ghidra's `iRam000c139c` label is an
address artifact — 0xC139C falls inside `.text` (0x0–0x19F16F, read-only), which is why the
live bytes there decode as MIPS instructions.

State-struct fields read in code:

| offset | meaning |
|---|---|
| `+0x00` / `+0x04` / `+0x08` | `[MENU]` MESSAGE / UPDATE / DRAW task handles |
| `+0x6c` | sub-state (`2` = updating, `3` = another) |
| `+0x70` | **menu SCREEN ID** (`FUN_000e28b8` reads it; skips draw when it is `0xE`, `0x13`, `0x14`) |
| `+0x74` | **SELECTED ITEM ID** — explicitly compared against `-1` = nothing selected |
| `+0x80` / `+0x81` / `+0x82` / `+0x83` | flags: visible, active, redraw |
| `+0x84` / `+0x88` | animation counter / scale (`1.0 - n*0.03125`) |

⛔ **The struct address is resolved at runtime** (`*(0xC139C)`), so no live RAM dump has been
able to tie it to the heap pair the menu reader uses. That link is the open item.

## 3. Menu screens are a 24-row descriptor table (VERIFIED live)

`FUN_000e1afc` sets:

```c
*piRam00034660 = (int)(&DAT_001e7ba0 + menuId * 0x14);
piRam00034660[1] = menuId;
```

The table is at ELF **`0x1E7BA0`**, row stride **`0x14`**, indexed by **menu id**. With the
confirmed ELF→RAM delta (`ghidra + 0x08804000`) it sits at RAM **`0x089EBBA0`**, and it was
observed **byte-present there with correctly relocated pointers** (e.g. `0x088F1204`), so it
is readable live.

Row layout: `w0`,`w1` = handler function pointers · `w2` = third handler or 0 · `w3` =
another handler · `w4` = a small count.

| id | w0 | w1 | w2 | w3 | count |
|---|---|---|---|---|---|
| 1 | `000ED204` | `000ED20C` | – | `000ED368` | 1 |
| 2 | `000DA290` | `000DA4D4` | – | `000DA298` | 0 |
| 3 | `000ED588` | `000ED590` | – | `000ED6FC` | 0 |
| 4 | `000E62F4` | `000E62FC` | `000E63CC` | `000E6440` | 4 |
| 5 | `000EB208` | `000EB210` | – | `000EB2FC` | 1 |
| 6 | `000EADA8` | `000EADB0` | – | `000EB054` | 1 |
| 7 | `000ECFB0` | `000ECFB8` | – | `000ED0B4` | 0 |
| 8 | same as 7 | | | | 0 |
| 9 | `000EB95C` | `000EB968` | `000EBA04` | `000EBA30` | 4 |
| 10 | `000DD3D4` | `000DD3DC` | – | `000DD438` | 6 |
| 11 | `000DA72C` | `000DA734` | – | `000DA7B8` | 4 |
| 12 | `000DC1D8` | `000DC1E0` | `000DC260` | `000DC27C` | 4 |
| 13 | `000DFE74` | `000DFE7C` | `000DFEFC` | `000DFF18` | 4 |
| 14 | `000E0994` | `000E099C` | – | `000E14CC` | 5 |
| 15 | `000ECC64` | `000ECC6C` | – | `000ECE64` | 1 |
| 16 | `000EB478` | `000EB480` | – | `000EB778` | 1 |
| 17 | `000DD614` | `000DD61C` | `000DD750` | `000DD76C` | 4 |
| 18 | `000DE334` | `000DE400` | – | `000DE5E0` | 0 |
| 19 | `000DCF44` | `000DCF4C` | – | `000DCFDC` | 1 |
| 20 | `000EA1CC` | `000EA1D4` | – | `000EA4E4` | 1 |
| 21 | `000EAA90` | `000EAA98` | – | `000EAC1C` | 0 |
| 22 | `000DE710` | `000DE718` | – | `000DE77C` | 0 |
| 23 | `000DE934` | `000DE93C` | – | `000DE9B0` | 0 |

**24 menu screens.** The handlers sit in three clusters — `0x000Dxxxx`, `0x000Exxxx`,
`0x001Exxxx` — which is a useful grouping hint.

The handler *names* are not in the handlers: a screen's title comes from a **message id**
resolved through the game's text system, which is why string-mining the handlers returns
`(none)`. Naming the 24 screens needs the MSG-ID→text resolver, or a live sweep that notes
the (id) on each screen.

## 4. Why the fixed addresses do not resolve

- `0xC139C` and `0x34660` sit in `.text` (read-only) → they are **Ghidra address artifacts**,
  and the live bytes at `+0x08804000` are overlay code.
- The game runs from **overlays that swap per game-state**, so anything that is not resident
  in the base module has no stable absolute address. This is the same wall the doc recorded
  for the battle HUD.
- A search for any word pointing into the 24-row table returned **0 hits** on the screens
  sampled, consistent with the holder living in overlay memory.

## 5. What this means for the reader

The menu reader works off a **heap (list_len, index) pair** that moves every boot and every
screen change, so it needs re-location each session. The decompile says the engine's own
handles are the pointer at `0xC139C` and the table pointer at `0x34660` — both unreachable as
fixed addresses here.

**The two viable routes, in order of promise:**

1. **Find the pointer slots at runtime** — scan for a word that points into the descriptor
   table (`0x089EBBA0`) *while a menu is on screen*. If the holder is overlay-resident it will
   appear only then, which is exactly why earlier scans found 0.
2. **Accept the heap pair** and re-derive per session with the 5-snapshot tool
   (`psp-ar-pair.mjs`), naming screens by the observed list length.

## 6. Tooling added by this pass (in `oga-ghidra-dbzar/`)

| file | purpose |
|---|---|
| `DisMenu.java` / `run-menu.bat` | decompile the `[MENU]`/`[TITLE]`/`[SHOP]` module cluster |
| `DisMenu2.java` | struct fields, descriptor tables, references |
| `DisScreens.java` | enumerate the 24 screens + decompile each handler |
| `DisBlocks.java` | memory-block map; proves where an address can live |
| `DisDelta.java` | settle the ELF→RAM delta from the code's own constants |
| `menu-decomp.txt`, `menu2.txt`, `screens.txt`, `blocks.txt`, `delta.txt` | the outputs |
