# TASK4 — Dissidia story-board state: DP, cursor, tiles, movement validation

## Context

Dissidia Final Fantasy, PSP (ULUS10437). EBOOT decrypted: ask the operator for its path
(`EBOOT.BIN.dec`, 4.7 MB, MIPS, load base 0; RAM = vaddr + 0x08804000). Prior Ghidra reports in
`C:\Users\Devin Prater\oga-ghidra-dissidia\`. Full story: `C:\Users\Devin Prater\open-game-access\docs\reverse-engineering\dissidia-final-fantasy.md`
(sections 97-101 latest; pause-menu cursor solved in s101, commit 25ccc17 — PRESERVE, do not redo).

## What the game shows (verified live, screenshots + display-diffed inputs)

- Story board: 3D grid of tiles with tokens, one character piece, a glowing highlight tile.
  HUD: "Destiny Points" (DP, movement allowance) + "LEVEL BONUS ... gil".
- D-pad moves a HIGHLIGHT/CURSOR freely in 4 directions even at DP 00 (up/down and left/right
  are exact inverses — verified by screenshot diffs returning to ~0 px). DP 00 blocks NOTHING
  about the cursor.
- Pressing a direction with DP 01 once moved the PIECE one tile east, spent DP 01 -> 00, changed
  LEVEL BONUS 300 -> 100 gil, and left the highlight on the origin (west) tile. So: cursor move
  and confirmed piece movement are DIFFERENT things; one direction press can confirm a move.
- Unusual behavior (unverified mechanism): moving right sometimes stops; the player must move
  down then right to continue. Hypotheses: node-graph movement, obstacles, path-following,
  per-tile direction tables. Test, don't assume.

## Live-memory evidence (all RAM = vaddr + 0x08804000; all display-verified)

1. DP candidates: TWO words `1 -> 0` on the confirmed move, at RAM `0x08C0403C` and `0x08C0413C`
   (vaddrs `0x0004003C`, `0x0004013C`; 0x100 apart, both isolated in zero neighborhoods).
   HUD showed DP 01 -> 00. Which is DP? What is the other (movement allowance? undo flag?)?
   Who WRITES them (find writer functions — the adapter needs the DP address)?
2. On the confirmed move, three words converged: RAM `0x096C0540`: 57 -> 58,
   `0x096C0B90`: 59 -> 58, `0x096C2560`: 57 -> 58. What are these (tile indices? animation?)?
3. `0x09B3FED8` (vaddr `0x0013BED8`) is a TRAP: small int that LOOKS input-driven
   (7 -> 13 -> 11 -> 8 -> 5 across cursor moves) but hands-off reads show a free-running
   down-counter (...15,14,13,11,8,6,4,3,1,15,14,12 over 12s, mod-16-ish). Do NOT propose timers
   without a no-input control.
4. Cursor hunts (11 runs) all blank: no wrapping small-int word in 24 MB; no index/count pair
   (`[X-0x204]` in range + `[X]` = count 2..8) tracking highlight moves on pause OR mode-select
   menus; no one-hot flag with wrap rhythm. The board cursor is likely NOT a plain RAM ordinal —
   suspect world-coordinate floats, a node id in a tile struct, or derived state.
5. `0x096BBxxx` band (vaddr `0x0016Bxxx`), stride ~0x270, mostly float matrices + sibling
   pointers (`0x096BB460/580/5A0/5E0/5C0`-style links) = render scene graph, NOT board logic.
   But: something OWNS those nodes. Follow the owner pointers toward the logic-level tile/piece
   objects instead of scanning blindly.
6. UD-only float words `0x08DF5EA8`, `0x08DF61C8` (vaddrs `0x000F1EA8`, `0x000F21C8`): 3.6 -> 1.8
   on up/down; `0x096C1BA0`, `0x096C1C60`: -13.0 -> -9.0. Cursor-follow camera Y, or cursor Y
   itself? Determine by structure, not value.

## Questions (answer each with vaddr evidence; validate against the evidence above)

1. **DP variable.** Starting from the "Destiny Points" UI string (UTF-16LE in RAM pool
   `0x09D16A68+`; ASCII art assets reference it too), find the code that formats/reads the DP
   HUD value. Name the DP variable's vaddr, its size, and every function that WRITES it
   (spend/restore paths). Resolve which of `0x0004003C` / `0x0004013C` is DP and what the other is.
2. **Board state struct.** Find the story-board manager: look for the tile/node table (array of
   structs with type/position/connection fields), the piece object (current tile/node id), and
   the cursor object (highlighted tile/node id). Good seeds: string refs ("Destiny", " storypoint.stp",
   `talkevent/`, region paths in the ELF), the render-node owners from (5), cross-xrefs to the
   DP writers from (1). Deliver: struct layouts with field offsets, array base + stride + count.
3. **Movement validation.** Find the function that decides whether a direction press moves the
   cursor (and where). Does it consult a per-tile direction table / adjacency list / node graph?
   Deliver the function vaddr + the table it reads. This directly answers the right-down-right
   behavior: read the table, don't guess.
4. **Confirmed-move commit.** Find what a direction press does when DP > 0: the function that
   moves the piece, spends DP, and updates the origin highlight. Its reads/writes name the
   player-position and movement-origin variables (Phase 1 Q1/Q2/Q4).

## Rules

- A value that "looks like" a cursor/DP is NOT an answer: propose the WRITER function and the
  struct it lives in. No-input controls beat pattern-matching (see trap in (3)).
- Prefer pointer chains from static holders (vaddr constants) over heap addresses: the adapter
  can only use what is re-discoverable per boot.
- Rank every proposal: VERIFIED (code-proven) / CANDIDATE (one sighting) / REJECTED (with reason).

## Deliverable

Write `ANSWER4.md` in THIS directory: per-question answers with vaddr citations, struct layouts,
the DP writer functions, the movement-validation function + table, and a ranked live-test list
(exact RAM addresses + expected behavior for each, so the operator can verify in minutes).
Static analysis only — no emulator, no ROMs.
