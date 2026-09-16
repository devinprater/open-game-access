# Dragon Ball Z: Attack of the Saiyans — character table FOUND

**Status: the character table is located and its layout is measured. It is the
CHARACTER ROSTER, not the active party.** The published Action Replay addresses are
wrong, and the reason is now known exactly.

## ROM

| | |
|---|---|
| file | `Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds` |
| size | 134,217,728 bytes (128 MB) |
| internal title | **`DB KAI RPG`** — not "Attack of the Saiyans" |
| game code | **`BRPE`** |
| maker | `AF` (Namco Bandai) |
| revision | 0 |

⚠️ GameTDB lists this game as **`BRPP`**, but this ROM is **`BRPE`**. The internal
title (`DB KAI RPG`) suggests shared lineage with the Japanese *DB Kai* title. Both
are evidence that published code lists may target a different build.

## Why the published addresses read zero

**The base address in the code lists is wrong** — not absent structure. The table
exists and is populated from the very start.

## ✅ The verified layout

**Eight** consecutive character records, **stride `0x24C`** — exactly the stride the
Europe code list claims via `DC000000 0000024C`:

| # | character | name address | # | character | name address |
|---|---|---|---|---|---|
| 0 | Goku | `0x020CD774` | 4 | Tien | `0x020CE0A4` |
| 1 | Gohan | `0x020CD9C0` | 5 | Yamcha | `0x020CE2F0` |
| 2 | Piccolo | `0x020CDC0C` | 6 | Bubbles | `0x020CE53C` |
| 3 | Krillin | `0x020CDE58` | 7 | Gregory | `0x020CE788` |

Every gap is `0x24C`. Names sit at **+0x20** in each record; the record head is the
name followed by zeros (no per-slot flag bytes at the head).

Derived array base: **`0x020CD754`** (name address − 0x20).

⛔ The code list's base is `0x020CD300` — **0x454 too low**, so every offset taken
from it lands in empty memory. That single error explains every zero reading across
all previous runs.

## ⛔ THIS IS THE ROSTER, NOT THE PARTY — and that is a correction

The four-record scan initially found Goku, Gohan, Piccolo and Krillin, which looked
exactly like a starting party. It was not, and two facts corrected it:

1. **All four had populated stats at frame 0** — but this game opens with **Goku
   alone**. Gohan, Piccolo and Krillin join much later. A real active-party array
   cannot have four members at that point.
2. **A wider scan found eight records**, including **Bubbles** (King Kai's monkey)
   and **Gregory** (his cricket). Those two are *never* playable. A table holding
   them is a character database, not a party.

⛔ **The lesson: "how many records are there, and who is in it" settles
roster-vs-party — and it is far cheaper than a battle.** The name list is
self-diagnosing: the moment Bubbles and Gregory appear, the question is answered.
A truncated scan window had hidden entries 4–7; the four-record result was an
artifact of stopping at `0x020CE000`, which the first four records already overran.

## Stat field candidates (names NOT confirmed)

Read as u32. Values appear **three times consecutively** (current / max / a display
copy — the normal shape for a DS RPG), then the next stat 0x10 later:

| character | `name+0x1D8` (+1DC, +1E0) | `name+0x1E8` (+1EC, +1F0) |
|---|---|---|
| Goku | 290 | 95 |
| Gohan | 660 | 225 |
| Piccolo | 300 | 105 |
| Krillin | 320 | 110 |

Plausibly **HP** and **Ki**. But given the roster finding, these most likely encode
**character base/derived stats** rather than live party HP — the live values will be
computed and stored elsewhere.

⛔ **What is confirmed is the LAYOUT, not the field meanings.** Confirming a field
means changing exactly one value in-game and re-reading — never accepting a
plausible-looking number.

### A duplicate hunt that raised more questions than it settled

Searching all main RAM for Goku's value `290` (`0x122`) found it at:

```
0x02054A10   0x020CD94C   0x020CD950   0x020CD954   0x02195658
```

The three at `0x020CD9xx` are inside **Gohan's** record (base `0x020CD9C0`, so
`0x020CD94C` is *before* it) — i.e. Goku's value at `0x020CD774+0x1D8` and the trio
at `0x020CD9xx` sit on the same `0x24C` lattice as the names, so they are the same
field across different records with **coincidentally equal values**. That is a
warning about the scan method, not a discovery: with `0x24C`-spaced records, any
repeated value lands on the lattice.

## ⛔ The method that worked, and the one that wasted a run

**What failed:** scanning 128 KB for an address whose value was "plausible AND
different" at `base`, `+stride`, `+2*stride`. It returned **348 candidates** — the
top hits (`5489 / 7240 / 4279`) were graphics data. Plausibility is not evidence.

**What worked: scanning for ASCII STRINGS.**

```
=== string scan ===
  0x020CD774  "Goku"      0x020CDC0C  "Piccolo"   0x020CE0A4  "Tien"     0x020CE53C  "Bubbles"
  0x020CD9C0  "Gohan"     0x020CDE58  "Krillin"   0x020CE2F0  "Yamcha"   0x020CE788  "Gregory"
```

Character names are the best structure oracle in an RPG: long, self-identifying, at
a fixed offset in each record — so the spacing between them *is* the stride, measured
rather than assumed. Four hits replaced 348 useless candidates.

**Generalisable rules:**
- **When hunting an unknown structure, scan for strings first.** A name is a
  known-content anchor you can check against; a "plausible integer" is nothing.
- **Scan wide enough to overshoot the structure.** Stopping 0x94 bytes short of the
  array's end made a roster look like a 4-member party.
- **Check WHO is in the table, not just how many.** Bubbles and Gregory cannot be in
  a party; their presence identifies the table as a roster.

## ⛔ The zero-reading trap, and the screenshot that broke it

A probe reported every published address as `0` while a control address read real
data — consistent with **both** "not allocated yet" **and** "wrong address", which
demand opposite next actions. A screenshot showing a **status gauge while the
addresses read zero** converted the ambiguity into a finding: the stats exist, so
the addresses are wrong.

**Generalised: "reads zero" is ambiguous; "reads zero while the game draws the value"
is not.** Bracket every RAM claim with a screenshot at the same moment.

## Evidence

- `docs/evidence/dbz-20000-frames-intro-scene.png` — intro dialogue, past the title
- `docs/evidence/dbz-24000-frames-house-interior.png` — house interior **with a
  status gauge rendered**, the image that proved the addresses wrong

## How to reproduce

```bash
export PA_SHIM=<repo>/tools/re/platforms/gba/mgba_compat.lua   # ⛔ A FILE, not a dir
bash scripts/build-host.sh
g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I$PWD/Core -I$PWD/Sources/CPokeCore/include \
    -I$HOME/src/melonds-lua/src -I$HOME/src/lua-5.4.7/src -std=c++17 \
    -o /tmp/dbz_probe fe/dbz_probe.cpp Vendor/hostobj/*.o -lpthread -ldl -lm
DBZ_SHOT=shot.ppm /tmp/dbz_probe "<rom>.nds" 12000 fe/plans/dbz-battle.txt
```

⛔ **`PA_SHIM` is a FILE, not a directory.** Pointing it at the directory makes
`fopen` fail, the probe falls through to a bare `emu.frameadvance()` with no shim,
and it dies on `attempt to index a nil value (global 'emu')` — which reads as a
broken script rather than a missing shim. The `emu` global comes from the shim.

⛔ **Do not link `-lSDL2`** — the `POKE_HOST` platform layer uses
`clock_gettime`/`usleep`, not SDL.

## ⛔ THE ROM SETTLES IT: the RAM table is CHARACTER-ID slots, not a party

Since RAM scanning had failed three times, the search moved to the ROM. It found a
**single name table for every entity in the game** — offset table at `0x1004560`,
strings from `0x10045D8`:

```
[0] Goku        [4] Tien         [8] Saibaman        [16] Captain Robot
[1] Gohan       [5] Yamcha       [9] J. Sai          [17] Red Ribbon Spy
[2] Piccolo     [6] Bubbles     [10] C. Sai          [18] Hired Rider
[3] Krillin     [7] Gregory     [11] K. Sai          [19] Distrustful Man
                                [12] T. Sai / [13-15] Pirate Robot / Skull Robot
```

⛔ **The first eight entries of that table are EXACTLY the eight RAM records**, in the
same order, at the same `0x24C` stride:

| ID | name | RAM record | ROM table |
|---|---|---|---|
| 0 | Goku | `0x020CD774` | `0x10045D8` |
| 1 | Gohan | `0x020CD9C0` | |
| 2 | Piccolo | `0x020CDC0C` | |
| 3 | Krillin | `0x020CDE58` | |
| 4 | Tien | `0x020CE0A4` | |
| 5 | Yamcha | `0x020CE2F0` | |
| 6 | Bubbles | `0x020CE53C` | |
| 7 | Gregory | `0x020CE788` | |

**Conclusion: the RAM table is an array of character-ID slots** covering the first
eight IDs — a known/unlocked-character or stat table — **not the party.** The game's
entity table continues past ID 7 into enemies (Saibaman, the Saibamen variants,
Pirate Robot, Red Ribbon Spy, Hired Rider, Distrustful Man...), so a table holding
IDs 0–7 is a character *definition* array.

That is consistent with everything measured earlier: Bubbles and Gregory present
(never playable), populated stats at frame 0 (these are definitions, not live
state), and no pointer list (the party does not index these records).

⛔ The offset-table base is `0x10038C0` (entry `0xD19` lands on the first string with
a 1-byte rounding, so treat the exact base as unconfirmed while the *contents* are
certain). Reading the table as `base + entry` is the right shape; verify the base
before relying on any single index.

## ✅ Where the search actually stands

**Confirmed by measurement:**
- A character-definition array in RAM at `0x020CD754`, stride `0x24C`, 8 records,
  names at +0x20 — matching ROM character IDs 0–7.
- The ROM name table at `0x1004560` / strings `0x10045D8`, covering all entities.
- The published AR base `0x020CD300` is **0x454 too low** — the cause of every zero
  reading this project observed.

**Refuted (do not retry):**
1. The array is a party → **no**: it holds non-playable IDs, populated at frame 0.
2. The party is a pointer list into these records → **0 pointer-shaped words**.
3. The `0x02054A10` duplicate of 290 is a live copy → it is **ARM overlay code**
   (`F8 4F 2D E9` = `push {r3-r11, lr}`), not data.

**Still missing: the ACTIVE PARTY** — who is in it now, with current HP.

### The honest next step

Read the ROM code that references the character region. `arm9.bin` contains **169
literals** pointing into `0x020CD000..0x020CF000`, with clear clusters:

```
arm9+0x65948 -> 0x020CD04C     arm9+0x66128 -> 0x020CD634
arm9+0x65A7C -> 0x020CD0D0     arm9+0x663F0 -> 0x020CD63B
arm9+0x65DBC -> 0x020CD048     arm9+0x666BC -> 0x020CD64C
arm9+0x62770 -> 0x020CE8C0  (many references)
```

⛔ **The roster base `0x020CD754` itself never appears as a literal**, so the base is
*computed* at runtime rather than stored — which is why no pointer search could find
it. To get the party, disassemble around these literal sites (Ghidra 12.1.3 +
PyGhidra are installed; project at `C:\Users\Devin Prater\oga-ghidra`) and read the
index arithmetic. Named code beats any further RAM scan.

⛔ **Never build the adapter on the character-definition array.** It holds entities
the player cannot field; narrating their stats as party HP would be worse than
silence.
