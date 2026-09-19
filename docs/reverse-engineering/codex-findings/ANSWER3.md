# TASK3 — identifying Dissidia's live menu objects

Static analysis only. Vaddrs below are relative to the decrypted executable's load base (the Ghidra image is based at 0). “Role” names are semantic inferences from control flow and item sets, not recovered symbols.

## 1. The other five `FUN_00250538` call sites

| Call site | Enclosing function and apparent role | Object passed to `FUN_00250538` | Assessment |
|---|---|---|---|
| `0x001257D0` | `FUN_001256f4`: a small modal/branch-result handler. It accepts only item values `0x0C` and `0x17` and dispatches two different close/transition paths (`0x001257EC–0x00125890`). | `*(param_1 + 0x18)`. This is explicit in the delay slot at `0x001257D4`: `lw a0,0x18(s0)`. | Not the 4-row pause menu or 5-row mode-select menu; its recognized value set is effectively two choices. |
| `0x001297A0` | `FUN_0012961c`: broad front-end/menu dispatcher. It handles many tags (`8–0x19`, `0x3F–0x45`) and branches according to global front-end state `[DAT_003931A0+0x4C]` (`0x00129650` onward). | `*(param_1 + 0x2C)`, proven by `lw a0,0x2c(s0)` in the call delay slot at `0x001297A4`. | A generic owner-driven front-end list; plausible secondary mode-select candidate, but less directly anchored than the next caller. |
| `0x0012ED10` | `FUN_0012ebcc`: fixed front-end selection handler. Its companion builder `FUN_0012fcd4` at `0x0012FCD4` populates the same list differently for global modes `3,6,7,8` and a default branch. | `DAT_003931A0 + 0x3E54`. The function forms that object at `0x0012EBE0–0x0012EBEC`; the call at `0x0012ED10` receives the saved pointer. | **Best mode-select candidate.** It is a stable global-relative object whose contents explicitly depend on the current front-end mode. |
| `0x001BA3A4` | `FUN_001b97c8`: the large battle/field UI state dispatcher, case `0x0F`. It recognizes tags `9`, `0x13`, and `0x14` and performs battle/UI transitions (`0x001BA3A4–0x001BA494`). | `DAT_00394940 + 0x234`. At function entry `iVar11=DAT_00394940`; the delay slot at `0x001BA3A8` is `addiu a0,s2,0x234`, where `s2` holds that root in this case. | Same pause-widget object as the next site; a substate-specific consumer. |
| `0x001BA5F0` | `FUN_001b97c8`: the same battle/field UI dispatcher, case `0x10`, with a jump table over tags `8..0x39` (`0x001BA5FC–0x001BA61C`). | `DAT_00394940 + 0x234`, directly in the delay slot at `0x001BA5F4`. | **Best pause-menu candidate.** It lives in the battle UI root and its broad tag switch fits pause commands and submenus. |

Thus the two strongest names are:

- pause-menu list widget: `battle_ui_root + 0x234`, with `battle_ui_root = [vaddr 0x00394940]`;
- mode-select list widget: `front_end_root + 0x3E54`, with `front_end_root = [vaddr 0x003931A0]`.

For either widget `W`, the live ordinal is `[W+0x3C]`, the count is `[W+0x240]`, and item `i` is at `W+0x64+i*0x44` with its tag at `+0x18`.

## 2. The “guard singletons”

They are not global singleton checks. They are tiny per-subobject availability reads, and the decompiler omitted arguments at several calls because their prototypes were not recovered.

- `FUN_00251238` at `0x00251238` is exactly `lbu v0,0(a0)` in the return delay slot (`0x0025123C`). It returns the first byte of the index subobject.
- `FUN_00252364` at `0x00252364` is the same shape: `jr ra; lbu v0,0(a0)` (`0x00252364–0x00252368`). It returns the first byte of the item-table/count subobject.
- `FUN_00251da4` at `0x00251DA4` is also `jr ra; lbu v0,0(a0)` (`0x00251DA4–0x00251DA8`). It returns the first byte of an item record.
- `FUN_0025051c` at `0x0025051C` calls `FUN_00251240(W+0x28)` (`addiu a0,a0,0x28` at `0x00250528`) and preserves that callee's `v0` through its return. `FUN_00251240` first checks byte `[W+0x28]` through `FUN_00251238`; if clear it returns `0`, otherwise it returns word `[W+0x38]` (`lw v0,0x10(s0)` at `0x00251264`). Callers interpret this as an input-result/status enum: `0` means no completed action, `1` is selection/confirm, and `2` is cancel/back.

These functions read no absolute RAM address and do not lead to an owner pointer. Once `W` is known, however, `[W+0x28]` is a useful “index controller initialized” byte and `[W+0x38]` is a useful current action/result field. Neither is a unique system-wide “menu is up” anchor.

## 3. `param_1` of `FUN_00267cb8`: pointer or index?

It is unequivocally a pointer.

At entry, `param_1` is saved in `s0`. The supposedly index-like expression is literally `lbu a0,0x1296(s0)` at `0x00267D58`; the nearby accesses are `lbu a0,0x1297(s0)` at `0x00267D64` and `0x00267D90`. The widget address is formed by `addiu s1,s0,0x12c4` at `0x00267DF0`. The compiler is doing base-plus-field-offset addressing throughout, not indexing a global array.

More importantly, the owner is statically anchored:

- global initialization assigns `DAT_003982F0 = &DAT_01356D30` (assignment visible in the global service initialization immediately after the one-time guard at vaddr `0x013582D0`);
- `FUN_00267a8c` loads `DAT_003982F0` and passes it to `FUN_00266e50` at `0x00267A9C`;
- `FUN_0026796c` initializes the same large object and calls `FUN_00266cf0(P)` at `0x002679BC`;
- `FUN_00266cf0` does make an allocation, but it is a separate `0x2000`-byte backing/work buffer stored at `[P+0]` (`0x00266D04–0x00266D1C`), not allocation of `P` itself;
- `FUN_002675d8` constructs the list widget at `P+0x12C4` (`addiu a0,s0,0x12c4` at `0x00267608`) and sets `[P+0x1598]=1` at `0x0026765C`.

Therefore the live field-menu owner is statically allocated:

```text
P = 0x01356D30
W = P + 0x12C4 = 0x01357FF4
index = P + 0x1300 = 0x01358030
count = P + 0x1504 = 0x01358234
```

There is also a pointer holder at vaddr `0x003982F0` whose value should be `0x01356D30` after global initialization. This is an excellent validation anchor, but this object belongs to the field/talk-event confirmation UI, not necessarily either hunted menu.

## 4. Meaning of value `0x3A`

`0x3A` is an **item value/tag (command ID)**, not a button ID and not the ordinal row number. `FUN_00267740` appends two list elements by calling `FUN_002501ac(W,0x3B,...)` and then `FUN_002501ac(W,0x3A,...)` at `0x00267764` and `0x00267778`. `FUN_00250538` returns the chosen element's field at `element+0x18`; `FUN_00267cb8` compares that returned tag with `0x3A` at `0x00267E38–0x00267E3C`.

The behavioral meaning is the affirmative tag in a two-choice confirmation prompt: choosing `0x3A` plays confirm sound `0x2711` and advances state to `3` (`0x00267E44–0x00267E68`); every other selected tag—here, the paired `0x3B`—plays cancel sound `0x2712` (`0x00267E6C–0x00267E80`). In practical names, `0x3A = Yes/OK` and `0x3B = No/Cancel`. That constrains `FUN_00267cb8` to a field/talk-event confirmation dialog, not the observed pause or mode-select menu.

## Ranked live-scan recommendation

### Pause menu

1. **Scan from the static pointer at `0x00394940`; candidate `W = [0x00394940] + 0x234`.** Read `[W+0x3C]` and `[W+0x240]`; require `0 <= index < count <= 7`, and validate item records/tags at `W+0x64+i*0x44`. This is the strongest candidate because both selection calls at `0x001BA3A4` and `0x001BA5F0` are in the battle UI state machine.
2. If that does not show the four-row pause screen, follow the owner-held widget in `FUN_0012961c`: locate the live owner and use `W=*(owner+0x2C)`. This is less attractive because no static holder for that owner is established here.

### Mode-select menu

1. **Scan from the front-end singleton at `0x003931A0`; candidate `W = [0x003931A0] + 0x3E54`.** The corresponding fields are `index=[0x003931A0]+0x3E90` and `count=[0x003931A0]+0x4094`. Validate `count==5` on the observed five-row screen and the same ordinal bounds. This is the strongest candidate because `FUN_0012fcd4` rebuilds this exact object from the current front-end mode and `FUN_0012ebcc` consumes it at `0x0012ED10`.
2. If the fixed object is a neighboring front-end menu rather than the displayed selector, inspect `W=*(owner+0x2C)` for the live `FUN_0012961c` owner; its dispatcher is the other strong front-end candidate.

The previous `P+0x12C4` target should be deprioritized for both hunts: it is now positively identified as the static field/talk-event Yes/No prompt at `0x01357FF4`, explaining why its ordinal did not track either displayed menu.
