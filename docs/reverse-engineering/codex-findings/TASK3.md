# TASK3 — Name the live menu object for Dissidia's selection accessor

## Context

Dissidia Final Fantasy, PSP (ULUS10437). EBOOT decrypted: ask the operator for its path
(`EBOOT.BIN.dec`, 4.7 MB, MIPS, loaded vaddr 0). Ghidra reports from prior work live in
`C:\Users\Devin Prater\oga-ghidra-dissidia\` — read these first:

- `api-consumers-report.txt` (4 KB) — FULL body of `FUN_00267cb8` (menu-region input consumer)
- `index-accessors-report.txt` (4 KB) — `FUN_00251278` (index), `FUN_0025236c` (count),
  `FUN_00252424` (element, stride 0x44 cap 7), `FUN_00251de4` (selected value), `FUN_00250538`
  call sites
- `list-object-report.txt` (9 KB) — `FUN_00267740` body + widget-function field offsets
- `consumer-chain-report.txt` (15 KB) — how the input API table is reached

Doc (if you want the full story): `C:\Users\Devin Prater\open-game-access\docs\reverse-engineering\dissidia-final-fantasy.md`,
sections 94-100. Sections 97-100 are the most relevant.

## Established facts (do NOT re-derive; build on them)

1. `FUN_00250538(obj)`: index = `[obj+0x3C]` (via +0x28/+0x14), count = `[obj+0x240]`
   (via +0x60/+0x1e0), element = `obj+0x60+4+idx*0x44` (stride 0x44, hard cap 7),
   selected value = `[element+0x18]`; returns -1 unless `0 <= idx < count`.
2. `&DAT_000012c4 + param_1` is a FIELD offset, not a global: file bytes at 0x12c4 are MIPS
   code (`FUN_000012c8` sits 4 bytes later). So the widget is `W = P + 0x12c4` where `P` is
   `FUN_00267cb8`'s `param_1`; index at `P+0x1300`, count at `P+0x1504`.
3. `FUN_00267cb8` is a CONFIRM handler: it calls `FUN_00250538(W)` and checks the returned
   VALUE == `0x3a` (58), playing sound 0x2711 vs 0x2712 (confirm/cancel).
4. Eleven live hunts failed to find the index as a wrapping word, a gated pair, or a one-hot
   flag on EITHER the 4-row pause menu or the 5-row mode-select menu — even with display-verified
   highlight moves. The prime suspect is now: **these menus are served by a DIFFERENT consumer
   than `FUN_00267cb8`, each with its own object** — there are SIX call sites of `FUN_00250538`.

## Questions (answer each with vaddr evidence)

1. **The other five call sites.** For each `FUN_00250538` call site in
   `FUN_001256f4` (0x001257d0), `FUN_0012961c` (0x001297a0), `FUN_0012ebcc` (0x0012ed10),
   and `FUN_001b97c8` (0x001ba3a4, 0x001ba5f0 — 17 KB function): what is the enclosing
   function's apparent menu role, and what OBJECT EXPRESSION is passed? Which one most
   plausibly serves a pause menu vs a mode-select menu?
2. **The guard singletons.** Decompile `FUN_0025051c` (28 bytes — what does it return?),
   `FUN_00251238`, `FUN_00252364`, `FUN_00251da4`. What state do they check, and do they read
   any RAM address that would be a LIVE-READABLE anchor for "the menu system is up" or for
   reaching the object?
3. **`param_1` of `FUN_00267cb8`: pointer or index?** It is used as `param_1 + 0x1598/0x1599/
   0x159a/0xeb0/0xf60` (pointer-like) AND in `(&DAT_00001296)[param_1]` (index-like). Settle it
   from the instruction level if you can (is the 0x1296 form `lb reg,0x1296(reg)`?).
   If it IS a heap pointer: who allocates objects of that size (~0x1600+ bytes)? Find an
   allocator call or a static holder that would let a live-memory scan find `P`.
4. **What is value `0x3a`?** In the game's menu value space, what kind of item has value 58?
   (Button ID? Row tag?) Your answer constrains which menu `FUN_00267cb8` serves.

## Deliverable

Write `ANSWER3.md` in THIS directory (`C:\Users\Devin Prater\oga-trace-codex\`): per-question
answers with vaddr citations, plus ONE ranked recommendation for where a live-memory scan should
look for the selection state on the pause menu and on the mode-select menu. No ROMs needed, no
emulator needed — static analysis only.
