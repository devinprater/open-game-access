# TASK10 — Stage geometry: collision, cover, ramps, surroundings query

Context: same game/build (read-only static analysis, no ROMs, no emulator). ANSWER8/8B
(participants) and ANSWER9 (lock/objects) are VALIDATED live (doc ss112-114 — preserve).
This task is the STAGE side, for on-demand arena-surroundings speech.

Player-confirmed needs: cover objects (to hide behind), ramps (height change),
BRV-zero traps (lose all Bravery when caught), walls/boundaries. A blind player wants,
on demand: nearest cover direction + distance, nearest ramp, nearest trap, nearest wall.

ANSWER8 already points at: stage container parsed by FUN_0006F3F4/FUN_000E5A64, chunks to
world/collision services (DAT_003923C0, DAT_00391230, FUN_00030A18, FUN_00066314),
resource records 0x7F/0x80/0x81/0xC1 in the manager at DAT_003915E0 (RAM +0x08804000).

## Ask

1. Loaded stage geometry: holder + layout for collision (walls/cover/ramps as shapes)?
2. Nearest-feature query: does the engine expose one (AI/pathing use it), or must the
adapter walk shapes? Give the cheapest validated query path.
3. Traps/gimmicks: trigger records + positions for damaging triggers (BRV-zero class)?
4. Minimal validator reads (live battle available for checking within minutes).

Two independent static references per claim. Semantic labels only where the executable
supports them; mark inference as inference. Write ANSWER10.md.
