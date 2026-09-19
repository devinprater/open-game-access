# TASK7 — Board bounds, tile data source, and the (6,2) marker meaning

## Context

Same game/build. READ FIRST: `ANSWER5.md`, `ANSWER6.md` (all chains preserved and
live-verified), plus doc sections 106 (now RETRACTED -- eaten inputs, see s107) and 107.

## Verified live state (RAM cursor bytes, multi-try protocol, do NOT re-derive)

1. Movement is a GRID: all four directions work both ways everywhere probed (10/10 with
   retries). No one-way edges; s106's directed graph is dead. ANSWER6's probe+filter model
   stands.
2. ONE real boundary: westward travel on row 2 stops at x=1 (15 failed presses over 5
   probes: (4,2)->(3,2)->(2,2)->(1,2)->BLOCKED). Validation EXISTS against some tile data.
3. The `N` node array (`[T+4]` = `R+0x114`, count 1) holds ONLY the (6,2) marker; the
   cursor's tiles (1..6,1..3) are ABSENT from it. The validation source is elsewhere.
4. Cursor at (6,2) = the marker tile (reached and confirmed in RAM).
5. Unexamined lead: `B+0x100` holds pointer `0x08C3B7B2` (inside the R record at `R+0x12`).

## Questions (vaddr evidence for each)

1. **Bounds/dimensions.** Is cursor validation a per-row/per-board x/y RANGE (dimension
   bounds) rather than a tile table? Find where the west x=1 boundary (row 2) is stored:
   chapter record fields, board-bundle fields, or asset-derived limits. Deliver the exact
   holder chain + offsets, and the full bounds set (all four edges as the game stores them).
2. **Tile data source.** If bounds alone don't explain it (e.g. holes/gaps mid-board, or
   per-tile flags), find the REAL tile array: which structure holds an entry per visible
   tile (the `2` coins span a wide area)? Candidates: the `B+0x100` pointer above, chapter
   asset data paged in per board, or the render band's owners. Deliver holder chain +
   struct layout, or rule each candidate out with reason.
3. **Marker (6,2) meaning.** Full decode via the catalog chain (`key -> C+4/C+8 -> O+4`
   type, `O+0x14/+0x16` assoc ids): read the CODE-CONSUMED meaning (which consumer branches
   on this entry, what event it arms). The cursor sitting on it changes nothing by itself;
   CROSS on it might. Do NOT advise pressing anything -- static meaning only.
4. **Available-directions read path.** Given (1)-(2): what is the cheapest READ-ONLY recipe
   for "which of (x+-1,y),(x,y+-1) are legal targets" -- bounds compare, table scan, or the
   ANSWER6 decision tree over node records? The adapter will implement exactly this recipe.

## Rules

- Rank VERIFIED / CANDIDATE / REJECTED. Holder chains, never heap constants.
- The operator will verify bounds by probing (multi-try protocol) and read every address
  the same day.

## Deliverable

`ANSWER7.md` in THIS directory. Static analysis only, no emulator, no ROMs.
