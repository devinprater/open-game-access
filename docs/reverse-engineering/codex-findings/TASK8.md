# TASK8 — Battle participant state: HP, Bravery, EX, positions, hazards

Context: same game/build (`EBOOT.BIN.dec`, vaddr `0x00000000`; read-only static analysis, no
ROMs, no emulator). Board RAM map is DONE (ANSWER5/6/7, doc ss105-110 — preserve, no rework).
This task is the BATTLE side: no RAM map exists yet. A live battle (WoL vs Garland story
fight) runs during validation, so every address/offset you report gets checked within minutes.

Live differential (idle player takes BRV damage, 3 verified full-RAM snapshots R0/R1/R2):
persistent drops (fell, never recovered): 0x09D8EBC8: 755->243 HELD (same value at
0x09D8E944); 0x09B27974: 512->256->0; 0x09D75B78/7C pair: 1274->0; 0x08BAC79C: 350->340
->300 (slow drain). Transient drops (recovered, likely BRV churn): a 0x09C53xx stride-0x40
array (399-405 dropping 15-30) and a 0x09B3Cxxx triple (drop 733 x3, floats/pointers
nearby = effect data, NOT participants). Neighborhood of 0x09D8EBC8: render-ish struct
(ptr, floats 3.53/1.17/36.62/1.0/20.0, then 1, 2, 243, 2.0).

## Questions

1. Participant array: where do the two combatants' structs live (static holder + chain)?
   HP (current/max), Bravery (current/base), EX gauge (current/full flag) offsets?
2. Positions: XYZ float offsets per participant (for bearing + distance speech)?
3. Stage hazards: cover objects, ramps, BRV-zero traps — data source?
4. Which of the four persistent-drop candidates (if any) is real participant state?

## Rules

Verify each claim against at least two independent static references. No invented offsets.
Write `C:\Users\Devin Prater\oga-trace-codex\ANSWER8.md` when done.