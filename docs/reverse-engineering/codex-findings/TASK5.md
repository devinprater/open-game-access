# TASK5 — Board cursor object, tile-table owner, and the direction-press commit site

## Context

Same game/build as TASK3/TASK4 (paths in those files). READ FIRST:
`C:\Users\Devin Prater\oga-trace-codex\ANSWER4.md` (board structs, DP chains) and
`C:\Users\Devin Prater\oga-ghidra-dissidia\dp-spend-report.txt` (fresh decompiles of
`FUN_001b6084`, `FUN_001d5438`, `FUN_001ca7ec` with callers).

## What changed since (verified live, do NOT re-derive)

1. DP spend path is CLOSED: direction press -> battle-UI dispatcher `FUN_001b97c8` ->
   `FUN_001b6084(obj, delta)` (`s16[[obj+0x38]+6] += delta`, clamps, calls `FUN_001ca124`
   to refresh cache `[U+0xD78]`). Write watchpoints proved it; decompile confirms.
2. Board re-entry RESETS DP 0 -> 1 on both chains (cache + authoritative s16). Spend-test
   fuel is unlimited -- no battles needed.
3. The free cursor (DP 00, highlight roams, exact inverse returns) is NOT: a wrapping word
   in 24 MB, an index/count pair, a one-hot flag, an `R+0x114` marker flag (a 526K-px verified
   cursor advance changed ZERO bytes there). The cursor is likely world-coordinate floats, a
   node id inside a tile struct, or derived per-frame state.

## Questions (vaddr evidence for each)

1. **The live tile-table `T` owner.** ANSWER4 verified the SHARED table type consumed by
   `FUN_001c5bb4(T,x,y)` (`T+0x04` node array, stride `0x10`, coords at `+0x02/+0x03`,
   blocking flags at `+0x0C`) but left the live owner candidate. The board highlight MUST
   resolve through some `T` every cursor move. Find which manager/widget field holds the
   board's live `T`: trace callers of `FUN_001c5bb4`/`FUN_001c5db0` (or the draw-tick readers
   of the highlight) back to a static holder or a field of `M = [[0x00394940]]` /
   `U = [M+0x56C]`. Deliver the exact chain (e.g. `[[M+0x??]]+0x??`) plus the node count
   source (`FUN_001c4740([T+0])`).
2. **Which dispatcher site commits the move?** `FUN_001b6084` has THREE call sites in
   `FUN_001b97c8` (`0x001BB8BC`, `0x001BC344`, `0x001BC394`). Which one fires on a board
   direction press (case/tag context for each)? What `delta` does each pass (spend -1 vs
   grant)? Is any of them the CONFIRM path for cursor-then-commit (two-step), or do all
   three spend immediately?
3. **Cursor position variable.** Given the table from (1): where is the CURRENT highlight
   (x,y) or node id kept between moves -- a field of `U`, of the piece object at `M+0x118`,
   or recomputed from render state? Look at what `FUN_001c5bb4`'s callers pass as `(x,y)`:
   a live variable (adapter-readable) or a transient? If transient, name the upstream holder.
4. **Home-area prompt.** The board tile tooltip "Make this area your home area?" YES/NO --
   which consumer serves it (one of the six `FUN_00250538` call sites, or the `FUN_00267cb8`
   confirm family with tag `0x3A`)? Its object may be the nearest proven neighbor of the
   cursor object.

## Rules

- Rank: VERIFIED (code-proven) / CANDIDATE (one sighting) / REJECTED (with reason).
- Pointer chains from static holders only -- heap addresses are useless to the adapter.
- The operator will live-test every chain within minutes; give exact RAM math
  (RAM = vaddr + 0x08804000 for statics) + expected values on the prologue board.

## Deliverable

`ANSWER5.md` in THIS directory. No emulator, no ROMs, static analysis only.
