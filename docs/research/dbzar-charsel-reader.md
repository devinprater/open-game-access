# DBZ Another Road — character-select READER (verified live)

## Result

`psp-ar-reader.mjs` now reads character select as well as menus, and it is verified following
the cursor in real time.

Live proof (reader stdout while `down` was pressed six times, then `up`):

```
character cursor @0x8ABC2E8
{"charId":5, "screen":"Select Characters", "item":"Krillin",      "pos":7}
{"charId":6, "screen":"Select Characters", "item":"Piccolo",      "pos":8}
{"charId":7, "screen":"Select Characters", "item":"Frieza",       "pos":9}
{"charId":8, "screen":"Select Characters", "item":"Android #18",  "pos":10}
{"charId":9, "screen":"Select Characters", "item":"Cell",         "pos":11}
{"charId":19,"screen":"Select Characters", "item":"Majin Buu",    "pos":12}
{"charId":10,"screen":"Select Characters", "item":"Kid Buu",      "pos":13}
{"charId":19,"screen":"Select Characters", "item":"Majin Buu",    "pos":12}   <- after `up`
```

Names are the 24-roster name-table names; `pos` is the position in the game's display order.

## Why character select needed its own anchor

The main-menu reader anchors on the engine's active-menu struct
(`FUN_000e1afc`; descriptor table `0x089EBBA0`, stride `0x14`; `+0x04` = menuId, `+0x74` = sel).
**That struct is not present on character select** — `locateMenu()` returns zero hits there, so
the reader used to report "no active-menu struct found" while standing on the screen.

Character select uses a different structure, and it has a clean re-locatable anchor:

```
anchor : the byte string  "PLAYER\0\0COM\0"        (2 occurrences in RAM)
cursor : anchor + 0x10   ->  the highlighted character's NAME-TABLE ID
```

Measured window around the cursor (`0x08ABC2E8`, this boot):

```
0x8ABC2D8  0x59414c50   "PLAY"     \
0x8ABC2DC  0x00005245   "ER\0\0"   /  the anchor string
0x8ABC2E0  0x004d4f43   "COM\0"
0x8ABC2E4  0              <- 0 sometimes here on other screens
0x8ABC2E8  id             <== CURSOR
0x8ABC2EC  0
0x8ABC2F0  0
0x8ABC2F4  1 (sometimes)  <- a related flag; NOT reliable, see below
```

The two occurrences are told apart by the words at `+0x04`/`+0x08`: the real one has `0, 0`
there, the other is a plain text copy with garbage.

⛔ **Do not require `+0x0C` to be zero.** It is `0` on some screens and `1` on others; requiring
all three trailing words to be zero silently rejected the real cursor and made the reader report
"not present" while standing on character select. Two zero words are enough (the text copy has
garbage at `+0x04`).

## ⛔ The id JUMPS — this is why every earlier cursor hunt failed

The cursor holds a **name-table id**, and character select lists the characters in the game's OWN
display order, so the value does not step by one:

| display position | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| name-table id | 0 | 1 | 2 | **18** | 3 | 4 | **23** | 5 | 6 | 7 | 8 | 9 | **19** | 10 | **11** | **12** |
| character | Goku | Teen Gohan | Gohan | Future Gohan | Vegeta | Trunks | Future Trunks | Krillin | Piccolo | Frieza | Android #18 | Cell | Majin Buu | Kid Buu | Cooler | Broly |

All earlier hunts used the signature "stable on no-press, then +1, +1, -1", which works on the
**main menu** but **cannot ever match** here: measured steps were 2 → 18 → 3 → 4 → 23 → 5.

The hunt that worked instead required the values to be **a contiguous run of the display order**
(either the id run or the position run), at u8/u16/u32. It returned 9 candidates; the real cursor
was the one whose following `up` returned the value to the previous entry.

## Correction: 16 selectable characters, not 15

`dbzar-unlocked.md` said 15. The cursor data proves **16**: the cycle run missed one because a
single OCR step came back empty. The verified set, in the order the game presents them, is the
16 in the table above — **Cooler (id 11) sits between Kid Buu and Broly** and *is* selectable.

So the split is **16 selectable / 8 locked**: locked are Gotenks, Gogeta, Vegito, Pikkon,
Janemba, Super Buu, Dabura, Bardock.

(An OCR-only count of the cycle is unreliable — one blank frame silently loses a character. Use
the id sequence, which is what the reader does.)

## Still open

The per-character unlock FLAG is still not located (see `dbzar-unlock-flag.md`); the reader does
not need it, because locked characters simply never appear in the display order.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-reader.mjs` | **the reader** — menus (menu struct) + character select (`PLAYER\0\0COM\0` anchor) |
| `psp-ar-cshunt2.mjs` | the hunt that found the cursor (display-order run, not +1/+1/-1) |
| `psp-ar-csverify.mjs` | id vs on-screen name verification |
| `psp-ar-anchortest.mjs` | anchor uniqueness + window dump |
| `psp-ar-csanchor.mjs` | earlier anchor attempt (superseded) |
| `psp-ar-fullcycle.mjs` | enumerates the roster by cycling (OCR; can lose one — prefer ids) |
