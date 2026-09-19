# TASK6 — Board node graph: neighbor links, tile types, object catalog

## Context

Same game/build. READ FIRST: `ANSWER4.md`, `ANSWER5.md` (chains, DP, spend sites — all
live-verified, PRESERVE), and the live results below (do NOT re-derive).

## Decisive live results (display-verified moves + RAM cursor bytes `D+0x194/+0x195`)

The cursor chain `M=[0x08B98940] -> P=[M+0x118] -> B=[P+4] -> D=[B+0x10]` tracks the
highlight exactly. Walking it on the prologue board revealed the movement model:

```text
(4,2) --right BLOCKED-->  (no (5,2); ~80K-px bump feedback, coords frozen)
(4,2) --up--> (4,1)       (150K px, coords update)
(4,1) --down/left/up--> ALL BLOCKED (bump, coords frozen at (4,1))
(4,1) --right--> (5,1)    (511K px)
(5,1) --up/right--> BLOCKED; --down--> (5,2)  (146K px)
(5,2) --left--> BLOCKED (no edge back to (4,2))
```

So eastward travel on row 2 ends at x=4; the route continues up-right-down:
`(4,2) -> (4,1) -> (5,1) -> (5,2)`. EDGES ARE ASYMMETRIC: `(4,2)->(4,1)` exists but
`(4,1)->(4,2)` does not; `(5,1)->(5,2)` exists but `(5,2)->(4,2)` does not.
This KILLS the coordinate-adjacency model (ANSWER4's `FUN_001c5bb4` coordinate lookup
cannot express one-way edges). The graph must store PER-NODE NEIGHBOR LISTS (ids, not
coordinates). `FUN_001b8728(D, ox, oy)` (neighbor search from origin) is the prime
routine to dissect.

Also: single active marker at (6,2) in the 32-slot `R+0x114` array (= `[T+4]`, count 1).
Visually every tile shows an identical `2` coin -- tile TYPE is invisible.

## Questions (vaddr evidence for each)

1. **Node graph structure.** Find the array of ALL board tiles (not the 1-entry marker
   table): each node's coordinates, and above all its NEIGHBOR LIST (which edges exist,
   including one-way ones). Seeds: `FUN_001b8728`'s reads; callers of `FUN_001c5db0(T,id)`;
   what `FUN_001c218c`/`FUN_001c418c` tick besides D. Deliver: array base rule (holder
   chain from `M`), stride, node struct layout (coord offsets, neighbor-list offset/count).
2. **Tile types + object catalog.** Token tiles vs chests vs battles vs exits (Stigma of
   Chaos) vs home spots: where is TYPE stored (node field? separate object table? the
   `R+0x114` marker entries' unused bytes?). The marker at (6,2): what object does it
   denote -- read its full 0x10 bytes' meaning from the code that consumes markers.
   Deliver: type-ID values observed in code (not guesses), and which table the adapter
   should read for "what is here".
3. **Block reason encoding.** When lookup/neighbor search rejects `(5,2)`-from-west: is it
   "no node at coordinate", "node flagged", or "neighbor id absent from list"? The adapter
   must distinguish "edge does not exist" from "locked for now" -- find the exact branch.
4. **Available-directions primitive.** Is there one function that, given the cursor node,
   returns the neighbor set (which the adapter could call/read after)? Or must the adapter
   probe up/down/left/right through `FUN_001c5bb4`-equivalents itself? Name it or rule it out.

## Rules

- Rank VERIFIED / CANDIDATE / REJECTED. No heap constants -- holder chains only.
- The operator live-tests every chain the same day: give exact RAM math + expected
  prologue-board values (e.g. "node (4,2)'s neighbor list contains (4,1)'s id but not
  (5,2)'s; node (4,1)'s list contains (5,1)'s id but not (4,2)'s").

## Deliverable

`ANSWER6.md` in THIS directory. Static analysis only, no emulator, no ROMs.
