# DBZ Another Road — STORY MODE ("Another Road"): the map

All from the **decompilation** (project `DBZAR`, program `EBOOT.dec`), per the workspace rule of
decomp-first. Several points are **live-verified** as marked. `RAM = 0x08804000 + ELF vaddr`.

## The mode flag — live-verified

| what | ELF vaddr | RAM | meaning |
|---|---|---|---|
| `DAT_001b11d4` | `0x1B11D4` | **`0x89B51D4`** | **Another Road mode flag** — measured **0 → 1** on entering Another Road, and stays 1 while inside |
| `DAT_001b11d5` | `0x1B11D5` | `0x89B51D5` | **chapter index** (byte) |
| `DAT_001b11d6` | `0x1B11D6` | `0x89B51D6` | second chapter argument (passed with the index to `FUN_000314a8`) |
| `DAT_001b11d8` | `0x1B11D8` | `0x89B51D8` | **chapter table**, u32 per entry |

Live read of the table (RAM `0x89B51D8`):

```
0x601C5, 0x601C5, 0x601C3, 0x601C4, 0x601C8, 0x601C6, 0x601C7, 0
```
**Seven entries, then a zero terminator.** Each is shaped `0x0006_01xx`, i.e. a small id.

## The story entry point

`FUN_000323bc` — registers the `"AR MAIN"` task (0x31) and `"AR MAIN DRAW"` (0x131).

```c
if (DAT_001b11d4 == '\x01') {                       // already in Another Road
    FUN_00140a30(uRam00007e5c, FUN_000328c4);       // chapter handler A
    FUN_001237bc(1, *(undefined4 *)(&DAT_001b11d8 + DAT_001b11d5 * 4), 0x3c);   // table[chapter]
} else {                                            // first entry
    FUN_00140a30(uRam00007e5c, FUN_000325e8);       // chapter handler B
    FUN_00140948(uRam00007e5c, 0x1e, 1);
}
```

So the **chapter id used by the game is `chapterTable[DAT_001b11d5]`**, and the two handlers are
`FUN_000328c4` (A) and `FUN_000325e8` (B).

### Chapter progression (`FUN_000325e8`, handler B)

```c
if ((uVar3 & 0x10) == 0) {          // pad flag 0x10 NOT set
    if ((uVar3 & 0x40) == 0) { ... } // pad flag 0x40 NOT set -> nothing to do
    else {                           // pad flag 0x40 set -> ADVANCE
        DAT_001b11d5 = DAT_001b11d5 + 1;
        if (DAT_001b11d5 < cRam00099dac) { ... } else DAT_001b11d5 = cRam00099dac - 1;
    }
} else {                             // pad flag 0x10 set -> GO BACK
    DAT_001b11d5 = DAT_001b11d5 - 1;
    if (DAT_001b11d5 < 0) DAT_001b11d5 = 0;
}
```
**The chapter index is clamped to `cRam00099dac - 1`** — so `cRam00099dac` is the chapter count
variable. ⛔ **But do not read `cRam00099dac` live at `0x08804000 + 0x99DAC`** — at runtime that
address lands inside `.text` MIPS instructions (measured), the same `iRam`/`cRam` artifact seen
before with `0xC139C`. Only the `.data` addresses (`0x1B11xx`) verified.

⚠️ Also: the index sat at **0** while the Chapter Select screen was browsed and did not move on
`down`/`up`, so `DAT_001b11d5` is the chapter the game is in, **not** the browse cursor.

## The `[AR]` state machine (all decompiled)

| function | size | tag | meaning |
|---|---|---|---|
| `FUN_00021ecc` | 232 | `[AR] FIELD EVENT` | field event entry; reads `DAT_001afbd3[cur]` / `DAT_001afc04[cur]` |
| `FUN_00024888` | 220 | `[AR] FIELD EVENT` | another field-event entry |
| `FUN_000243f0` | 180 | `[AR] FIELD EVENT` | reads AR struct `+0x8a` (short) |
| `FUN_0002213c` | 84 | `[AR] BATTLE MODE` | → callback `FUN_00025764` |
| `FUN_00023000` | 92 | `[AR] BTL START` | → callback `FUN_0002305c` |
| `FUN_000219e4` | 212 | `AR UPDATE GAMECLEAR` | **story chapter clear**; reads AR struct `+0x6e4` (vs `-1`) and `+0x6f0` |
| `FUN_00021ab8` | 152 | `AR UPDATE GAMEOVER` | → callback `FUN_00023634` |
| `FUN_00024d0c` | 108 | `AR UPDATE GAMEOVER` | → callback `FUN_00023920` |
| `FUN_00021940` | 136 | `AR UPDATE BACK TO MENU` | → callback `FUN_00023bc0` |
| `FUN_000287f8` | 140 | `[AR] FIELD RESULT` | two tasks (0x31 proc / 0x1c1 draw) |
| `FUN_00018420` | 140 | `[AR] CITY UPDATE` + `[AR] CITY DRAW` | city/field module |
| `FUN_0001fec0` | 140 | `[AR] PAUSE` | pause tasks |
| `FUN_000251e8` | 108 | `[AR ESCP MSG]` | escape message |

**The AR state struct base is `0x6b90`**: `FUN_000213c4()` is a 12-byte function that literally
returns `0x6b90`, and 33 functions call it to get the AR state. GAME CLEAR reads:
- `+0x6e4` — a byte compared against `-1` (unset sentinel)
- `+0x6f0` — a byte tested for non-zero

These are the closest thing found to **story progress flags**, and they are the next thing worth
reading live at chapter-clear time.

## Story content: 24 chapters

`MSG_` families decoded from `data_sys_us.afs` (`dbz-msg-all.txt`, 456 ids):

| family | count | meaning |
|---|---|---|
| `MSG_AR_FIELDPLAY_*` | 268 | in-level story dialogue lines |
| `MSG_AR_CLEAR_*` | **24** | **one per chapter clear** — 24 chapters |
| `MSG_AR_CITY_*` | 24 | city/field names |
| `MSG_AR_MISSION_*` | 5 | missions |
| `MSG_CMT_*` | 93 | comments |

The **Chapter Select** screen was reached live (main menu → Another Road → cross) and shows
`Chapter Select`, `TOTAL complete 000%`, `City DF. 000%` — i.e. chapters plus per-city
completion, on a fresh save.

## ⛔ Open: the chapter BROWSER cursor is not found

Chapter Select is a **third UI type**: it is not the engine's active-menu struct (that anchor
returns 0 hits there), and an ordinal hunt (+1/+1/-1 at u8/u16/u32, with a no-press control)
returned **0 candidates**. So the on-screen chapter cursor is still unlocated.

Likely cause, recorded so the next attempt does not repeat the mistake: the chapters on that
screen are probably a **map/grid**, not a linear list, so a linear-ordinal signature cannot match
any more than it could on character select (where the ids jumped). Next step would be to read the
screen's own structure rather than assume a list.

## Story TEXT is not in the ELF

⛔ No UTF-16 story strings exist in `EBOOT.dec` (0 found), and display text is not plaintext ASCII
in the `.afs` archives — it lives in `#MSG` containers / compiled `.MGB`s. So a story reader needs
the **MSG-ID → text resolver**, which is still open (DECOMP_INDEX §6).

## ⛔ Address pitfalls hit here (both already seen elsewhere — they recur)

- **`iRam`/`cRam` addresses from Ghidra are not trustworthy for this binary.** `FUN_000213c4`
  "returns 0x6b90" (a small constant, not an address), and `cRam00099dac` landed inside `.text`.
  Trust `.data` addresses that a live probe confirms; verify every one.
- **The `0x74` file-offset/vaddr skew** appears again: the chapter table sits at ELF file offset
  **`0x1B124C`** but its vaddr is **`0x1B11D8`**. Searching the file by vaddr finds nothing.

## Tooling

| script | purpose |
|---|---|
| `DisStory.java` | finds `[AR]` string sites + the functions referencing them (→ `story.txt`) |
| `DisStory2.java` | dumps the chapter table, the state variables and their readers (→ `story2.txt`) |
| `psp-ar-story.mjs` | reads the AR mode flag, chapter index, chapter table live |
| `psp-ar-chaptersel.mjs` | watches the story bytes while driving Chapter Select |
