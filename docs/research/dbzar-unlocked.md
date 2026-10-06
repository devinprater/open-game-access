# DBZ Another Road — which characters are unlocked (task 3 ANSWERED)

**Devin's correction was right: `down` moves the character-select cursor, `right` does not.**
Every earlier "cursor not found" result came from pressing the wrong direction — the per-button
screen sweep had already said `down` changed the screen 83% and `left`/`right` changed nothing.

## The selectable roster: 15 characters (+ RANDOM)

Enumerated by pressing `down` and reading the name plate off the screen each time (OCR of the
"Name @ Form" plate), then confirming the list wraps:

| # | character | form shown |
|---|---|---|
| 0 | Goku | Normal |
| 1 | Teen Gohan | Normal |
| 2 | Gohan | Normal |
| 3 | Future Gohan | Normal |
| 4 | Vegeta | Normal |
| 5 | Trunks | Normal |
| 6 | Future Trunks | Normal |
| 7 | Krillin | Normal |
| 8 | Piccolo | Normal |
| 9 | Frieza | Final |
| 10 | Android #18 | Normal |
| 11 | Cell | Perfect Form |
| 12 | Majin Buu | Normal |
| 13 | Kid Buu | Normal |
| 14 | Broly | Super Saiyan |
| 15 | **RANDOM** | — |
| 16 | (wraps to Goku) | |

The wrap at 16 is the proof the list is complete: 15 selectable characters plus RANDOM.

## Locked: 9 of the 24 roster entries

Full roster is 24 (`dbzar-roster.md`). Subtracting the 15 selectable:

**Locked:** Cooler · Gotenks · Gogeta · Vegito · Pikkon · Janemba · Super Buu · Dabura · Bardock

(Note: Future Gohan is selectable even though it sits at index 18 of the name table, i.e. the
name-table order is NOT the unlock order — the game has its own display order.)

## The unlock FLAG: not found, and the searches that failed are recorded

The shape is known exactly (24 slots, 15 available / 9 locked), so it was searched directly:

| search | result |
|---|---|
| 24-slot u8, values 0/1 only, exactly 15 ones | hits, but **all in a bitmap region** (`0x08DF3xxx`-`0x08DF8xxx`) hundreds of times over — graphics data, not flags |
| **exact roster-order pattern** (`111111111110100000110001`), u8 | **0 hits** |
| the same inverted | **0 hits** |
| the same at u32 stride | **0 hits** |
| a parallel array beside the 24 name pointers | none — the name pointers are a slice of a general string table, and a 24-run of them sits at `0x08A243EC` with no flag array adjacent |

⛔ **Do not re-run the loose `9-off/15-on` scan expecting an answer.** It matches bitmap data
hundreds of times, which is coincidence; the exact roster-order test is the discriminating one
and it returns 0.

Also ruled out earlier: no static ELF reference to the name table, no plaintext in the save
(`DATA.BIN` is encrypted), and the table is resident (not rewritten by navigating menus).

## What this means practically

For a reader, **the unlock question is already answerable without the flag**: cycle
character select and the game itself tells you which characters are available. The locked ones
simply do not appear in the cycle. That is the game's own truth, and it needs no RAM address.

⛔ One caution if this is used for reading: the display order differs from the name-table order,
so a reader must use the **cycle order above**, not the roster table order, or it will name
characters wrongly.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-fullcycle.mjs` | navigates to character select and enumerates the whole roster by cycling |
| `psp-ar-csdown.mjs` | reaches character select and steps with `down`, OCRing the plate |
| `psp-ar-cscursor.mjs` | ordinal hunt for the cursor (used `down` after the correction) |
| `psp-ar-flag.mjs` | loose 15-on/9-off structural scan (records the bitmap false positives) |
| `psp-ar-flag2.mjs` | exact roster-order flag search (0 hits — the discriminating test) |
| `psp-ar-goto.mjs` | navigates with OCR verification after every press |
