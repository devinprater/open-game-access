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

Since RAM scanning had failed three times, the search moved to the ROM — and the
ROM gives an exact, self-validating answer.

**There are TWO separate offset tables sharing ONE string blob at ROM `0x1004164`:**

| table | ROM address | entries | range |
|---|---|---|---|
| **CHARACTERS** | `0x1004224` | **8** | Goku … Gregory |
| **ENEMIES** | `0x10043BC` | **65** | Gogyo Majin … Poison Sandbug |

```
CHARACTERS  table 0x1004224  entries  8  first='Goku'      last='Gregory'
ENEMIES     table 0x10043BC  entries 65  first='Gogyo Majin' last='Poison Sandbug'
```

The 8 character IDs map **one-to-one onto the 8 RAM records**, in the same order,
at the same `0x24C` stride:

| ID | name | ROM blob | RAM record |
|---|---|---|---|
| 0 | Goku | `+0x0474` | `0x020CD774` |
| 1 | Gohan | `+0x0479` | `0x020CD9C0` |
| 2 | Piccolo | `+0x047F` | `0x020CDC0C` |
| 3 | Krillin | `+0x0487` | `0x020CDE58` |
| 4 | Tien | `+0x048F` | `0x020CE0A4` |
| 5 | Yamcha | `+0x0494` | `0x020CE2F0` |
| 6 | Bubbles | `+0x049B` | `0x020CE53C` |
| 7 | Gregory | `+0x04A3` | `0x020CE788` |

**Conclusion: the RAM array is a table of the 8 CHARACTER IDs** — the eight entries
of the character name table, no more and no fewer. It is a character *definition*
array, **not the party.** The match is exact in count, order and stride, which is
why Bubbles and Gregory were present: IDs 6 and 7 are simply the next two characters
in a table that happens to be ordered with the non-playable ones last.

### How the pair was pinned (the method matters)

Two constraints together identify a string table uniquely:
1. every offset points at a printable run;
2. **the byte before that run is NUL** — entries do not start mid-string.

Constraint 2 is what earlier attempts missed, and its absence produced garbage
(`'ivor'`, `'edic'`, `'ueen'` instead of *Survivor*, *Medic*, *Queen*). With both
constraints the solver returned **exactly one** high-scoring candidate: table
`0x1004224`, base `0x1004164`.

⛔ **A sliding window makes `base` ambiguous unless you constrain it.** Adjacent
table starts and base values are jointly self-consistent (`T+4` with `B+4` decodes
identically), so scoring on "clean strings" alone gives dozens of equally-ranked
answers. The NUL-predecessor rule collapses them to one.

⛔ **One string blob serves several tables.** Characters and enemies are separate
index arrays over the same text. Do not assume a found table is *the* entity list.

## ✅ Where the search actually stands

**Confirmed by measurement:**
- A character-definition array in RAM at `0x020CD754`, stride `0x24C`, 8 records,
  names at +0x20 — matching ROM character IDs 0–7 exactly.
- ROM: character table `0x1004224` (8), enemy table `0x10043BC` (65), shared string
  blob base `0x1004164`.
- The published AR base `0x020CD300` is **0x454 too low** — the cause of every zero
  reading this project observed.

**Refuted (do not retry):**
1. The array is a party → **no**: it is exactly the 8 character IDs, including two
   that are never playable, populated at frame 0.
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

⛔ **The character-array base `0x020CD754` itself never appears as a literal**, so the
base is *computed* at runtime — which is why no pointer search could find it. To get
the party, disassemble around these literal sites (Ghidra 12.1.3 + PyGhidra are
installed; project at `C:\Users\Devin Prater\oga-ghidra`) and read the index
arithmetic. Named code beats any further RAM scan.

⛔ **Never build the adapter on the character-definition array.** It holds entities
the player cannot field; narrating their stats as party HP would be worse than
silence.
