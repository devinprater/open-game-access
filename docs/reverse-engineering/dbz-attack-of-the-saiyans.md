# DBZ: Attack of the Saiyans — the PARTY ARRAY is SOLVED

## ✅✅✅ SOLVED: `0x020CC774` is the ACTIVE PARTY — confirmed against the game's own screen

The investigation is closed on its main question. The pointer array **is the party**,
and it was confirmed by reading the game's own **Status** screen and matching the names
exactly.

**The game's Status screen** (`docs/evidence/dbz-status-screen.png`) shows:

```
[Status]
   Krillin   AP 0
   Tien      AP 0
   Yamcha    AP 0
```

**The array, decoded with the corrected base:**

```
0x020CC774  0x020CDD44  = base + 0x6E4  (rec3, exact)  -> Krillin
0x020CC778  0x020CDF90  = base + 0x930  (rec4, exact)  -> Tien
0x020CC77C  0x020CE1DC  = base + 0xB7C  (rec5, exact)  -> Yamcha
0x020CC780  0x00000000  NULL terminator
count word 0x020CC794 = 0x00030003   (low u16 = 3 = entry count)
```

**Krillin, Tien, Yamcha — the same three, in the same order.** Not an approximation:
every pointer lands *exactly* on a record base (`offset % 0x24C == 0`), the count word
agrees, and the names match the screen.

## ⛔ THE BUG THAT HID THIS: the record base was one stride too high

| | value | consequence |
|---|---|---|
| **correct** record base | **`0x020CD660`** | name field at **`+0x114`**; pointers hit `rec3,4,5` |
| what I had been using | `0x020CD754` | name field at `+0x20`; pointers "hit" `rec2,3,4` |

I derived the base as `name_address − 0x20` after *assuming* the name field sat at
`+0x20`. The real offset is **`+0x114`**, so my base came out **exactly `0x24C` (one
whole record) too high** — and since the stride is `0x24C`, every decode landed on a
*plausible* record, just the wrong one.

⛔ **That is why the mistake survived so long: an off-by-one-stride error is invisible
under a stride-sized lattice.** Each pointer still decoded to a real character with a
real name and real stats. Nothing looked broken. Only comparing against a screen that
*names its contents* exposed it.

### ⛔ The lesson

**Do not derive a structure's base from an assumed field offset.** I picked `+0x20`
because "the name is 0x20 bytes in" *looked* right, and every later measurement
inherited that guess. Derive a base only from something that does not depend on another
guess — here, the on-screen party list.

**And: a structure whose fields look plausible can still be off by a whole record.**
When records are a fixed stride apart, an off-by-one-stride error produces valid-looking
data at every slot. The only reliable check is an external ground truth — the game's own
screen.

## ✅ The confirmed layout

```
character record array
  base    0x020CD660
  stride  0x24C
  count   8   (Goku, Gohan, Piccolo, Krillin, Tien, Yamcha, Bubbles, Gregory)
  name    +0x114   (NUL-terminated ASCII)

party pointer array
  at      0x020CC774
  format  NUL-terminated array of pointers to character record BASES
  count   0x020CC794, low u16 == number of entries
```

| rec | base | name field | name |
|---|---|---|---|
| 0 | `0x020CD660` | `0x020CD774` | Goku |
| 1 | `0x020CD8AC` | `0x020CD9C0` | Gohan |
| 2 | `0x020CDAF8` | `0x020CDC0C` | Piccolo |
| 3 | `0x020CDD44` | `0x020CDE58` | Krillin |
| 4 | `0x020CDF90` | `0x020CE0A4` | Tien |
| 5 | `0x020CE1DC` | `0x020CE2F0` | Yamcha |
| 6 | `0x020CE428` | `0x020CE53C` | Bubbles |
| 7 | `0x020CE674` | `0x020CE788` | Gregory |

⛔ **The published Action Replay base `0x020CD300` is `0x360` below the true base
`0x020CD660`** — which is why every published address read exactly zero.

## ✅✅✅ LIVE STATS LOCATED AND VERIFIED

With the RAM dump and the **Status screen captured in the same run**, the party member
stats are confirmed by direct match — no inference:

**Ground truth** (top screen, same run) — `docs/evidence/dbz-status-krillin.png`:

```
Krillin     LV  1
HP          300 / 300
KI          105 / 105
EXP           0
NEXT         70
AP            0
```

**The Krillin record, `base 0x020CDD44`:**

```
+0x0A0  u16 = 300     HP   (triple: current / max / display copy)
+0x0A4  u16 = 300
+0x0A8  u16 = 300
+0x0B0  u16 = 105     KI   (same triple shape)
+0x0B4  u16 = 105
+0x0B8  u16 = 105
+0x240  u16 =  70     NEXT
```

**Exact match on all three values** — HP 300, KI 105, NEXT 70.

**Cross-checked against the rest of the party** — each member has its own distinct,
plausible values, which is what a real per-member stat block looks like:

| member | record | `+0x0A0` HP | `+0x0B0` KI | `+0x240` NEXT |
|---|---|---|---|---|
| Krillin | rec3 | 300 | 105 | 70 |
| Tien | rec4 | 320 | 110 | 79 |
| Yamcha | rec5 | 305 | 100 | 61 |

⛔ **The stats live at `+0x0A0` (HP) and `+0x0B0` (KI) inside each character record** —
relative to the record base `0x020CD660 + n*0x24C`. Not at `+0x1F8`/`+0x208`, which the
earlier (wrong-base) analysis had reported; those values (290/95 etc.) were the *next
record's* HP/KI seen through a one-stride-shifted window. **That is a clean illustration
of the base bug: the wrong base made a plausible-looking stat field out of a
neighbouring record.**

**Stat block layout (u16, each value stored three times):**

```
+0x0A0  HP    cur / max / copy
+0x0B0  KI    cur / max / copy
+0x240  NEXT  (EXP to next level)
```

**AP** showed 0 on the Status screen and was not located; `+0x240` is **NEXT**, not AP
(Krillin reads 70 there, matching NEXT).

## ✅ The published Action Replay codes, explained

The published base `0x020CD300` is **`0x360` below** the true base `0x020CD660`, so
every published address read zero. The published list is not wrong about *what* is in
this region — party stats at a `0x24C` stride — it is wrong about *where*.



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

## ✅ The offline workflow (this is the durable win)

Every question about this structure had been costing a **full probe run** — ~5
minutes to boot a 128 MB ROM and drive 12,000 frames — and each run could answer
exactly one question because the formatting was compiled in.

The probe now writes raw RAM on request:

```bash
DBZ_DUMP=out.bin DBZ_DUMP_BASE=0x02000000 DBZ_DUMP_LEN=0x400000 \
  /tmp/dbz_probe "<rom>.nds" 12000 fe/plans/dbz-battle.txt
# -> "ram dump: out.bin  base=0x02000000  4194304 bytes"
```

and `scripts/oga-dbz-analyze.py` answers structural questions against that file in
milliseconds:

```bash
python3 scripts/oga-dbz-analyze.py out.bin                      # record dump + field profile
python3 scripts/oga-dbz-analyze.py out.bin --strings            # every ASCII run
python3 scripts/oga-dbz-analyze.py out.bin --refs 0x020CD754    # who points at an address
python3 scripts/oga-dbz-analyze.py out.bin --scan 0xC0000 0x20000
```

⛔ **Separate collection from analysis.** One 5-minute run now feeds unlimited
offline queries. Reach for this before adding another `printf`.

⛔ `--rec` takes the **record base**, not the name address. Passing `0x020CD774`
(Goku's *name*) instead of `0x020CD754` (his *record*) shifts every field by 0x20 and
makes ASCII read as u16 garbage — which looks like "the structure isn't there"
rather than an argument mistake.

## ✅ Measured record layout (8 records, stride 0x24C)

| offset | meaning | evidence |
|---|---|---|
| `+0x010` | zero in all 8 | — |
| `+0x014` | `[4,1,14,1,1,1,20,20]` | not yet interpreted |
| `+0x020` | **name**, NUL-terminated | `"Goku"`, `"Piccolo"`… |
| `+0x180` | **next index — a linked list** | `[2,3,4,5,6,7,8,0]` |
| `+0x182` | `[0,2,0,2,1,1,0,0]` | not yet interpreted |
| `+0x1F8` | stat triple copy 1 (u32 ×4) | see table |
| `+0x208` | stat triple copy 2 (u32 ×3) | see table |

**The `+0x180` chain is the clearest structural signal found:** `[2,3,4,5,6,7,8,0]`
walks Goku→Gohan→Piccolo→Krillin→Tien→Yamcha→Bubbles→Gregory and terminates at `0`.
That is a **linked list / ordered index chain over the character IDs**, not a party
roster — a menu or "known characters" list.

### The stat values

| char | `+0x1F8` | `+0x208` |
|---|---|---|
| Goku | 290 | 95 |
| Gohan | 660 | 225 |
| Piccolo | 300 | 105 |
| Krillin | 320 | 110 |
| Tien | 305 | 100 |
| Yamcha | 600 | 50 |
| Bubbles | 600 | 150 |
| **Gregory** | **0** | **0** |

```
hp seq: [290, 660, 300, 320, 305, 600, 600, 0]
ki seq: [ 95, 225, 105, 110, 100,  50, 150, 0]
```

⛔ **Gregory reads 0 while every other character has non-zero values.** That is either
"Gregory is not implemented as a fighter" or "unpopulated slot" — and it is exactly
the kind of detail that would produce a wrong narration if the table were read as a
party. It is recorded as an anomaly, not explained away.

⛔ **These are most likely character BASE stats, not live state.** The ROM contains
only a string table, no `0x24C` record structure, so the records are built at
runtime — and the values being round, tidy per-character constants (290/95, 660/225,
600/50) fits derived-from-definition better than current-HP-in-play. **Confirming
that requires changing a value in-game** (take damage, re-read) — never by
plausibility. This is the cheapest outstanding experiment and it settles it.

⛔ The earlier claim "`+0x1DC`/`+0x1F0` triplets" came from a **record base that was
0x20 too high**. With the correct base the triples sit at `+0x1F8` and `+0x208`. Any
offset quoted from that earlier run is shifted — re-measure before relying on it.

## ⚠️ CORRECTION TO THE CORRECTION: the array at `0x020CC774` IS live state

An earlier revision called `0x020CC774` "THE ACTIVE PARTY". A screenshot showed the
game at its **opening with Goku alone**, so that was retracted — the array held
Piccolo, Krillin and Tien, who are not yet recruited.

**The retraction was also wrong.** The array is not a static template. Two runs at
different frame counts prove it:

```
f=2000  (title screen) : 0x020CD660 0x020CD8AC 0x020CDAF8 0x020CDD44 0x020CDF90 0x020CE1DC
f=12000 (in game)      : 0x020CDD44 0x020CDF90 0x020CE1DC 0x00000000
count word @0x020CC794 : 0x00060003 (f=2000)   ->   0x00030003 (f=12000)
```

### ✅ The full time series — one run, sampled every 2000 frames

Two snapshots cannot distinguish a monotonic queue from a fluctuating list, so the
probe now samples the list every 2000 frames **within a single run**:

```
[list]  f=0      count32=0x00000000  n=0
[list]  f=2000   count32=0x00060003  n=6   entries: <garbage>,0,1,2,3,4
[list]  f=4000   count32=0x00030003  n=3   entries: 2,3,4
[list]  f=6000   count32=0x00030003  n=3   entries: 2,3,4
[list]  f=8000   count32=0x00030003  n=3   entries: 2,3,4
[list]  f=10000  count32=0x00030003  n=3   entries: 2,3,4
```

**What this establishes:**

1. **Empty at f=0**, populated by f=2000 → the list is **built during startup**, so
   it is engine state, not a compile-time constant.
2. **It shrinks exactly once (6 → 3), then is completely stable** for the rest of the
   run. It does **not** fluctuate.
3. **At f=2000 the first slot is garbage** (a pointer that decodes to a nonsense
   record index) while the other five are consecutive records `0,1,2,3,4`. A real
   party would never contain a garbage entry — this is an **initialisation
   artefact**, not game state.
4. **`0x020CC794` is a count word**: its low u16 equals the entry count exactly
   (`0x0006` → 6, `0x0003` → 3). The high u16 stayed `3` throughout.
5. **The stable post-init list is records 2, 3, 4 = Piccolo, Krillin, Tien.**
6. **The rest of the character table is byte-identical** across runs and across the
   whole time series (names, `+0x180` chain, `+0x1F8`/`+0x208` stats).

### 📷 The run's actual trajectory (filmstrip — verified)

One capture per sample point, paired with the RAM reading at the same frame
(`docs/evidence/dbz-run-filmstrip.png`, a 3×2 contact sheet):

| frame | list | what the screen shows |
|---|---|---|
| 0 | n=0 | black — nothing rendered yet |
| 2000 | n=6 `<garbage>,0,1,2,3,4` | **Shenron logo / splash** |
| 4000 | n=3 `2,3,4` | cutscene interior; dialogue box starting |
| 6000 | n=3 `2,3,4` | narration: *"Together, they successfully resurrected Shenron, the eternal dragon"* |
| 8000 | n=3 `2,3,4` | interior, no dialogue box |
| 10000 | n=3 `2,3,4` | interior |

⛔ **This resolves a long-standing confusion in this project.** The game is in its
**opening cutscene by ~f=4000**, not "past the title into play" much later. The
filmstrip makes the sequence explicit: black → Shenron splash → interior/dialogue.

### ⚠️ CORRECTION: the "Goku's house" identification was WRONG

Earlier revisions of this document (and several progress reports) described the
opening scene as **Goku's house** with **Krillin** speaking. A later filmstrip with
bigger, later panels shows the truth:

- the speaker is **Launch** (blue hair, name box reads `Launch`)
- the line is ***"Come on, everyone! Master Roshi's waiting for you!"***
- so the location is **Kame House** (Master Roshi's island), not Goku's house
- the earlier "Krillin speaking / Brrr! Sure is a cold one today" reading was also
  taken from a small, early panel and is **not** what this run shows

⛔ **Method lesson: identifying characters and dialogue from small, low-resolution
panels is unreliable, and the mistake survived into several reports.** The fix is
mechanical, not attentiveness: **capture larger and later panels, and read one panel
at a time at full size** rather than a contact sheet of six tiny images. A 64×192 DS
screen upscaled 2× is not enough to read a name box; upscaled 4× and cropped it is.

### ✅✅✅ SOLVED: the cutscene is advanced by TOUCH, and the probe never called it

**This is the answer to the long-standing "nothing advances the cutscene" problem.**

The core exposes `poke_touch(core, x, y, down)` — a documented, exported symbol in
`Sources/CPokeCore/include/pokecore.h` — and **`fe/dbz_probe.cpp` never called it.**
Every run in this investigation was **button-only**, so an entire input class was
untestable by construction. A touch-gated screen looks *exactly* like what was
observed: no button does anything, the screen cycles, the run never advances.

**The probe now parses `TAP <frame> <x> <y>`** in plans and emits a press plus a
release 8 frames later (the core samples input once per frame, so a zero-length tap
would be missed).

**Objective result — the hash test, which needs no interpretation:**

| plan | distinct screens over 24,000 frames |
|---|---|
| dense B presses only | **2** (a two-frame loop) |
| **with TAP events added** | **10** |

New, distinct screens appear at f=8000, 10000, 12000, 20000, 22000, 24000 — the game
**moves forward** where it previously looped.

**And the content confirms it is real story progression:**

```
f=20000  "He revived all of his friends who had lost
          their lives against King Piccolo.          (A)"

f=24000  "With that task done, Goku dedicated himself
          to training under Kami's watchful eye...   (A)"
```

Two *consecutive* narration beats — revive friends, then "with that task done" —
which is forward movement through the cutscene, not a cycle.

### ✅ The prompt reading: it is **A**

At this size the icon in the dialogue box reads as **Ⓐ** (a small dark circle with a
light `A`). The earlier claim that it read **B** is retracted; the honest position is
that a single glance at this glyph is unreliable — which is why the *hash test*, not
the glyph, is what established the behaviour.

### ⛔ Why this took so long — the real lesson

**The probe's input surface was narrower than the console's, and nothing said so.**
`poke_touch` existed, was exported, and was documented in the header; the probe
simply never wired it. Every "no button works" conclusion was therefore drawn from an
incomplete input model, and no amount of trying *different buttons* could ever have
found it.

**Generalisable rule: before concluding a game ignores input, prove your harness can
send every input the console has.** Enumerate the core's input API and diff it against
what the probe actually calls. Here that diff was one function, and it was the whole
blocker.

**Second rule: hash the frames to test progression.** The 2-vs-10 distinct-screen
comparison is what proved touch works; reading panels had already produced two wrong
answers on this same question.


## ✅✅✅ IN GAMEPLAY — reached by TOUCH input

After wiring `TAP`, a 120,000-frame run with alternating A presses and centre taps
reached **actual gameplay**:

```
input plan : fe/plans/dbz-finish.txt (538 key events, 263 taps)

distinct screens across 120,000 frames: 60 captured, ALL UNIQUE
   75a09f2801  x1  first=f090000  last=f090000
   f125b5588f  x1  first=f092000  last=f092000
   ...
   0dc115f01a  x1  first=f118000  last=f118000
```

⛔ **Every captured screen is unique — not one repeat.** Compare the button-only runs,
which produced exactly **two** alternating screens. The game is now progressing
continuously, and the hash test proves it without any interpretation.

**Final state** (`docs/evidence/dbz-gameplay.png`): an **isometric interior** — a
bedroom with furniture, two character sprites standing on the floor, a doorway, and a
**status gauge across the bottom of the screen** (orange/red bar with a round icon at
the left). This is the game's play state, not a cutscene and not a menu.

⚠️ **The two sprites have not been identified.** At this resolution they cannot be
named reliably, and sprite-identification has already produced one wrong claim
(Kame House) in this document. What is asserted is only what the image shows: an
isometric room, two figures, and a status gauge.

## ✅✅✅ THE MAIN MENU — found by capturing the BOTTOM SCREEN

**Two harness gaps, not one, were blocking this investigation.** The first was missing
touch input. The second: `poke_framebuffer(core, screen, ...)` takes a **screen index**
and the probe only ever asked for **screen 0**. On the DS the bottom screen is where
RPGs put their menus — so half the console's output was never captured in any run.

**Both screens are now written** (`f004000_top.ppm` / `f004000_bot.ppm`).

### The bottom screen, in gameplay, before the menu

```
[L] Capsule help display                    [START] Menu
   + two large RED and GREEN touch buttons
   + a background board showing character names
```

Two facts fall straight out of it:

1. **`[START] Menu`** — the game's own label says how to open the menu. This is the
   screen telling you its controls, and it had never been looked at.
2. **Big red/green touch buttons** — touch affordances, consistent with the finding
   that touch is a primary input on this game.

### After pressing START: the MAIN MENU

`docs/evidence/dbz-main-menu.png`:

```
                       MAIN MENU
   [icons: equipment, skills, ...]      > Items
                                        ...
   "View Items and their usage"
   currency counters, top right
```

A radial icon menu with **MAIN MENU** labelled, a highlighted **Items** entry and the
description line *"View Items and their usage"*. **This is the menu the investigation
needed** — and it is reachable in one run.

⛔ **The `0x020CC774` list still reads `2,3,4` through all of this** — before the menu,
during it, and after (f=102000–108000, count `0x00030003`). So the list is **not**
tracking which screen is open. It remains unexplained, and the next step is to navigate
this menu to a **status/party page** that displays member stats, then compare.

### ✅ The MAIN MENU is a RADIAL dial, navigated with the D-PAD

Reading the menu at full size (`docs/evidence/dbz-main-menu.png`) shows its structure:

```
        [skill: flame]  [equip: shirt]        currency counters (top right)
                     \   ^
    [icon]            [*]  |  pink UP arrow     > Items
          MAIN MENU   dial |                       "View Items and their usage"
                     /   v
        [capsule]      [bottle]  pink DOWN arrow
```

A **rotating dial**: the selected entry's icon sits in the centre circle, the entry
name appears in the banner on the right (`> Items`), and the description line sits at
the bottom (`View Items and their usage`). **Pink up/down arrows** beside the dial
mark the rotation direction — so the d-pad rotates the selection, it does not move a
cursor.

**Verified by navigating it.** A run that pressed START and then DOWN/UP/RIGHT across
the menu produced **38 distinct bottom screens** in the menu window (f=100000–158000,
every sample unique — no cycling). The selection demonstrably moved: at f=158000 the
centre icon is a **gear** and the banner reads **`Options`** with the description
*"Adjust the game's options"*, where it read `Items` at f=108000.

**So the menu is fully drivable**: START opens it, the d-pad rotates it, and A opens
the selected entry. What remains is to rotate to a **party/status** entry and open it.

⛔ **The `0x020CC774` list still reads `2,3,4` (count `0x00030003`) throughout the
entire menu session** — f=100000 through f=158000, unchanged. So the list is not
tracking the open menu, the highlighted entry, or the screen. It is still unexplained.

### ⛔ The lesson: enumerate the HARNESS SURFACE, not just the API you remember

Two independent omissions, both invisible without a deliberate audit:

| capability | exported? | was the probe using it? |
|---|---|---|
| `poke_touch(core,x,y,down)` | yes | **NO** — every run button-only |
| `poke_framebuffer(core, screen, …)` | yes | **only screen 0** — bottom screen never captured |

Both were "documented, exported, and unused". Neither produced an error. Both looked
exactly like facts about the game — "no button works", "the game shows a cutscene" —
when they were facts about the harness.

**Rule: diff the core's exported surface against what the harness calls, in both
directions — input AND output — before drawing conclusions about a game.**


```
[list] f=112000 count32=0x00030003 n=3 2,3,4
[list] f=118000 count32=0x00030003 n=3 2,3,4
```

The same stable value seen in earlier runs (Piccolo, Krillin, Tien) with the same
count word `0x00030003`. **This is now observed in a run that is demonstrably in
gameplay**, which strengthens — but still does not confirm — the reading that the list
holds a character roster for the current story stage. Confirming it needs a **party
menu screenshot**, and the input tooling to reach one now exists.

### ⛔ What this cost, and the durable lesson

The blocker was never the game. **The probe could not send touch input**, so a
touch-gated opening was untestable and every "no button works" conclusion was drawn
from an incomplete input model. `poke_touch` was exported and documented; a single
missing call in the harness cost many runs and produced several wrong conclusions.

**Enumerate the core's input API and diff it against what the harness actually calls,
before ever concluding a game ignores input.**


This was true *of button-only input* and is kept because it is the measurement that
isolated the bug:

```
f004000  a728d0aa4212
f006000  1a07c13df6fb
f008000  a728d0aa4212   <-- SAME AS f004000
f010000  1a07c13df6fb   <-- SAME AS f006000
...
f028000  a728d0aa4212
```

With buttons only there were exactly **two** distinct screens, alternating. Adding
`TAP` events took that to **ten**. The loop was real; the cause was the missing input
class, not the game.


**Test: hash every captured panel and compare.** It needs no interpretation at all.

```
f000000  2b706b878cb2
f002000  82da8f6d3c46  CHANGED
f004000  a728d0aa4212  CHANGED
f006000  1a07c13df6fb  CHANGED
f008000  a728d0aa4212  <-- SAME AS f004000
f010000  1a07c13df6fb  <-- SAME AS f006000
f012000  a728d0aa4212
f014000  1a07c13df6fb
f016000  a728d0aa4212
f018000  1a07c13df6fb
f020000  a728d0aa4212
...
f028000  a728d0aa4212
```

**There are exactly TWO distinct screens after f=4000, and they alternate.** The game
is **cycling between two frames** — it is not advancing, not reading my presses, and
not moving to new dialogue. Everything from f=4000 to f=28000 is the same two images.

**What this means:**

1. ⛔ **The cutscene does not advance at all** under any plan tried — 10-frame B
   presses, 30/60/120-frame B holds, dense 40-frame B holds every 200 frames, A
   presses, START, X, Y. All produce the same two-frame loop.
2. ⛔ **My "longer holds advanced it" claim was a MISREADING.** I compared text
   between two panels that are the two alternating frames and read them as
   chronological progress. They are not a sequence — they are a cycle.
3. ⛔ **The button-icon reading is unreliable.** A 14× nearest-neighbour crop of the
   icon is genuinely ambiguous (`A` and `B` differ by a few pixels at that size). Two
   readings of the *same* icon gave different answers, so **no conclusion may rest on
   it.** Read from the image, it looked like A; the earlier glance said B.

### ⛔ The method lesson — this is the important one

**HASH THE FRAMES. Do not read them.** A byte hash answers "did the screen change"
objectively, with no interpretation, no upscaling, and no possibility of misreading a
glyph or a sentence. It took one command and immediately exposed a two-frame loop that
several minutes of image-reading had misreported as progress.

**When a run seems not to progress, hash the captures before interpreting them.**
Every image-reading conclusion in this document is weaker than the hash test, and
several were wrong.

### ⛔ What this does NOT change

The RAM-side findings stand, because they never depended on reading screens: the
character table, the ROM name tables, the AR base error, the count word, and the list
being empty before the game starts and populated once it runs. The `0x020CC774` list
reports the same values across these runs precisely **because the game itself is not
progressing** — which is now explained rather than mysterious.

### ⛔ The real blocker, stated plainly

**The scripted input cannot get this game past its opening cutscene.** Whether that is
a wrong button, a missing touch-screen input, or a cutscene that needs the second
screen is **not yet determined**, and the hash test says all current attempts produce
the same loop. Guessing the next button is not a plan.

The honest next move is to **find how this specific cutscene is dismissed** — from
documentation, a walkthrough, or by disassembling the input handler — rather than
trying more keys. Until then, no amount of running reaches gameplay, and the party
structure cannot be confirmed.

### ⛔ The method lesson

**A button prompt is a FACT YOU CAN READ OFF THE SCREEN — read it before scripting
input.** Several runs and reports were spent driving A at a screen whose own text said
B. The filmstrip plus a **3× upscale of a single panel** is what made it visible; the
same information was present in earlier 2× contact sheets and was not legible there.

**Corollary: when a run "does not progress", check whether you are pressing the button
the screen is asking for, before concluding anything about the game's state machine.**

Plan `fe/plans/dbz-menu.txt` presses through the boot gate, then tries **X**, then
**START**, then **Y**, each followed by B to clear any submenu
(`docs/evidence/dbz-menu-attempts.png`, an 8-panel sheet):

| frame | attempt | result |
|---|---|---|
| 6000 | after **X** | cutscene with dialogue box — **no menu** |
| 8000 | after **START** | **a menu box appears, top-right: `Skip` / `Back`** |
| 10000 | START menu still open | same box |
| 12000 | after **Y** | box still present |
| 14000 | later | box still present |

**Findings:**

1. **Input can open a menu on this screen** — `START` produced a two-item box
   (`Skip`, `Back`). So the probe's input path reaches menus; it is not blocked.
2. **This is a cutscene/dialogue menu, not the party menu.** `Skip`/`Back` are
   dialogue controls and the box shows **no character names** — so it cannot yet
   answer what `0x020CC774` means.
3. **`X` and `Y` produced no menu** on this screen.

⛔ **Next step is now specific: skip the cutscene (or advance dialogue far enough) and
open a real party/status screen.** That is the only screen that names its contents and
would let the character list be tested against it. The filmstrip tooling makes that a
single run — capture at each attempt and read the panels at 4× cropped.

⚠️ **Observed but NOT yet identified:** the list names Piccolo, Krillin and Tien while
the cutscene shows **Launch**. Whether the list tracks characters *present in the
scene* is a live hypothesis; it is not confirmed, and sprite/name-box identification
from small panels is exactly what just produced the Kame House misidentification.

### ✅✅ THE CONTROL EXPERIMENT — and the caveat that limits it

The missing comparison was an **idle run** — same ROM, same frame count, no input plan:

```
(a) IDLE (no plan)               (b) DRIVEN (dbz-battle plan)
[list] f=0      n=0              [list] f=0      n=0
[list] f=2000   n=0              [list] f=2000   n=6   <garbage>,0,1,2,3,4
[list] f=4000   n=0              [list] f=4000   n=3   2,3,4
[list] f=6000   n=0              [list] f=6000   n=3   2,3,4
[list] f=8000   n=0              [list] f=8000   n=3   2,3,4
[list] f=10000  n=0              [list] f=10000  n=3   2,3,4
```

⛔ **BUT CHECK WHAT THE CONTROL ACTUALLY SAT ON.** Inspecting the idle run's own
screenshot (`docs/evidence/dbz-idle-control.png`) shows:

> **"Resetting backup memory. Press the A Button to begin."**

**The idle run never left the pre-game boot screen.** With no A press, the game sits
there for all 12,000 frames. So the experiment did **not** test "does the list
populate during play without input" — it tested "is the list populated before the game
starts". Those are different questions.

### What the control therefore proves (weaker, but still real)

1. **The list is empty in the pre-game state** — at the backup-memory reset screen,
   for 12,000 frames. So it is **not** a constant baked into the ROM, and **not**
   populated at cold boot.
2. **The list is populated only once the game is actually running** — the driven run
   pressed A, passed this screen, and the list appeared.
3. **Both runs read `n=0` at f=0**, consistent with (1).

⛔ **Retired:** the "startup default" reading, and the stronger "created by play"
phrasing. The evidence supports **"empty before the game starts; populated after"** —
it does **not** yet distinguish "populated by the act of starting" from "populated by
later progression", because the control never got past the first screen.

⛔ **A control that stalls at the first input gate answers a different question than
the one you asked.** Check the control's own screenshot before drawing conclusions
from it — the same rule that applies to the experiment applies to its baseline.

### What remains genuinely open

The list contains **Piccolo, Krillin, Tien** while the player has Goku alone. The
survivors are all game-progress shapes that this early state could produce:

- a **story/event roster** — characters staged for the next scripted scene;
- a **next-battle roster** pre-filled by the episode; or
- characters the intro dialogue has "registered" so far.

⛔ **Deciding needs a state this plan cannot reach** — an actual battle, a recruit, or
a party-menu edit. Until then the field stays **unresolved and must not be narrated
from.**

### The boot sequence finding (a side benefit)

The control screenshot is the first direct look at this ROM's boot path, and it
records a gate the input plans must clear:

```
Resetting backup memory.
Press the A Button to begin.
```

⛔ **Any scripted run must press A at this screen or it never starts.** Worth having
explicitly in the plan rather than relying on an early A press to land by luck.

### ⛔ The method lesson, sharpened

**A control run with the variable removed is the cheapest experiment — but verify the
control reached the state you assume.** Here it was free (no plan, no new code), and it
genuinely killed the "constant in the ROM" hypothesis. It did *not* license the
stronger claim, and only looking at the control's screenshot revealed that.

**Ask "what is the control?" before measuring, and "where did the control get to?"
before concluding.**


## ❌ Superseded claim (kept for the record)

The text below was written before the screenshot check and asserted this array was
the active party. It is retained only so the reasoning that produced a wrong answer
stays visible. **Do not act on it.**


```
[+0] 0x020CC774 -> 0x020CDD44   rec2 + 0x158   = Piccolo
[+1] 0x020CC778 -> 0x020CDF90   rec3 + 0x158   = Krillin
[+2] 0x020CC77C -> 0x020CE1DC   rec4 + 0x158   = Tien
[+3] 0x020CC780 -> 0x00000000   <- NULL terminator
```

**This is the structure the whole search was for.** Everything about it is checkable:

- It is a **NULL-terminated pointer array** — the canonical party-list shape, and
  exactly the hypothesis the earlier pointer hunt looked for. That hunt searched for
  pointers to the record *base* or to the *name* field and found none; the pointers
  actually aim at **`rec + 0x158`**, which is why it was missed.
- **The code references `0x020CC774` 75 times**, every reference 4-aligned (ARM
  literal-pool shape). No coincidence produces that.
- Its contents are a **different set from the static `+0x180` chain** (there:
  `[2,3,4,5,6,7,8,0]` = all 8 characters in order). This list holds **only 3** and
  is terminated — the distinction between "every character in the game" and "the
  characters currently in use" that the earlier roster analysis predicted.
- The list is bracketed by zeros both before `[+0]` and after the terminator, and
  the next bytes (`0x020CC794 = 0x00030003`) look like a separate flag word — so the
  array is a real, bounded field, not a coincidental run.

### ⛔ What is NOT yet confirmed

- **Which character is actively fielded.** `0x020CC774` is the party array, but the
  pointer targets and their scalar fields need an in-game change to interpret.
  Reading `+0x1F8`/`+0x208` *relative to the pointer* lands outside the member's
  data (the pointer aims 0x158 into a `0x24C` record), so the member's own stat
  fields sit at known offsets **from the record base**, not from the pointer.
- **Whether this is the active battle party or the "selected for the next fight"
  list.** Both are plausible for a 3-entry terminated array. Changing party members
  in-game and re-reading settles it — the next experiment, and now a cheap one.

⛔ **Do not narrate from this list yet.** It is located and structurally sound;
its semantics are one experiment away, not zero.

## ✅ The method that found it: reference density, not value plausibility

Both earlier RAM searches failed because they guessed at **values** — "a plausible
integer" (348 hits of graphics noise) and "a pointer to a record base" (0 hits).
Neither asked what the *code* actually uses.

An ARM program names its globals a different way: the address sits in a **literal
pool** and is loaded with `LDR Rn, [pc, #imm]`. Those literals are **4-byte aligned
and live in the code region**. Counting how many times each data address appears as
an aligned u32 *inside code* measures how central it is.

`scripts/oga-dbz-globals.py` does exactly this, and it returned 17 globals at ≥12
references:

```
0x020D2124  x256  24/64 non-zero   first4=0x020D2128
0x020CCA20  x126  ZERO-FILLED
0x020CE8C0  x87   ZERO-FILLED
0x020CC774  x75   14/64 non-zero   first4=0x020CDD44   <- THE PARTY ARRAY
0x020CC780  x73   2/64 non-zero
0x020CC770  x70   14/64 non-zero   first4=0x00000000   (sibling of the party array)
0x0227A000  x46   64/64 non-zero   first4=0x99999999   (graphics fill)
0x02360000  x30   52/64 non-zero   first4=0x50414D64   ("dMAP" — a map/asset header)
...
```

⛔ **The generalisable rule: stop asking "which value looks like HP"; ask "which
address does the code actually use".** Reference density is evidence; plausibility
is not. This is the same lesson as the string scan, applied to code instead of data.

⛔ **`0x020CC770` is 4 bytes before the party array and referenced 70 times** —
almost certainly its header or count. Worth reading alongside it.

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
