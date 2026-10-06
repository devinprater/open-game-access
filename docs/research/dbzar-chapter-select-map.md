# DBZ Another Road — Chapter Select is a MAP, not a list (job A, answered structurally)

## The finding that explains every failed hunt

`FUN_00008590` is **not** a chapter-record copier — it is a **map/graph renderer**. Decompile
structure, annotated:

```c
local_40 = param_1;              // param_1 = the 320-byte chapter record
do {
    if (local_40[2] < 0) break;                  // terminator
    uVar6 = (*local_40 >> 4) & 0xf;              // NODE X  = high nibble
    uVar5 = (*local_40     ) & 0xf;              // NODE Y  = low  nibble
    if (camera_x - 2 <= uVar6 && uVar6 <= camera_x + 5 &&
        camera_y - 2 <= uVar5 && uVar5 <= camera_y + 4) {
        for (i = 0; i < 3; i++) {                // up to 3 CONNECTIONS per node
            if (pcVar2[i] != -1)
                FUN_00009004( ... node (x,y) ... target node ... );   // draw the link
        }
    }
    local_3c++;
    local_40 += 10;                              // stride 10 bytes
} while (local_3c < 0x20);                       // 32 entries
```

**So a chapter record is a 32-node map table of 10-byte entries**, each with a grid position and
up to 3 links to other nodes. `DAT_001ac2a1`/`DAT_001ac2a2` are the camera/scroll position in
that grid (the code compares nodes against them to decide what is on screen), and
`DAT_001ac2c4`/`DAT_001ac2c8` are derived screen offsets.

## Why this matters

⛔ **Chapter Select has no linear cursor, so no list-based signature can ever find one.** That is
the answer to "why did the ordinal hunts return 0 candidates" — it was never a list:

- not the engine's active-menu struct (0 hits),
- not a linear ordinal `+1/+1/-1` at u8/u16/u32 (0 candidates),
- the chapter index `DAT_001b11d5` does not move on `down`/`up`.

Pressing directions on a map moves a **position between connected nodes**, so the variable being
written is a node/position, and it can wrap, jump, or be blocked by map topology — none of which
a linear-ordinal signature matches.

## ⛔ The diff attempt failed for a reason worth recording

A full-RAM guarded diff (dump / down / dump / down / dump / up-dump) reported **58,433 words
changed across three presses** — impossible for a cursor move. The screenshots showed why: the
game was still in the **opening narration** of chapter 0 ("This world's lone surviving warrior
returned to his past…", i.e. `MSG_AR_000_00_003`), and the text scroll/typing animation churns
RAM continuously. So the diff was measuring a text animation, not input.

**Lesson:** on this game, a RAM diff is worthless unless the screen is first *confirmed settled*.
Presses do not reach Chapter Select until the intro narration is cleared, and the narration
animates constantly.

## What a reader actually needs — and already has

A story reader does **not** need the map cursor:

| what a reader needs | status |
|---|---|
| which chapter the game is in | ✅ `DAT_001b11d5` (chapter index) — verified live |
| is story mode active | ✅ `DAT_001b11d4` (AR mode flag) — verified live |
| the chapter's clear condition, in words | ✅ `MSG_AR_CLEAR_<nn>`, **now readable** via the MSG resolver |
| the story dialogue text | ✅ 7,485 messages resolved, incl. `MSG_AR_<ch>_<scene>_<line>` |
| the chapter/city names | ✅ `MSG_AR_CITY_00..23` |
| which *node* the map cursor is hovering | ❌ not found — and not needed for speech |

The only thing still missing is "which unentered chapter is highlighted on the map" — cosmetic for
a reader, since entering a chapter is what sets the index that IS readable.

## If this is pursued later: the right next step

Not another signature hunt. **Diff on a confirmed-settled Chapter Select screen only**, after
clearing the intro narration, and compare the map camera variables `DAT_001ac2a1` /
`DAT_001ac2a2` (vaddr `0x1AC2A1`/`0x1AC2A2`) — on a map, those are what move when you navigate.
Verify "settled" with a screenshot pair that is byte-identical before trusting any diff.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-chsel2.mjs` | screenshot-guarded diff (records a shot per step, so a transition is visible) |
| `psp-ar-chdiff.mjs` | first diff attempt (superseded; shows the transition-saturation failure) |
| `DisChSel.java` | decompiles the chapter-record processor → `chsel.txt` |
