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

## ✅ 2026-10-25 — whole-party readout shipped (host-proven)

`WhereAmI` now speaks the whole party in one press ("Party of 3." + each
member's HP/KI/NEXT in slot order) instead of the one-member cursor walk.
Host suite 19 checks (was 17). Live proof needs a multi-member save.

## ⛔→✅ 2026-10-25 — CORRECTION: HP/KI stats are u32 triples, not u16

The MEMBER screen (menu → Status page) of a live 3-member party gave
ground truth the earlier single-member check could not: Krillin HP
300/300 KI 105/105, Tien 320/320 / 110/110, Yamcha 305/305 / 100/100 —
and the party pointers in that same dump name exactly records 3, 4, 5
in that order. The record layout around the stats is:

```
+0x0A0  u32 HP current     +0x0A4  u32 HP max      +0x0A8  u32 HP copy
+0x0B0  u32 KI current     +0x0B4  u32 KI max      +0x0B8  u32 KI copy
+0x240  u32 NEXT (exp to next level)
```

The adapter had been reading max as a **u16 at +0x02** — the high half
of the u32 current, i.e. **0 on all real data** (HP values < 65536). The
host tests passed because their fixtures encoded the same wrong layout.
Fixed in the adapter (u32 reads at +0/+4), fixtures rewritten to the
true layout, and a regression check added (damaged member must speak
"HP 250 of 300"). Suite: 20 checks. **Lesson: a fixture copied from the
adapter's own assumption proves nothing — ground truth must come from
the game's screen.**

## ✅ 2026-10-25 — the menu, observed live (dial index NOT isolated)

A driven bench run (START at f=100500, dumps at Items and after 3 DOWN
rotations, screenshots every 2000 frames) established the menu's model:

- START opens the menu. **Bottom screen = the radial dial** (selection
  name + description line + Zeni and play-time counters top-right);
  **top screen = the selected entry's page**, updated as the dial
  rotates. At open the dial sits on **Items**; 3 DOWNs land on
  **Status**, whose page is the member list.
- Play time advanced 0:27 → 0:30 across ~9800 menu frames — it does
  **not** tick per frame in the menu (a timer confound for diffs).
- The Zeni candidate `0x020CC770` read **0 in both states** — still
  unverified; only a nonzero-Zeni state (post-battle/shop) can settle it.
- The selection index did **not** isolate: a full u32/u16/u8 diff of
  the two states found no clean 0→3 (or 1→4) word — the only +3
  movers were script-engine counters at `0x020D8E44/54/58/60/A0`
  (cross-checked against cutscene dumps, where they climb without any
  menu — refuted). The pair also crossed a Status-page data load
  (`0x020DA0xx` block), which polluted the diff. Next attempt: dumps
  one rotation apart with the page held as constant as possible, or
  an on-frame watch over the candidate regions while rotating.

## ✅ 2026-10-25 — the script system is readable: dialogue format CRACKED

Full-RAM dumps mid-scene (bench probe, melonDS; dumps in `~/oga-dbz-bench/`)
found the event-script system in plain ASCII:

- **Chapter scripts load at a fixed work area around `0x02283E00`** (this
  chapter). Before the chapter loads, the same area holds the script
  engine's command vocabulary (`SetBtlMember`, `Create`, `SetHitCircle`,
  `Begin`, `SE`, `PauseAction`, `GetRot`, `SetFlag`, `Face`, `PcCloth`, …).
- **Box grammar** (verified against screenshots): `<p>` starts a box's
  text, `<n>` is a newline inside the box, `<k>` ends the box, and a bare
  NUL-terminated actor token between boxes (`kuririn`, `lunch01`,
  `tenshin01`, `yamcha01`, `Mobj150`, …) directs the following box's
  speaker/staging. Example, byte-for-byte from RAM, matching the box on
  screen at that frame:
  `kuririn\x00<p>...!!!<n>Oh! Hey, Launch!<n>Good morning!<k>`
  (screen showed lines 1–2 typed, Krillin name plate up).
- A **second, processed copy** of the text feeds through a ring/stream
  buffer near `0x020D6000` (boxes separated by `\x04`/control bytes,
  markup stripped) — the engine's text feed, and a second candidate
  source if the raw script proves awkward.
- Narration boxes are in the same stream (the Shenron recap text was
  found verbatim — "He revived all of his friends who had lost…" — in
  the processed copy after the scene ended).
- **Chapter title cards are NOT in RAM as ASCII** ("The Moment of
  Departure! Toward Ever Greater Heights..." was on screen at dump time
  and absent from the dump) — rendered from a graphics/font resource;
  a title-card reader needs a different source (likely a string ID
  table) or stays silent.

⛔ **Still missing for dialogue speech: the current-box witness.** A
two-dump differential across two boxes of the same scene found **no**
RAM word pointing into the script region that advances with the box,
and no clean small-offset counter. Candidates for the next hunt: the
processed stream's read position near `0x020D6000`, and offsets relative
to a script base held outside the dumped range. **Do not ship a dialogue
reader without the witness** — speaking from the script resource alone
would narrate boxes that are not on screen.

## ⚠️ 2026-10-25 — bench note: local-copy drift + the localize trap

The local working copy's Core plumbing (`oga_core`, `mesen_core`,
`snes_core`, `pokecore`) had drifted from main; restoring main's content
(verified hash-identical to 54efd48) made all 674 host TUs compile.
Separately: `scripts/mesen-localize-twins.sh` localizes symbols shared
between Mesen objects and anything else defining them — including RTTI
that `snes_core.o` emits from Mesen headers — and the localized objects
then fail probe links with hundreds of "defined in discarded section"
errors. Bench workaround used for the DBZ probe: a bench-local,
unlocalized Mesen object set linked with `--allow-multiple-definition`
(the genuine twins are identical vendored copies). The shipped adapter
and its host tests never needed the probe link.

## ✅ 2026-10-25 (round 2) — dialogue witness FOUND in existing dumps

Re-analysis of round 1's five dialogue dumps, with screenshot ground
truth (f8000 = Yamcha "Snoorrrre...", f10000 = Krillin "...!!!/Oh!..."
mid-type, f12000 = Krillin "!!!", f6000 = title card, f20500/f24500 =
gameplay), found the message-system context at `0x020D5E00`:

- `+0x10` **P1** (`0x020D5E10`): pointer into the processed-text buffer.
- `+0x14` **P2** (`0x020D5E14`): pointer into a second copy of the text
  near `0x020D6100` (role TBD).
- `+0x40` **SPK** (`0x020D5E40`): pointer to the current speaker's
  display-name string **inside the script** (`0x02283DD3` = "Krillin"
  in both Krillin-box dumps; 0 on title/gameplay — but STALE in the
  menu dump, so it is not an open-signal by itself).
- `+0x08` low u16 (`0x020D5E08`): **1 in both box-open dumps, 0 in
  title-card, gameplay, and menu dumps** — the dialogue-active flag
  candidate.

The processed-text buffer starts at `0x020D5E44` (`\f` + text). In
every box-open dump, the text from `0x020D5E45` to **P1** is EXACTLY
the box on screen ("Ahh... ahhh... ahhhh-choooo!!" at f10500, "!!!" at
f12000), complete even though the screen typewriter was still running
in the f10000 shot — so the full processed box lands in the buffer at
box open. In the title-card dump the same span holds all six narration
pages (narration writes its block whole; whether P1 tracks narration
page-by-page is the one open question). Round 1's two-dump diff missed
this because it looked for a pointer INTO THE SCRIPT; the witness
points into the message buffer instead. Still to prove before wiring:
micro-states around one advance (P1 behaviour during typewriter),
the flag during a narration box, SPK for other speakers (Launch /
Tien / Yamcha boxes), and P1 stability between boxes.

## ✅ 2026-10-25 (round 2, later) — witness PROVEN, reader WIRED + live-proven

All open questions closed with four fresh probe runs (dumps in `~/oga-dbz-bench/w-*`):

- **Flag**: 1 in every box/page state (13+ states: house scene, narration mid + end),
  0 in the between-boxes state, title card, gameplay, menu. It is the gate.
- **P1 tracks narration page by page** (grows as pages append; the current page is the
  LAST `\f`-segment before P1 — mid-state ended exactly after page 3 with page 3 on
  screen, screenshot-confirmed). P1 max observed 0x020D6015; reader caps at 0x020D6100.
- **SPK** per speaker: Tien 0x02283DA6, Yamcha 0x02283DB9, Krillin 0x02283DD3 — each the
  display-name string in the scene's script. Narration SPK (0x022837FA) lands on a
  binary record → empty name → bare line. Processed `\n` can be display wrapping, so
  script agreement compares whitespace-stripped text.

**The reader** (`dbz_on_frame` → `DbzDialogueFrame`): flag gate → P1 range check →
last-segment extraction → 2-frame debounce → **script agreement** (the segment must
reconstruct from a `<p>…<k>` box in the SPK-anchored script window; `%d` = digit
wildcard; no agreement = silent, latched for that box's whole display) → speak as
`"Name: text"` (or bare) at High priority, newlines flattened. The spoken latch clears
only after 3 idle frames, so identical consecutive boxes both speak while a 1-frame
flicker cannot double-speak. 11 host checks (DBZ suite 31). **Live proof**: the real
dumps replayed through the real adapter (`~/oga-dbz-bench/replay.cpp`) spoke exactly:
"Tien: ...Zzz." / "Yamcha: Snoorrrre..." / (between-boxes: silent) / "Krillin: ...!!!
Oh! Hey, Launch! Good morning!" / "Krillin: Ahh... ahhh... ahhhh-choooo!!" / narration
pages bare — all matching screenshots. Chapter title cards stay silent BY DESIGN
(graphics, not text — the flag never rises for them).

## 🔶 2026-10-25 (round 2) — main-menu dial: VM theory FALSIFIED, countdown-byte candidate

Six menu states with screenshot-known selections now exist (bench dumps):
Items ×2 (fresh open + close/reopen), Skills, Equip, Status, Capsules (UP-wrap).

- **Falsified:** the script-VM frame at 0x020D8E40–0x020D8EA0 (counter 0x020D8E54 =
  [1,2,3,4] over four DOWN rotations; record pointers 0x020D8E40/50 stepping +0x48
  through the page array at 0x020DA008). It looked perfect — until the UP-wrap state
  read the SAME values as one DOWN (2 / record 1) while the screen showed Capsules,
  and the close+reopen state read (5 / record 3) while the screen showed Items.
  These are script-execution traces, not selection state. (The same registers take
  unrelated values 1–6 during dialogue — generic VM state.)
- **Candidate (unique survivor of a strict 4 MB scan):** bytes at **0x020D212A/2B**
  (mirrored pair) hold **(5 − dial index) mod 8**: Items 5, Skills 4, Equip 3,
  Status 2, Capsules 6 — history-independent (fresh open and reopen both read 5),
  distinct per entry, equal −1 steps. In menu states the word at 0x020D2128 is
  0x0v0v0202; outside the menu it reads entirely differently (0x00000203,
  0x01110000, …), giving a candidate menu-open gate: u16@0x020D2128 == 0x0202 AND
  the mirrored bytes equal AND ≤ 7. Dial order (string table 0x0220EBAF + screens):
  Items, Skills, Equip, Status, Crystal Ball, Options, Switch, Capsules —
  predicted bytes: Crystal Ball 1, Options 0, Switch 7. Confirmation run
  (positions 4 + 6) in progress; the watcher ships only if both land.
- Menu text (names + descriptions) is plain ASCII in RAM at ~0x0220EB80–0x0220F200,
  including the save-flow strings ("Save your progress?", "Quicksaving...",
  "Save Complete") — useful for the save-flow item later.

## ✅ 2026-10-25 (round 2) — dial index PROVEN; menu watcher wired + live-proven

The countdown candidate landed exactly: pos4 (Options on screen) read v=1, pos5's
filmstrip frame showed Switch, pos6 (Capsules) read v=6 — so the dial has SEVEN
entries and v = (5 − index) mod 7:
Items 5 · Skills 4 · Equip 3 · Status 2 · Options 1 · Switch 0 · Capsules 6.
("Crystal Ball" is not on this dial; its string belongs to a sub-screen.)

**Watcher** (`DbzMenuFrame`, polled from `dbz_on_frame`): gate = u16@0x020D2128
== 0x0202 AND u8@+2 == u8@+3 AND v ≤ 6; the index must be stable 2 frames;
opening speaks "Menu. <Entry>." (High), rotations speak the entry name.
9 host checks (DBZ suite 40). **Live proof** (real dumps through the real
adapter): "Menu. Items." → "Skills" → "Equip" → "Status" → "Capsules"
(the UP-wrap state) → "Options" → "Capsules" — every state correct.
Limits: entering a sub-screen (A) drops the gate and stays silent — sub-menu
contents are audit item 11, still open; closing the menu is silent by design.

## ⛔ 2026-10-25 (round 2) — battle reach: blocked on the Kame House exit

Five A-mash/navigation bench runs (plans + filmstrips + dumps in
`~/oga-dbz-bench/battle*`) clear the opening scenes deterministically and roam
both floors of Kame House, but no input pattern found the exit to the island —
so no battle state, no nonzero Zeni, and no save point could be banked this
round. Audit doc has the full log and the two next routes (player entity-record
attribution at 0x020D45xx +0xA4/+0xA8 (x,y) pairs; map exit-trigger data).
Zeni candidate 0x020CC770 read 0 in all 20+ banked states — still unproven,
neither confirmed nor refuted.

### Round 3 (2026-10-25/26) — attribution REFUTED for the 0x020D45xx records; descent is a warp

The attribution experiment ran (probe gained `DBZ_DUMPS` multi-dump and a
`DBZ_ENT` 30-frame sampler; bench binary `~/oga-dbz-bench/dbz_probe3`; audit
"Round 3 log" has the full account). Verdicts:

- **The 0x020D4500/4600/4700 records are not the player.** Two runs ending
  with the player visibly in different places (south door mat vs staircase)
  hold identical (x,y) at +0xA4/+0xA8 in all three records. 0x020D4700's
  +0xA8 is not a coordinate (41836 in every ground-floor dump).
- **The bedroom→ground-floor "descent" is a stairwell WARP**: positions snap
  between f64170 and f64200 during a RIGHT hold; the DOWN legs in all
  round-2 plans were inert, and the ground-floor record values are final
  from the warp onward — which is why round 2's later legs looked dead
  against these records.
- Four clean 150-frame legs on the ground floor changed no position-like
  field anywhere in work RAM, yet a filmstrip run with 1200-frame legs
  (plan-r3-film.txt) shows the player crossing the room (staircase base
  at f070000 → kitchen at f072000): movement works, the watched records
  were simply never his. A dedicated narrowing hunt (dumps every 90f
  across continuous LEFT/DOWN holds) then excluded ALL of MainRAM:
  no u8/u16/u32 scalar, either alignment, fixed-point included, tracks
  the walk. The position lives in SharedWRAM, ARM7 WRAM, or DTCM — the
  probe now dumps those banks beside every main dump (`DBZ_DUMP_ALT=1`)
  and the hunt is re-running against them.
- ⛔ Dump files carry a 48-byte text header line before the binary RAM;
  any offline reader must skip it (replay does; ad-hoc scripts that
  don't read everything shifted by 48 bytes).
- Round 3's exit attempts (screenshot-steered, plans r3-exit1/2/3 +
  r3-ab) still did not leave the house. What they mapped: the ground
  floor's stair landing is a pocket — LEFT/DOWN holds of any length
  from it are inert, RIGHT steps NE along it, and UP/DOWN/RIGHT in
  sequence (400f each) reproducibly reaches the coffee table; every
  tried continuation funnels back to the landing, and the south door
  mat has never been occupied on film. The living room's NORTH DOOR
  has never actually been reached (round 2's attempts used the broken
  movement model) and is the next target, with Roshi's whereabouts
  (absent from both mapped rooms) the related question. The script
  table is plain ASCII in the ROM (~0x4812000): table c0002_00 holds
  Roshi's briefing (island south of here; crystal ball = quest
  finder), Turtle's island lines, and Launch's summons — the prologue
  is: be summoned → see Roshi → leave south. NPCs wander on schedules
  (the orange-gi figure in the kitchen is Krillin, not the player —
  check identities on zoomed crops, not thumbnails).

### Active Guard design (settled 2026-10-25, before any battle was reached)

Per the user's direction, Active Guard gets NO speech at the timing
moment: it gets the Budokai Access meter-tone shape (accessibility-tricks
catalog §1.2) — a short pitch ramp driven by the game's own timing value,
peak = the guard moment, a soft beat marking the window's restart, at
most one spoken setup line, and spoken confirmation afterwards of whether
the guard counted (§1.1). Full design note in the completion audit
(~/oga-dbz-bench/completion-audit.md). Wiring is gated on (a) reaching a
battle and (b) measuring the timing value the ramp will follow.

## ⛔ 2026-10-26 (round 4) — house geography corrected; (A,B) killed; reload divergence

Full log in the completion audit ("Round 4 log"). The short version,
because rounds 2–3 built on two of these errors:

- **Measurement discipline:** eyeball sprite coordinates are not
  evidence. Round 4 used a template matcher (the player's front
  sprite as a patch, exact-search, error 0.0) + frame hashes. By that
  ruler the ground floor is ONE fixed-camera screen; the
  post-briefing spawn and the stair-warp arrival are the same tile —
  the rug's west edge, native (75,120), facing S. The "landing
  pocket", "railed landing", "coffee-table route" and "two camera
  framings" of rounds 2–3 were misread scene-era frames. Roshi IS in
  the house: he stands on the bedroom landing with his staff (the
  figure earlier filed as Turtle). The front mat is at the room's
  south vertex ~(107,160).
- **(A,B) @0x020D284A / @0x020D5DEC is NOT the player position —
  killed in writing.** Spawn (75,120) reads (116,64); a second
  verified player tile reads (117,63) (Δ(+1,−1) for a clear screen
  displacement; the two states' full-RAM diff is only 92 bytes). In
  the bedroom the pair oscillates with no input — it tracks a pacing
  NPC (Roshi's patrol), i.e. a per-map actor/scene record, not the
  player. Round 3's "only movement-responsive track" was warp-transit
  coincidence. The player position is STILL unfound in RAM.
- **Movement (living room, matcher-verified):** RIGHT = S to the
  rug's south lip (75,134); DOWN = N (exact reverse); LEFT = E, fast,
  to (123,120); UP is position-dependent and is the only lip crossing
  found (from (123,120) it slides S to (123,143)). A sustained DOWN
  hold at spawn HANGS the game (frames byte-identical, RAM static,
  3,600f, twice) — taps are fine. The south floor is reachable
  in-run (LEFT 400 → UP 1200 → RIGHT 600 ends at (75,147); the mat
  tile itself was crossed at (120,162) with no map change — the exit
  trigger is not the mat's top surface).
- **⛔ Savestate branching is NOT trustworthy for position.** States
  saved on the south floor reload with the player relocated
  (kitchen/fridge behaviour: A-taps cycle the fridge examine box,
  "A large refrigerator filled with a variety of food"). The probe's
  whole-core state evidently misses (or the game re-derives) the
  field position on load. Drive long itineraries inside ONE run from
  the root state (`~/oga-dbz-bench/r4-base.st`, reload-verified)
  until the real position record is found.
- **ROM:** NitroFS parsed (7,010 files). kam01 (island) ↔ kam02
  (ground floor) ↔ kam03 (bedroom) from field scripts; kam02 lists
  kam01 as a neighbour (the front door's target). Script format
  `SB `, bytecode at 0x40, builtins include MapJump / SetFlag /
  GetFlag / SetScenarioCnt / SetPos / GetPos / WalkAuto / EventCall.
  c0002_01 is the scripted tutorial battle (SetEnemyParty, BTL_TUT00)
  — story-gated, not an encounter. `.map` bodies (dMAP) appear
  compressed; not decoded. No briefing-flag gate on the exit was
  found; the base state is already post-briefing.
- Host suite unchanged and green: **40 passed, 0 failed** (no adapter
  change this round — nothing met the shipping bar).

## ✅ 2026-10-26 (round 5) — position record FOUND; round 4's kills overturned

Full log in the completion audit ("Round 5 log"). Round 4's three
headline claims were themselves overturned, by a stronger method:
**differential RAM between deterministic runs** (five 4,500f runs
from `r4-base.st` differing in exactly one held key; NPC/animation
state cancels in the diff) plus pose-level sprite crops.

- **The player position record is `t @0x020D2848, s @0x020D284A`**
  (u16s), with mirrors 140−t @0x020D5DE8 and 180−s @0x020D5DEC
  (A+B = 180 exactly in kam02 — round 4's "killed" pair is this
  record's s half and its mirror). Facing @0x020D2766
  (0 down/spawn, 1 UP, 2 LEFT, 3 RIGHT; 8–11 moving variants).
  The record is the map's actor-0 slot: in kam03's scripted state
  it shows a wandering actor and is not key-driven — the source of
  rounds 2–4's "it tracks a patrol" confusion.
- **The player is Krillin** (bald, orange gi; pose-verified turning
  under a held key). Spawn is **tile (92,116), the stair base**
  (screen ~(200,85)). The green-top figure at the rug is a
  stationary NPC (Launch); the camera is a perspective view that
  pans with parallax (near +46px, far +5px), so round 4's
  fixed-camera template positions — spawn (75,120), the mat
  crossing, the movement map — are void.
- **Movement (tile space, from the record):** LEFT = +t (wall
  140 on the s=116 row, ~0.46 u/f); RIGHT = (−t, +s) diagonal
  (from spawn the −t side is already walled, so it reads as pure
  +s to the stair-railing stop s=130/131); UP = +s (at t=92 the
  staircase: warp to kam03 fires at s≈156; at t=140 s+ walls at
  139); DOWN = −s (walled at s=116 on the ground row). Diagonals
  combine with terrain sliding: UP+LEFT bypasses the warp tile
  to a landing plateau at (119,176).
- **Reload relocation explained:** a savestate reload re-derives
  the actor position from the map entry point (kam02 entry =
  (92,116)); the saved field position does not win. Navigation
  must run unbranched, in tile coordinates, off the live record.
- **Exit still open:** the s<116 band is walled at every probed
  column (t = 92, 109, 113, 140); the front door's tile-space
  location is unknown (screen-south ≠ −s). Round-6 leads: DOWN
  from (92,143); the red door from the landing plateau; the
  kam02 `.sb` MapJump1/MapJump2 functions (string refs are table
  indices); the `.map` packed body. Zeni candidate 0x020CC770
  still reads 0 in all states.
- Host suite unchanged and green: **40 passed, 0 failed** (no
  adapter change — the record alone does not meet the shipping
  bar until the exit and battle detection land).

## 🔶 2026-10-26 (round 6) — the "exit problem" was a pending scene; door still shut

Full log in the completion audit ("Round 6 log"). Round 5's two
exit leads closed negatively, then the blocker reframed itself:

- **DOWN from (92,143)** (reached via the plateau slide):
  walks down the stair slope to (92,116) and stops — the s<116
  band does not open from the under-stairs column.
- **The plateau's north edge is the bathroom**: an UP hold from
  (119,176) settles at (118,177) facing UP, and A opens the
  toilet examine (*"A toilet sits here. You're good for now,
  thankfully."*). The "red door" of round 4's screen geography
  is this fixture beside the stair head. No door at the plateau.
- **The hidden scene**: a routine A-press sweep along the
  ground row triggered scripted movement — at ≈(120,116)
  facing DOWN the game turns the player UP, auto-walks him via
  (156,123) to (134,143), walks the NPCs in, and plays the
  **c0002_00 cast-gathering / briefing scene** (Roshi gives the
  crystal-ball mission and grants items to every inventory).
  Controls proved it is positional/interact-triggered, not
  timed: idle 2,400f does nothing; a verbatim replay of the
  sweep plan reproduces it exactly. So `r4-base.st` was never
  post-briefing — the summons scene had played, the gathering
  had not. Played out in run `r6-scene` (A every 300f to
  f13000); end state `r6-scene-end.st`, player at (112,116),
  control free.
- **Post-scene exit test NEGATIVE** (run `r6-exit`): at the
  t=140 column, two 900f DOWN holds + an A press — s never went
  below 116, no box opened. The door is not a story-gated
  south wall at the east end of the ground row. Remaining
  search space: the other s=116 columns post-scene and the
  s>116 interior (a (t,s) lawnmower, run `r6-lawn1`, is in
  progress). The room is isometric; the entrance mat visible
  at the floor's south tip has no known (t,s) yet.
- **`.sb` script format cracked** (destinations proven): six
  u32 section pointers in the header at +0x08; bytecode string
  operands are string-pool indices (the `26 XX` operand).
  kam02 **MapJump1 → `kam01_00`** (the front door), **MapJump2
  → `kam03_00`** (the stairs, matches the behavioural warp);
  kam03 MapJump1 → `kam02_00`. The stubs carry no readable
  trigger tile; trigger registration lives in the `.map` event
  data or the engine's object table. The `.map` body (dMAP,
  w=38 h=32, size u32 at +8) is still undecoded — high entropy
  throughout, no LZ77 stream at any offset. A RAM scan of
  0x020C0000–0x020E0000 at spawn found no trigger objects
  stored as plain u16 (t,s) pairs.
- **Scene-completion flag**: unproven candidates only (clean
  0→1 flips in quiet regions of the pre/post-scene window
  diff): 0x020C91C6, 0x020D2771, 0x020D42C4, 0x020D43B0. The
  diff is dominated by the item grant (table at 0x020DA158+),
  so confirmation needs two lineages differing only in the
  scene.
- **Lawnmower 1 (run `r6-lawn1`, post-scene, 8,700f)**: no
  exit warp anywhere; dlg/Zeni stayed 0. New geography: the
  t=140 column climbs to **s=141** (not 139); a diagonal
  channel runs from (140,141) via (113,158) to the
  under-stairs column (92,143); a gathered NPC physically
  blocks the ground row at ≈(97,116). **Warp anomaly**: an
  UP hold slid into the stair column and climbed
  (92,151)→(92,165) with **no kam03 warp** — through the
  s≈156 point round 5 banked. **Lawnmower 2 resolved it**:
  a full-flight climb from the bottom step warps at
  **t=92, s≈155** (actor-table signature flips to kam03's
  set; the position record becomes kam03's wandering
  actor-0), so the warp is **entry-path-dependent** — the
  slide entry crosses the band silently. Same warning
  applies to door-trigger probes. Lawnmower 2 also found
  that **diagonal inputs jam at exact wall contact**
  (RIGHT from (140,116) never moved; from (139,116) it ran
  the whole row) and that a plain UP hold can jam after a
  jammed diagonal — navigation recipes need a step-off
  first, and single-hold negatives from wall contact are
  suspect. Unexplored: mid-room north channels (the scene's
  gathering spot (134,143) implies one), the s≈145–176 band
  away from the stairs, and any space east of t=140 at
  s>116 (the scene's scripted path transiently showed
  (156,123)).
- **The kam02 US string pool is fully read** (175 strings;
  bench tool `r6-pool.py`): the map's complete examine set
  is three — toilet [167], **fridge** [168] (*"A large
  refrigerator filled with a variety of food."*, kitchen
  wall, not yet triggered live), kitchen note [169].
  **There is no door examine text: the front door is a
  step-on warp, not an A object.** Post-scene NPC talks are
  in the pool too (Roshi: *"You need to get moving before
  nightfall. It's the island just south of here."*; Turtle:
  *"Are you going to the southern island?"*).
- **The actor table is mapped** (lawns 8–9): position
  pairs (u16 t, u16 s) at **0x020D275C + k×0xEC**; the
  player is k=1. Post-scene kam02 cast: **k=0 is Roshi** —
  he wanders (stops include (128,104), inside the entrance
  passage south of the floor edge, and (128,150)); k=4
  (118,143) a student; k=5 (97,124) Launch at the rug;
  k=2/k=3 at t=150, east of the kitchen wall. **The front
  doorway is at t=128**: facing DOWN at (128,116) with A
  talks to Roshi when he stands in the passage at
  (128,104) — talk range reaches ≥12 units in the faced
  direction. The passage runs south to at least s=99. A
  DOWN hold there was body-blocked by Roshi in lawn 8;
  lawnmower 10 tests the doorway across his wander cycle.
- **★ THE EXIT IS SOLVED (runs `r6-lawn10`/`r6-lawn11`)**:
  at **(129,116)** with Roshi out of the passage, a DOWN
  hold fired the exit event (warp-tell facing 4, an
  off-band relocation arc) — it is the **scripted kam01
  transition**: Turtle intercepts Krillin on the beach and
  explains the **CC Point** (the red C mark on the sand —
  the game's save/heal point: *"This is what's known as a
  'CC Point'. It's a facility put up by the Capsule
  Corporation. Try taking a look at it, okay?"*). Free
  roam on kam01 begins at **(106,163)** by the house door.
  The door was never a tile: it is a story event at the
  (128,116) doorway, gated by the briefing scene and
  body-blocked by Roshi — three stacked causes behind six
  rounds of failures.
- **kam01 + CC Point + save flow (lawns 12–17)**: outdoor
  axes flip vs the interior (+t = screen-west, +s =
  north; read the record as s16 — walking off the island
  underflows t). **The CC Point's active tile is
  (178,163)**, on the north-shore line west of the house;
  stepping there + A opens the CC menu on the bottom
  screen. **Save flow banked end-to-end** (screenshots):
  Save → file select (FILE 1: party, all Lv 1, "Kame
  House", 0:26) → "Overwrite this file?" → "Saving. Don't
  turn off the power." → **"Save Complete"**. FILE 1 now
  holds the post-briefing state.
- **World map (lawns 18–19)**: heading south opens the
  world map (viewport frame + a **+ destination cursor**;
  the position record becomes the cursor, facing reads
  0xFFFF). Travel semantics, the mission island's marker,
  the cave, the first battle (c0002_01), Zeni
  confirmation and the Active Guard capture are round
  7's queue — they live past the map and the cave. No
  adapter change in round 6; host suite 40/40 green.
- **Lawnmower 3 (run `r6-lawn3`)**: the east wall is a wall
  at every probed s (LEFT from (140,141) slides down it to a
  stop at (140,126)) — no east wing. At **(140,126) facing
  LEFT, A opens the kitchen note**: *"There's a note on the
  corner: 'Keep the kitchen clean --Turtle'"* — the east
  wall is the kitchen counter, and face+A examining is now
  proven on two objects (toilet, note). The box ate four
  legs (dialogue discards movement), so the mid-room climbs
  slipped again; also, UP from the railing stop (92,132)
  jams at s=133 — the stair flight must be entered from its
  bottom step. Door-hunt corollary: a trigger may require
  arrival from a specific direction — the s=116 row has
  only ever been crossed horizontally, never arrived at
  moving DOWN from the room's interior; south-row A presses
  facing DOWN are still sparse post-scene. Lawnmower 4 tests
  both.
- No adapter change; host suite still **40 passed, 0 failed**
  (re-verified this round). Zeni candidate 0x020CC770 still
  0 everywhere.

## 🔶 2026-10-26 (round 7) — Zeni proven from code; world-map travel bench-blocked on touch

Full detail: `~/oga-dbz-bench/completion-audit.md` "Round 7 log".
Guide ground truth (Darkstar Ripclaw FAQ) filed at
`~/oga-dbz-bench/guide-notes.md`.

- **Zeni address PROVEN (static): u32 @0x020CC770.** Ghidra track
  (ARM9 @0x02000000, project `~/oga-dbz-bench/ghidra/proj`): the money
  wrapper `FUN_020469C4` adds its argument to `*0x020CC770`, clamped
  [0, 9,999,999]. Live payout proof still pending (no battle reached).
- **Clean post-tutorial lineage built** (round-6 states carry a stuck
  script — Krillin's sprite frozen, record patrolling, map inert).
  Recipe: from `r6-lawn11-end.st`, walk to the CC Point's east side,
  step adjacent facing it → Turtle's CC scene (portrait dialogue,
  "R Ffwd" prompt — hold R to fast-forward, A closes pages) → free
  control → wade WEST into the water → overworld map opens (cursor
  starts at the current location's marker; (180,96) in this lineage).
- **World map:** position record = cursor (facing 0xFFFF); glide with
  ~90f ramp to 2 u/f + inertia, clamps t∈[180,436], s∈[24,216];
  map px = (563 − t, 311 − s); bottom screen = 1:1 zoom on the cursor.
  Training Island's click point is marked by a ◎ reticle in the zoom
  (renders only once the tutorial chain is complete): map (206.5,
  290.5) → cursor ≈ (356, 20).
- **Travel NOT solved — "cursor + A" (per the guide) is disproven on
  this bench:** A at the click point (±3 units, incl. glide-crossing
  spam at 3-frame resolution), at home, and a full button battery are
  all inert in the clean lineage; probe taps (pixel + ADC-scaled, incl.
  a 3×3 grid) have never produced an effect in ANY UI — `poke_touch`
  delivery is unproven and is the prime suspect (the engine has a
  SetClickPoint native). Next attempt starts from `r7-go5-end.st`
  (cursor resting on the click point): fix/verify touch delivery, or
  test in a real frontend.
- **Menu corrections:** the dial rotates with DOWN/UP (not
  LEFT/RIGHT); "Switch" (v=5) is the party-formation screen.
- **Battle work (gaps 8–9) not started — gated on travel.** Ghidra
  assets for it: 78 'cf' script natives mapped (`ghidra/natives.txt`);
  the main loop `FUN_0202a2d0` dispatches 8 scene updaters
  (FUN_0201ea94 / 0201df28 / 0201ef8c / 0201f130 / 02020c4c /
  02020e2c / 02022424 / 02027f7c) — identify the battle updater and
  its RAM flags, then reconcile with a live capture.
- No adapter change; host suite **40 passed, 0 failed**. No push.

## 🔶 2026-10-26 (round 8) — touch delivery PROVEN (round 7's prime suspect refuted); travel is an engine-logic question

Full detail: `~/oga-dbz-bench/completion-audit.md` "Round 8 log".

- **Touch delivery WORKS — proven at the guest level.** Method: a
  RAM differential from `r7-go5-end.st` (idle / idle-repeat /
  sustained tap at (128,100) / button control; full-RAM + alt-bank
  dumps at f=60/116/172/228; `r8-tdiff.py`). The bench is fully
  deterministic from a savestate (idle vs idle = 0 differing bytes),
  and the tap branch shows the game's own RAM recording the tap,
  pixel-exact:
  - **0x020D6468 = the game's current touch point (u16 x, y, pixels)**
    — the standing touch witness for future work (pen-up rest value
    (128,96)).
  - A sample history near 0x020AF468 holds the raw ADC pair
    (x<<4, y<<4); ARM7 WRAM 0x03809B00.. steps while the pen is down.
  - Static cross-check: `poke_touch` (Core/pokecore.cpp) →
    `ApplyInput` → `nds->TouchScreen(x,y)` per frame while down;
    melonDS `TSC::SetTouchCoords` expects PIXEL coords and the
    generated firmware's calibration is the identity map. The bench
    obj/ and Vendor/hostobj copies of `poke_touch` /
    `SetTouchCoords` are byte-identical machine code — no link-set
    divergence either.
  - What round 7 actually proved is narrower: the UIs it tapped
    (dialogue, dial, Switch slots, the map at the spots tried) do
    not act on those taps. A sustained ~180f tap on the click point
    also does not travel.
- **The map scene is script `script/us/cf/worldmap.sb`** (loaded
  live at 0x02282000). Its string pool is the destination table —
  (name, map-id, Wobj) triples, 26 destinations; Training Island =
  `tut01_00` / Wobj002. Entry points: `main` (code 0x888, 89 bytes)
  and `main_normal` (code 0x8E1); which one the wade entry runs is
  still open — round 7's "event map vs normal map" question is
  literally these two symbols. `main_normal`'s prologue sets the
  cursor-clamp globals (436, 216) and (248, 201) as script globals;
  the globals/input block is at 0x020D6460+ (touch witness above,
  (436,216) at 0x020D648C, (248,201) at 0x020D6498).
- **SetClickPoint** (native `FUN_02048574`) stages a jump record
  through the MapJump writer `FUN_02043bdc` and commits via
  `FUN_02031584` (flag writes, no coordinate table) — click points
  are not stored as RAM coordinate pairs (full-RAM pair search
  finds none), so the hotspot test lives in engine code / the
  script, not in a readable table.
- **Travel still NOT solved.** Next lever, in order: (1) disassemble
  `main` + `main_normal` bytecode (op anchors banked in the audit:
  `15 HH LL` push imm16 BE, `26 XX` pool index, `29 03 XX` /
  `20 03 XX 3D` global load/store, `23 XX` call); (2) determine
  which entry the wade runs and what its wait loop polls.
  Battles, the Active Guard capture and the live Zeni proof remain
  gated on travel. Housekeeping: the round-6 log's truncated tail
  is now explicitly marked in the audit (no results were missing).
- No adapter change; host suite **40 passed, 0 failed**. No push.

## Round 9 addendum — script VM decoded, travel solved, battle + Zeni proven

- **.sb VM fully decoded** from the ARM9 handlers (executor
  FUN_02010b78, dispatch @0x0209B4CC, lengths @0x0209B468): a
  tagged stack VM (tags 0 null / 1 int / 2 string / 3 ref3 / 4 fn);
  ctx[0] PC, [1] SP, [2] frame base, [3] native result, [5]
  pending PC, [6] value stack, [7] tag array, [9] suspend, [10]
  wait counter; op 0x5B = WAIT n frames, 0x49/0x4A = native call
  (argc word sits below the args), 0x62 = SWITCH. Disassembler:
  bench `r9_dis.py`. .sb sections: S0 code, S1 globals (VM base =
  section+8), S2 strings, S3 symbols, S4 native bindings, S5 path.
  **Bindings/natives must be resolved from the LIVE registry**
  (12-byte {name, fn, prm} records @0x020C8830): the ROM table's
  field order differs and its name↔fn pairing is off by one row
  for some entries — round 8's SetClickPoint attribution
  (FUN_02048574) was this trap; the real one is **FUN_020485EC**.
- **worldmap.sb read in full.** `main` (the wade's entry) checks
  Dragon Ball key items 316–321 + flag 690 for a completion event,
  else delegates to `main_normal`, which is **one-shot destination
  registration with no input loop**: SetClickPoint(id, p1x, p1y,
  name, mapid, b2, p2x, p2y, b3) + Create(Wobj) per destination,
  gated on progress flags. Kame House id 1 p1 (436,216) p2
  (248,201); Training Island id 2 p1 (430,266) p2 (44,224).
  SetClickPoint writes a 20-byte record at field-struct
  (0x020D2128) +0x434C + id×0x14 {active, radius=8 default, b2,
  b3, p1, name*, mapid*, p2}.
- **Travel trigger is engine code** (world-map scene FUN_02044F98;
  hotspot test FUN_02044DC8 = Euclidean distance in raw pixels via
  the DS sqrt unit; match if d < radius 8 → name ptr to
  struct+0x4348, jump record staged at 0x0209F8E8). Two confirm
  paths: (1) **cursor mode** (mode byte struct+0x433C == 1) with a
  destination named + **A newly pressed**; (2) touch mode
  **double-tap** (second tap within 7 px of the snapped point
  while named). Scene input descriptor @0x020AF454 (+0x6 touch
  state 0/1/2, +0xC held, +0xE pressed). Rounds 7–8 failed because
  the map opens in touch mode (A inert), taps were single, and a
  sustained tap is a drag. **Live proof**: D-pad glide to test
  point (430,266) → name 'Training Island' + staging tut01_00
  (44,224); one A press → arrival on tut01 (states
  `r9-e3-end.st`, `r9-ti1-end.st` on the bench). FUN_02031584 is
  the travel COMMIT (called by the executor FUN_02044C84), not a
  SetClickPoint side effect.
- **Battle detection: u32 @0x020CC668 == 1** (0 in field / map /
  field scenes); phase @0x020CC66C (0x11 command flow, 0x09/0x19
  tutorial pages, 0x02 post-battle tutorial chain). Battle actor
  records @0x020CDDE4 + k×0x24C: **HP cur +0x00, drain/display
  +0x04, MAX +0x08** (corrects the round-1/7 "(cur, max, copy)"
  labels — +0x04/+0x08 swapped); KI mirrors at +0x10/+0x14/+0x18.
  Party slots 0–2 (Krillin 300/105, Tien 320/110, Yamcha
  305/100); enemy name strings 'Giant Snake A' @0x020CE9D4,
  'Giant Snake B' @0x020CEF88. Commands are a radial wheel on the
  touch screen (Attack centre; Item/Skill/Defend around), header
  = current actor; targets lettered on the top screen; top-screen
  TOTAL = a multi-hit attack's running damage. First battle is a
  scripted tutorial ("automated" per the guide); turn order
  (guide: Speed-based) still needs a free battle to prove.
- **Zeni PROVEN LIVE: u32 @0x020CC770 went 0 → 80** at battle 1's
  resolution (round 7's code-derived address; gap 2 closed).
  Recovery regen observed at turn boundaries (guide-consistent).

## Round 9 addendum, part 2 — battle flow, Active Guard measurement, wiring

- **Post-battle semantics**: after battle 1 the flag pair sits at
  (1, 0x02) through the results/Krillin help chain — and the field
  is walkable under (1, 0x02). Battle speech therefore keys on
  flag==1 && phase != 0x02. Zeni read 80 in all post-battle dumps
  (payout landed before the first).
- **Command wheel is direction-selected**: UP Item, LEFT Super,
  RIGHT Switch, DOWN Defend, centre Attack; A confirms command,
  A again confirms target. Targets are lettered on the top screen;
  TOTAL = the running damage total of the current attack.
- **Battle 2**: walking to the west end of the arrival ledge
  (t=220) ambushes **RR Deserters** (a free battle, not the
  guide's scripted tutorial battle — that one is past the first
  Ki-charge boulder, still unreached). Party/enemy HP carried
  between battles; Recovery regen (+9/+10) ticks at turn bounds.
- **Active Guard measured, not wired**: telegraph = a flash icon
  above the target's head ~14 frames before the first damage tick
  (burst attack: 3 ticks of ~−18.5, 8f apart); a single heavy shot
  showed a white screen-flash at hit−25f, −73 in one tick. No
  clean timing ramp exists in 0x020C0000–0x020E0000 across the two
  captured attacks; attack-aligned candidates banked (0x020CCBBC:
  0→0x100 at ~hit−15→0x215 at hit; 0x020CCA2A: 0x101→0). Fine
  dump series: bench `r9-e22/e23/e24-dumps/`.
- **Wired (local, unpushed)**: battle start/end speech in
  `Core/dbz_adapter.cpp` (enemy names from 0x020CE9D4/0x020CEF88,
  fail-closed gates + 2-frame debounce) and a **party-speech fix**:
  HP/Ki maxima are the triple's THIRD word (+0x08), not the drain
  word (+0x04) the shipped code had been printing as the max.
  Host suite **50 passed, 0 failed**.

## Round 10 addendum — the cave: boulder broken, Active Guard timing model unified, turn order answered

Bench: `~/oga-dbz-bench/`, experiments E1–E45; full narrative in
`completion-audit.md` (round-10 sections). No adapter changes this
round (spec-only for Active Guard, per the round brief); host suite
holds at **50 passed, 0 failed**.

- **Tooling correction**: dump headers are NOT fixed-length — the
  `len=` digit count varies. Offline readers must split at the first
  newline. `r10_ramps.py` carried the old fixed-48 skip and produced
  misaligned reads (incl. a phantom countdown) until fixed; its
  address labels from before the fix are off by the header delta.
- **Cave path**: arrival ledge east end → north junction (battle 3,
  scripted-position RR Ex-Sergeant, free battle) → pillar plateau
  (s tops at 192) → winding east-side descent → middle ledge, where
  a scripted Ki-Blast lecture plays. Lecture text recovered from the
  resident scene script (~0x022845EF). A one-page leash ("Hold on!
  I haven't finished talking yet.") re-fires on off-route movement
  until the blast succeeds; it eats inputs while open and dismisses
  with one A.
- **Ki-charge boulder — mechanics proven**: charge gauge **u16
  @0x020D636E** = 0 idle, **+100/frame while B held, caps 6000,
  resets to 0 on release** (blast consumes the full charge). Player
  state word @0x020D2766 reads **12 while charging, 16 on firing
  frames**. A full charge from (71,42) facing DOWN missed; the same
  charge from **(14,42), directly north of the rock, broke it** —
  the blast is short-ranged; stand adjacent, facing the rock. The
  bottom-left HUD bar is the Ki meter (yellow segment = charge;
  part of the bar reads spent after a blast and refills over time).
- **Battle 4 (scripted, Giant Snake A + B)** on the boulder ledge —
  NOT the Active Guard tutorial (no tutorial pages, phase 0x11
  throughout; the tutorial battle is still deeper in). Captured
  all-Defend (E27, every-frame), then won with plain-A attacks:
  Snake A (slot 5: HP 45, stats [20,10,8,10,8,8]) killed; **Snake B
  has no record in the 0x020CDDE4 array** (slot 6 = 0/0/0 all fight)
  yet kept attacking after A died and the battle ended normally —
  second foes can live outside the record array; their name slots
  (0x020CE9D4/0x020CEF88) still populate. Zeni +80 (160 → 240).
  Krillin was focused down and **stays at 0 HP in the field** (no
  auto-revive). Wiring check (target 5) PASSED on battle 3's full
  chain (single phase exit (1,0x11) → (1,0x02) across kill, results,
  3 level-up screens, payout).
- **Battle 5 (random cave encounter, RR Ex-Sergeant)**: the
  0x020CCBBE countdown armed again (init 19 → burst first tick
  exactly at 0, vs Yamcha) — the ranged family repeats. Party acted
  before the Sergeant both turns. **No Zeni paid** (240 → 240),
  unlike battle 3's +80 for the same foe in a scripted position.
- **Active Guard — unified timing model (17 attacks, 2 battles)**,
  action-descriptor block **0x020CCBB0**:
  - **+0x03 (0x020CCBB3) = 0x80 | target party slot** while an enemy
    attack is inbound (17/17); clears to 0xFF at resolution.
  - **+0x0D (0x020CCBBD) = guard-window flag** (nonzero): opens
    before every enemy attack — a near-constant **~28 frames before
    impact** for light melee; earlier (≥45f) for heavy attacks.
  - **+0x10 (0x020CCBC0) = strike countdown for heavy melee**: holds
    19 through the approach, **−1/frame over the final ~19 frames,
    damage at 0** (3/3). Light melee keeps it constant at 19 — no
    countdown exists for light melee anywhere in
    0x020C0000–0x020D4000 (8 instances scanned).
  - **u16 @0x020CCBBE = countdown for ranged/shot attacks** (battle
    3: burst 13/23, heavy 21/28; battle 5: burst 19) — arms once per
    burst, damage starts at 0. Silent in melee battles.
  - The game's own help text (resident message DB): "Press the
    Button just before the enemy attacks… Watch for the icon above
    your character's head"; the button varies by the target's
    formation row. **Wiring spec written in the audit** (rising-tone
    cue; window chosen per family from the live witness; target from
    +0x03; fail-closed) — NOT wired; the tutorial battle's scripted
    attack must be captured first in case it is a fourth family.
- **Turn order — answered**: NOT a fixed stat sort. Battle 3's three
  turns all differ; the Sp-5 Sergeant acted 3rd then 2nd against
  Sp 9/9/10. Battle 5 (Sp 12/14 vs 5) went party-first both turns.
  **Order is re-rolled per turn; Speed at most a weak weight** (the
  guide's "influences" is the right word). No plain id-list queue
  exists in RAM (byte-hunt over a full battle's dumps). Record stat
  names (from the level-up hexagon): **+0x44 Pw, +0x46 Df, +0x48 Rc,
  +0x4C Sp, +0x4E Lk** (+0x4A Tc).
- **Level-up flow fully traversed** (battle 3 spoils): EXP/AP results,
  then per member a BONUS POINTS screen (pool 2): UP/DOWN select the
  stat, **RIGHT adds**, A opens the confirm dialog (Yes above No, No
  default; **UP + A = Yes**). These dialogs need **≥10-frame holds**
  — the 3-frame presses that work elsewhere silently fail here.
  Stats/maxima update in place at commit. Zeni paid in installments
  during the results pages (80 → 130 → 160 across battle 3's chain).
- **Slots 3/4 (600 HP, Lv 20) closed as invariant bystanders** —
  unchanged through field, cave, and three battles; never damaged,
  never acting; identity unproven and not load-bearing.
- **Open at round's end**: the Active Guard tutorial battle and
  Jackie Chun (1200 HP; 300 Zeni → expect 240 → 540) are still
  deeper in the cave, past the Phoenix Feather chest plateau whose
  entry route is unproven; bench-side, re-entering Training Island
  from the world map resisted the round-9 recipe in this lineage
  (taps land and snap, D-pad registers in the input descriptor, but
  the point never moves and no destination names — the map scene's
  state after an island-exit differs from round 9's plot-opened map;
  state of record `r10-e37-end.st`, cursor snapped on Training
  Island). Party: Krillin **dead (0 HP)**, Tien 188/344, Yamcha
  223/325, all Lv 2, Zeni 240 — revive/press on with two in the next
  round.

## Round 11 addendum — the revive, the tutorial battle, the cue wired

- **Revive solved on tut01**: the chest on the southern spur
  (reached by the chute at t≈25–44, s 51→0) holds a **Phoenix
  Feather**; the field Items menu uses it on a member (cursor
  starts on slot 0) and it revives at **half max HP** (Krillin
  0 → 160). The "!" balloon is the game's adjacent-interactable
  marker. Items/target cursor cells not isolated (navigation
  was screenshot-driven).
- **BattleTutTwo found and fought**: tut01_00.sb's EventSet01
  registers every zone (disassembled; r9_dis.py works on any
  .sb given a full-RAM dump of that map loaded). BattleTut =
  battle 1 (flag 2647); BattleTutTwo (flag 2667) = Krillin's
  "There's something else near…" / "Whoah! A ghost!" → a
  **Lantern Ghost** (record slot 5, HP 45, pays 60 Zeni), its
  zone on tut01's east wall upper stretch. Its in-battle
  DrawTutorial pages (phases 0x19/0x09) are the Active Guard
  teaching (text = the message DB's "Active Guards (1)/(2)").
- **The tutorial attack is the RANGED family**: descriptor
  arms (target byte set), flag +0x0D = 1, u16 @0x020CCBBE arms
  29 → 0 (launch), flag = 2 through flight, impact 47 frames
  after zero (Krillin −49) — flight time is per-attack; round
  10's Sergeant shots landed at zero. A second ghost attack
  re-armed at ≥25 and burst (−15, −29).
- **Target-byte correction (supersedes round 10's wording)**:
  +0x03 read 0x81 on both ghost attacks while slot 0 took the
  damage; with round 10's 0x80→slot 0 and 0x82→slot 2, the byte
  is **0x80 | formation row** (per-battle), NOT a party slot.
  Never name a member from it.
- **The cue is wired** (Core/dbz_adapter.cpp DbzGuardFrame;
  CueSnapshot guard_active/guard_frac/guard_beat;
  poke_cue_guard in pokecore): rising sweep normalized per
  family (ranged countdown / heavy +0x10 / 28-frame flag lead),
  beat at the opening, "Guard!" setup, factual outcome line
  from the HP delta, 100-frame post-close grace, fail-closed
  on insane bytes and party-action blocks. Host suite 68/0.
  App synths don't poll poke_cue_guard yet (contract in
  pokecore.h).
- **tut02_00 entered** (through the MapJump2 object at tut01's
  east wall top — an interact/point object, not a walk-on
  tile): its script's event layout is decoded in the audit
  (Bulma's two calls, PoisonLock = the gas gate + her red-slot
  warnings, Crystal → common event c0002_01, chest items
  1404/1405). The entry terraces are a mapped loop; the
  eastern basin (Bulma's line, chests, CC Point, crystal) is
  NOT yet reached — that and Jackie Chun carry to round 12.
  Party at pause: Krillin Lv4, Tien Lv5 **down again**, Yamcha
  Lv5, Zeni 1600; the only Phoenix Feather is spent, so the
  CC Point past the gas is now a hard need.

## Round 12 addendum — app-side guard-cue hookup SHIPPED (code); the basin door still shut

**The Active Guard cue is now wired end-to-end in the app** (the
round-11 Core work had no consumer). Both platforms poll
`poke_cue_guard` from the same per-frame snapshot as the health
poll and render it with the Budokai Access model the design
settled on: a continuous tone whose pitch climbs as the window
runs (frac 0 → 1), peaking at the guard moment, plus a soft
low beat on the window's opening frame. Numbers, identical on
both platforms: exponential sweep 300 Hz × 3^frac (300 → 900
Hz), gain 0.30 + 0.15·frac, centred (the window names no side
and the cue must not invent one); beat 160 Hz, 45 ms, gain
0.35. The sweep rides the synths' TICK family level; the beat
is a dedicated one-shot voice (the sweep is already sounding
when the beat lands).

- iOS: `CueSynth.updateGuard(active:frac:beat:enabled:)` +
  a `guardBeatVoice`; `GameSession.refreshCue` polls
  `poke_cue_guard` immediately after the health poll (same
  placement logic — Saiyans battles run in field areas, so the
  poll sits before the field-question early return).
- Android: `PspAdapterHostCuePoll` grew three trailing outs
  (`guard_active`, `guard_frac`, `guard_beat`) filled from the
  same snapshot; the JNI packing grew to ints[7] / floats[8]
  ([5]=guard active, [6]=guard beat, [7]=guard frac);
  `PspAccessibilityScript.CuePoll` carries the three fields;
  `PspCueSynth.updateGuard` mirrors the Swift synth;
  `PspGameSession.pollCues` calls it beside `updateOneShots`,
  gated by the same battle-cues switch.

Proven vs inspected, stated plainly: the Core contract and its
behaviour are host-proven (suite 68 passed, 0 failed, re-run
after these edits). The Swift/Kotlin/C++ app code cannot be
compiled on the bench VM (no toolchain); it is verified by
close inspection against the shipped BBS cue-bridge pattern,
kept minimal and symmetric. **Actually hearing the sweep on a
device is the user's test** and remains open.

**Bench (completion) side — what 48 experiments banked:**
the pocket/shaft/terrace system and the basin landing are now
mapped in the party-record frame end to end (spur column at
t=0 with its stair steps (0,62)/(0,70) and slide; the rim tier
at s≈83–85 reached by stairs at t≈155: s 61 → 73 → 85;
MapJump1's firing recipe at the shaft foot: ~900f DOWN hold,
then LEFT; the island-exit world map is a proven dead end —
it never engages cursor mode, matching rounds 10–11). Two
battle-stall traps are solved and banked in the audit: the
command wheel remembers its selection (a RIGHT-fed wheel
parks the battle on the Switch screen — recover with B,
then a direction + A to execute a real command, then the
A-cadence), and level-up results park on the bonus-points
allocation screen (RIGHT per point, A, UP+A). The Ki charge
never starts in the open field — per the guide it is
contextual on facing a glowing rock ("hold B until the red
Ki gauge glows, release to punch", twice to break). Party:
Krillin/Yamcha up (Yamcha Lv7), Tien still down (no revive
item), Zeni 1860 → 2560 in this lineage.

**Still open:** the basin floor's door. Bulma's calls, the
Gas Mask / Darkness Card chests, the CC Point, the crystal
and Jackie Chun all sit in the script's northern basin
(zones at map px s≈278–299), and no walked route from either
cave section reaches it yet. Named leads for the next round:
the southern tier below the rim's west dip (live actors at
record s<0 — (164,−21), (222,−45)); the second tut01 boulder
in the guide's counter-clockwise order; and the .map object
table (dMAP, 38×32 tiles, still undecoded — its trigger
tiles would name every door outright).

## Round 13 addendum — the northern basin and the broken boulder (2026-10-26)

Full detail: `completion-audit.md` §13 (bench `~/oga-dbz-bench/`).

- **dMAP object-table decode (task 1) — not achieved from the file side.** The
  `.map` tail (starting at the u32@+8 offset; header +6/+7 = width/height in
  tiles) is a variable-length byte stream (alphabet dominated by 0x8F, 0x10,
  0x01, 0x03, 0x20, 0xFF, 0x06; bytes/cell varies 0.83–2.53 by map); no LZ77
  header; codec unidentified. The RAM actor table is the working substitute:
  position pairs (u16 t, u16 s) at `0x020D275C + k*0xEC`, player k=1
  (`0x020D2848/A`); static world objects appear there per screen (chests,
  boulders, the PoisonLock dummy were all read live). A Ghidra hunt for the
  dMAP loader in arm9.bin is the identified fallback for the door list.
- **tut02 script closed.** Full disassembly of `tut02_00.sb` (75 symbols):
  ObjSet01 creates the two BreakObj boulders (script px (141,82) and (427,99)),
  the PoisonLock dummy (script (388,324) in the final reading), the IsBattle
  anchor and two EffectSeton points — and EventSet01 registers exactly the
  events listed in §13 of the audit (two Bulma lines, the two one-way slope
  locks keyed to chest flags 1583/1584, PoisonLock, the crystal Move-zone,
  the WorldMap point). `CreateCCpoint` is never called in tut02 — the CC
  Point is map data. **There is no second scripted door/warp; the basin route
  is physical.**
- **The northern boulder is broken (first tut02 boulder break).** From the
  basin landing (record (0,192)), LEFT taps walk the top path to (37,192);
  face the rock, hold B: the player state word reads 12 (charging) and the
  charge gauge (`u16 @0x020D636E`, +100/frame, cap 6000) rises to full;
  release fires the punch (state 16). Two full charges break it. This proves
  the contextual Ki charge in tut02 (it starts only when facing a glowing
  rock — never in the open).
- **Orientation/coords settled.** The party record is VIEW (camera) coords;
  script px is the true map frame, and conversions are per-view (anchor
  live, never convert globally — the round-12 (24,0) conversion is
  withdrawn). Record s+ = screen-north (E74 step test). The basin events
  (script s 257–355) are north of the landing.
- **Battle-protocol additions.** (a) Battles that start while RIGHT is held
  open on the Switch/formation screen and, once saved, are unresponsive to
  every input class (buttons, direction+A combos, taps, shoulders, START/
  SELECT, long holds) — observed twice, both the terraces' 3-snake
  encounter (Yamcha sole attacker; Krillin in reserve, Tien down). Never
  end a run on such a battle. (b) Direction legs fed into a live battle can
  get the party wiped (game-over → reset; the resulting state is poison).
  (c) The standard recovery (B taps → plain A-cadence → allocation
  clearing) otherwise went 9-for-10 this round.
- **Geography corrections to round 12.** The "southern tier" was the world
  map (screenshot proof); the rim dip exit tile is a round trip (press A
  within ~150–200f on the map, before gliding, to re-enter at (256,64));
  the slide down the column's west face fires only from the MapJump-arrival
  stand at (0,192) (DOWN ≥400f: 192→179→144→108→73→50).
- **Status:** the Prologue chain remains gated — Bulma's first call never
  fired, chests 1404/1405 (Gas Mask / Darkness Card) unopened, CC Point
  unreached, Jackie Chun (boss id 174) unfought. Clean banks and the
  round-14 lead list (rim north edge; west-staircase continuation;
  mid-slide branch taps; dMAP loader in Ghidra) are in the audit's §13.45
  close-out.

## Round 14 addendum — the dMAP loader found; the basin door still shut (2026-10-26)

Full detail: `completion-audit.md` §14 (bench `~/oga-dbz-bench/`).

- **The map loader/parser is identified in arm9.bin.** The script-native
  table (~0x0209DDD8) orbits the field struct (base held at 0x02044024's
  pointer = 0x020D2124). FUN_02048FFC is the map-change executor: given a
  `.map` path it stores the path at fieldstruct+0xF0 and calls
  **FUN_02043D1C(path, flag)**, the parse routine: it loads the file via
  FUN_020082C0 into the scratch buffer at **0x02360000**, reads header
  bytes +6/+7 as tile width/height, walks the chunk list at +0x10
  (chunk = u16 type, u16 byte length, payload), and processes the
  section at +0x10+u32@+0xC (tut02: file +0x40), which is the **palette**
  (0x200 bytes to palette RAM). The processed map image is built at
  **0x02330000** (verified by full-RAM capture, bench runs r14-e1/e2:
  same 8-byte header, u32@+8 rewritten to the processed layout's tail
  offset). The type-0x100 chunk is a **points list**: 7 records for
  tut02, decoded with the parser's own field math as (213,144),
  (205,247), (114,264), (113,308), (207,162), (126,263), (81,312) with
  link ids 13/14/10/13/11/9/6 — they match neither the MapJump arrival
  nor the object list, so they are **not a door list**; the "dMAP object
  table names every door" hypothesis is closed.
- **The collision/trigger grid was NOT isolated.** The two large byte
  grids the parser builds (512×384 at 0x023A0000 and 0x02220000) are
  graphics renders of the map (cell-level dither checkerboards; every
  script anchor samples 0x00 in them). The tail stream (tut02: 7,764 B)
  is consumed by code not yet identified — the remaining static route is
  an xref hunt for the tail's consumer during actor movement.
- **Bench results (all negative for the basin, each closing a named
  lead):** the boulder plateau's east end is the t=0 column and no
  chest sits on the plateau or its ledges (the Medium Bean chest is
  presumably on the basin side, per the guide's ordering); the rim's
  north edge is walled along t 155–256; mid-slide UP taps at t≈66/127/
  137 catch no pillar branch (lip steps only); the upper-ledge pocket
  is a dead-end spur (north walled under a 600f hold; west is a slope
  funnel); the terraces "west staircase" is a t=441 chute from the top
  ledge toward entry level, two-way in part, with a working UP
  tier-catch. The battle recovery protocol (B taps + A-cadence) went
  8-for-8 across rounds 13–14.
- **Status:** Prologue still not completed — Bulma's call never fired,
  capsule chests unopened, CC Point unreached, Jackie Chun unfought,
  Tien still down. Round-15 leads are in the audit's §14.14 (DOWN at
  the top ledge's middle (427,−50) toward boulder 2's plateau; the
  tail-consumer xref; ground-truthing the 7 decoded point records).

## Round 15 addendum (2026-10-26) — the .map format is fully decoded; no collision grid exists in the file

- **FUN_0200a6c0 (the layer decoder) is fully reversed** and
  reimplemented in Python (`~/oga-dbz-bench/r15_decode.py`), validated
  **byte-exact** against live RAM: tut02's body decode (transposed)
  equals 0x02220000 with 0 diffs, its tail decode equals 0x022B0000
  with 0 diffs, and kam01 validates the same way (only runtime-mark
  diffs, ~878 cells). The format is a 2D-aware LZ: control <0x10 =
  literal run of c+1; 0x10 = RLE of the last byte ×(next+3);
  0x20–0x7F = back-reference with displacement T[c&0xF] and length
  (c>>4)+1; ≥0x80 = back-reference T[next byte], length (c&0xF)+3,
  plus (c>>4)&7 trailing literals; 0x11–0x1F are no-ops. The
  displacement table is built from pointer globals at
  0x0200A84C/850/854/858 (tables at 0x0209B10C/12C/14C/156) plus a
  14-entry swap pass over T[2..15].
- **Correction to the round-14 record:** the "collision/trigger grid
  not isolated" question is now closed — **there is no collision or
  trigger grid in the .map file at all.** Both layers are graphics:
  the body is the map's pixel image and the tail is an overlay image;
  the parser stages them through 0x023A0000, transposes them into
  0x02220000/0x022B0000 (8-column-strip order, FUN_0200a968), and
  hands both to the camera struct FUN_020130d0 builds at
  fieldstruct+0x3C9C. The only code xrefs to the staging base are a
  resource loader, a record search, a ring buffer and a memcpy — no
  movement-time consumer of the tail exists. (Two capture traps for
  future rounds: the scratch grid at 0x023A0000 is overwritten after
  parsing — a stale tut01 decode sat in r12-e26-end.st's saved RAM,
  and a 5-row strip at y 256–260 is overwritten live; the transposed
  copies stay pristine.) The real collision source (per-tile
  attributes or the step-reader's tables) remains unisolated; the
  trigger side is fully accounted for by the script event zones and
  the object list (round 13).
- **The 7 point records are step-marker ("foot-change") registry
  slots.** The parser loads them into the field struct's 16 slots at
  +0x3BC4 — the same slots the script native FUN_020480c8 writes at
  runtime with (x, y, link, flag) records. In the tail overlay each
  point sits in a speckled 0x0C-texture neighbourhood (a floor
  texture, not a filled patch). They are not doors, not warps, and
  every one lies in the still-gated territory; their link ids' effect
  is unknowable without the step-reader.
- **The one untried step was tried:** DOWN from the top ledge's
  middle ((437,−50) stand) drops once, diagonally, to a chute landing
  at (426,−68) — DOWN there is walled, A presses are inert, and the
  west exit drains back to the entry ledge. Boulder 2's plateau
  (record (427,−93)) was NOT reached; the chute system is a closed
  drain like the terraces staircase. New clean banks: r15-e1
  (441,−50), r15-e2 (437,−50), r15-e3 (426,−68), r15-e4 (393,−157),
  all Zeni 2180, no battles fought this round.
- **Status:** Prologue still not completed; the chain remains gated
  on the basin floor. Round-16 leads (audit §15.5): WEST/DOWN probes
  from the (411,−87) channel stand; an xref hunt for the step/
  collision reader via the point-slot array and actor position
  writes; naming the overlay's terrain values by cross-map
  comparison.

## Round 16 addendum (2026-10-26) — the channel system closed; the collision reader found

- **The channel stand (round 15's lead) is a closed loop.** West
  along the channel floor from the chute landing (426,−68) ends
  at record (389,−87) = global (389,105), pinned by the BODY of
  static actor k24 at global (388,105) (A presses inert — not an
  interactable). Above the channel runs a ledge at record s=−65
  (t 389..428) reached by a NE ramp from the channel end; its
  north and east are walled and its east end drops back to the
  chute landing. The t=411 "channel stand" is a tap-catch on a
  long diagonal slide (411,−72) → (402,−160) entry level, not a
  floor. DOWN from the channel end drops to a new shelf at
  record s=−108 (t 385..402) whose only exits are a slide drain
  to the entry ledge and a NE ramp back up to the s=−65 pocket.
  The entire east terraces complex is now mapped and closed:
  no route to boulder 2's plateau or the basin floor.
- **The collision/step reader is identified** (audit §16.6).
  FUN_0202EEE0 is the per-step field processor: it scans the
  foot-change slot registry (live base fieldstruct+0x3BC8;
  records (x, y, link, flag); only link≥0 slots are active) and
  maintains the actor's ground byte (actor work +0x6F) from the
  nearest in-radius slot whose flag matches cyclically. Movement
  runs FUN_0202D7E8 (step executor with diagonal fallbacks;
  writes the actor's 20.12 fixed-point position at work
  +100/+0x68) → FUN_0202D42C (5-point collision probe; a sample
  is clear iff the attribute query passes AND the camera-space
  occupancy byte grid at 0x022F1400 is 0 there — actor bodies
  block via the second test) → FUN_0201419C (the attribute
  query: grid pointer and tile dims from the map work struct at
  fieldstruct+0x3CA0; attribute assembled from two u16s of the
  strip-transposed buffer under nibble mask 0xF0F). The movement
  mask is 0x8000 >> (ground byte): the attribute is a ZONE
  bitmask. On tut02 the grid lives at 0x022B0000 — the same
  buffer as the tail graphics layer — and the assembled
  attributes use only bits 0x8000/0x4000/0x1000 (zones 0/1/3).
  Flood fill (bench r16_flood.py): the zone-1 layer is a single
  component joining the terraces stands to the basin-floor
  statics, chest 1405 and the PoisonLock; so the gate is not
  raw connectivity but the ground-byte/probe semantics at the
  component's edge (the player's live ground byte could not be
  read this round — the actor-work pointer at fieldstruct+0x30
  is 0 in field captures; see audit §16.6 for round-17 leads).
- **Overlay terrain values named** (audit §16.7): the tail
  byte is texture, not a movement attribute. kam01's tail has
  no ≥0x80 values at all — its wadeable sea reads 0x00 and its
  island reads 0x08/0x01 — which falsifies the "high value =
  wadeable water" reading of tut02's ≥0x80 region (x 140..314,
  y 150..297; a floor sub-texture overlapping the west/central
  floor statics). Terrain semantics live in the assembled
  attribute's zone bits, not in any single overlay byte.
- **Status:** Prologue still not completed; the chain remains
  gated on the basin floor. New clean banks: r16-e1 (389,−87),
  r16-e9 (385,−108), r16-e11 (402,−108), r16-e12 (405,−65),
  all Zeni 2180; no battles, no parked/poison states, no bench
  mishaps this round. Round-17 leads (audit §16.6): read the
  player's ground byte live; simulate the full probe semantics
  offline per candidate ground byte to find the exact boundary
  cells; identify static k24 from the object list.

## Round 17 addendum (2026-10-26) — probe simulated; ground byte read live; floor is zone 0

- **Slot-toggle rule** (FUN_0202EEE0, audit §17.1): when a
  step returns zero displacement, the foot-change slots in
  radius are scanned; a slot qualifies iff its flag equals
  the current ground byte or (byte+2) mod 3; the byte then
  becomes (flag+1) mod 3 (byte == flag) or the flag, and
  the step retries. With tut02's all-flag-0 slots the only
  possible toggle is **byte 0 ↔ byte 1**; bytes 2/3 are
  unreachable in this map.
- **Offline probe simulation** (bench r17_sim.py; exact
  port of FUN_0202D42C + the executor's slide chains,
  audit §17.2): the basin floor proper is **zone 0**
  (attr 0x8000; chest 1405's own cell included) — round
  16's "floor is in the Z1 component" was an artifact of
  its ±6 px window membership test. Byte 0's reachable set
  covers the floor in the model; byte 1 and byte 2 are
  8-cell dead layers; byte 3 (Z3 bands) reaches only k16.
  There is no zone key for the basin.
- **Live ground byte = 0**: the actor-work layout is the
  actor table record's layout (position shorts +0/+2,
  facing +0x6C, ground byte +0x6F); k1's +0x6F reads 0 in
  the landing, terraces and lip captures alike.
- **Static k24 is inert**: no script pointer (+0x9C = 0),
  no interest registration (+0x74 = 0), only a template
  pointer (+0x78). Its body is its whole presence; no
  trigger arms a crossing on it.
- **The sim's crossing failed live** (audit §17.5/§17.6):
  UP from the top ledge (441,−50) climbs a uniform 9 px
  shelf to (441,−41) and stops; the east diagonal does not
  exist live. Terrain, the ground byte, camera bounds,
  battles and script leashes are all eliminated as the
  cause; the leading suspect is the probe's fan clipping
  the ladder actor k9's occupancy block under the statics'
  (+13,+34) anchor offset — unproven inside measurement
  error. The ladder is the floor platform's down-exit.
- **Status:** Prologue still not completed; the chain
  remains gated, now on a physical seam rather than any
  zone mechanism. New clean banks r17-e1 (441,−41) and
  r17-e2 (424,−45), Zeni 2180; no battles, no parked or
  poison states; one minor self-reported bench mishap (an
  empty-dumpspec no-op run, §17.7). Round-18 leads in
  audit §17.8 — first: the basin plateau's own ladder
  (round 12 E16), never surveyed.

## Round 18 addendum (2026-10-26) — the window transform pinned; both ladders are intra-loop connectors; Route B is a spiral drain

- **Window transform (audit §18.3):** in a 10-frame-interval
  full-RAM series over the lip climb (run r18-w1, 211 dumps),
  fs+0x3CC0 = 0x00C00100 and fs+0x3CC4 were constant before,
  during, and after the climb; the whole 9 px climb landed
  inside one 10-frame interval (a step snap). The identity
  record + (fs+0x3CC4, fs+0x3CC8) = k1's actor-table fixed
  pair (+0x64/+0x68) holds in every capture checked (cap1,
  cap3, r17-cap1, the w1 series); fs+0x3CC8 is a camera-scroll
  component that compensated the record's s during the climb,
  leaving the fixed pair invariant. The fields undergo no
  transition at the lip stop — the round-17 window hypothesis
  is dead, and no sim recompute on window grounds is
  warranted. The fan-clip occupancy suspect remains the only
  standing explanation for the stop, still unproven.
- **Actor fixed pair is view-independent map px** for script
  objects (verified via the exact two-view shift (+52,−73)
  cap3→cap1): chest 1404 = (132,111), chest 1405 = (370,169)
  — the chain's chest targets. Round 12's basin-frame
  "chests" (311,101)/(329,107) are k11 (map (311,293), the
  crystal/CC-Point candidate) and k12 (MapJump1's point).
- **The (190,85) "ladder actor" is k0**: a dynamic NPC-class
  actor on the rim tier (tpl=0, scr=0, face 1, ground byte 1,
  fractional fixed pair), frozen through 2,400 idle frames in
  the landing view. Not a ladder. Both real ladder graphics
  in the west complex are two-way connectors inside a closed
  loop: the boulder plateau's ladder (plateau (37,158) ↔
  s=143 shelf at (37,143)) and the round plateau's north
  stair ((15,141) → (17,175), one step per UP tap). The
  basin floor plateau's own ladder faces the chasm at the
  landing tier's east end (tier t 0–63 at s=192; floor
  furniture at record t≈97–306, s≈200–212).
- **East strip / Route B (audit §§18.10–18.13, §18.16):**
  the top-ledge shelf walks east under taps to (439,−51);
  at t=441 a slide face — a spiral formation — descends to a
  standable bottom at (441,−124); horizontal inputs on it
  only re-slide the party; boulder 2's plateau is unreachable
  from it. The lip↔shelf ramp at t=441 is a two-way stair.
  Route B is a drain, not a seam crossing.
- **Phase 0x11** at 0x020CC66C is a live battle / results
  phase (the flag word at 0x020CC668 stays 1 in the field).
  A phase-0x11 park after a recovery cadence may be the
  level-up allocation screen, not an unresponsive battle —
  read the screen before judging (§18.6). The recovery
  protocol (B taps + Defend + A-cadence; RIGHT holds spend
  allocation points; UP+A commits) went 2-for-2 this round.
- **The dip (256,65) exits the cave to the world map**
  (cursor at record (180,46) on exit; §18.15 corrects
  §18.14's "north stair" misread of cursor glides). The band
  slide tops out at the rim's east end (256,80) without
  funneling out when arriving along the rim's north edge.
- **Status:** Prologue still not completed; Bulma never
  fired, no chest opened. Every brief-named route is closed
  with evidence. New clean banks: r18-e3c (17,175) Zeni 2700
  (Yamcha Lv8 in that lineage), r18-e6c (441,−53) and
  r18-e7 (441,−41), Zeni 2240. New parked battles:
  r18-e2 (0,135), r18-e6 (399,−72). Round-19 leads in audit
  §18 close-out — first: boulder 2's plateau north edge
  (guide: the route runs counter-clockwise past it to the
  next screen).

## Round 19 addendum (2026-10-26) — the occupancy grid decoded; the fan-clip suspect dead; every physical approach now closed

- **Occupancy grid layout (audit §19.3):** the grid at
  0x022F1400 is a 128×96-cell camera-space buffer, row-major,
  pitch 128, one byte per 2×2 px cell, covering exactly the
  256×192 top screen; value 10 = occupied. Of the 343
  value-10 cells in a 256 KB scan window, only 30 form the
  grid's one coherent block in the lip view — ladder k9's
  footprint: cells x 63..68, y 16..21 = screen (126..137,
  32..43), matching round 17's independent reading. Across
  the party's 9 px lip climb the block shifted +4 rows at
  constant x (camera scroll, cell-quantised); the party
  itself is not rasterised into the grid.
- **Fan-clip verdict: NO (audit §19.3).** The probe's north
  fan reaches ≤ ~6 px past the party's anchor; the ladder
  block's south edge sits ≥26 px beyond that reach under
  the feet-anchor convention (≥10 px under the most
  generous anchor). The ladder's occupancy block cannot
  clip the fan. Round 17's last standing suspect for the
  lip stop is dead; with terrain, the zone byte, the
  window transform, and leashes all previously eliminated,
  the lip is simply not enterable — the ladder is an
  exit-only connector.
- **Boulder 2's plateau and the spiral (audit §§19.1–19.2):**
  A-probes inert at every stand — the chute landing
  (426,−68) and its east lip (428,−65) (DOWN is wall; the
  party oscillates between the two stands), the ramp base
  (441,−53), and every catch down the spiral face to its
  bottom arc, which reaches (441,−143) (deeper than round
  18's −124 reading). The guide's "east loop downhill
  Small Bean" is on none of the spiral's stands.
- **The landing's west side does not exist (audit §19.4):**
  sixteen RIGHT taps from the MapJump landing (0,192)
  produce no step and no facing change; t=0 is the tier's
  west wall.
- **k0 cannot be named (audit §19.7):** the rim at
  (190,85) is empty and walkable in every loadable state
  (walked through this round; A-probed in all four facings
  at 2 px distance, dialogue witness 0); k0's record is
  unscripted (tpl=0, scr=0). An ambient shell, not a gate.
- **Rim access (audit §§19.5–19.6):** the §13.8 low route
  works with taps (band east at s≈53–61 → UP-tap climb of
  the rim face at t≈155–168 → rim tier); the rim is
  encounter-bearing (a rim battle won by the recovery
  protocol, +80 Zeni). The rim's east end (256,80) is a
  one-way lip over the exit dip: UP taps (§18.14) and
  RIGHT taps (§19.5) both step the party south into the
  MapJump exit from that stand. One bench mistake,
  self-reported: r19-e4 repeated §18.14's dip trap on a
  wrong frame assumption (RIGHT = −t in the rim view).
- **Status:** Prologue still not completed; the chain
  remains gated, and every physical approach to the basin
  floor — west loop, chasm, landing, rim, terraces,
  channel, spiral, chute — is now closed with evidence, as
  is every mechanism hypothesis (zone key, window
  transform, occupancy fan, script leash, decorative
  statics). New clean banks: r19-e2 (441,−143) Zeni 2240;
  r19-e5 (179,85) Zeni 2640. Round-20 leads in the audit's
  round-19 close-out — first: rebuild the probe simulation
  on the decoded live occupancy grid and settle whether
  any cell-exact path crosses the chasm at all.

## Round 20 addendum (2026-10-26, audit §§20.1–20.8)

- **Map mislabel corrected (audit §20.1):** the
  "landing/west loop/chasm/rim" geography of rounds
  12–19 is **tut01** (first cave screen); the terraces,
  lip and spiral are **tut02**. The two floors are in
  different maps: tut01's eastern floor across the
  chasm, tut02's event floor in its south. Resident
  script name read at 0x02282000 in both canonical
  captures is the proof.
- **Warp audit (audit §20.3):** exactly three warps in
  tut01+tut02 script data, all ungated map-edge points:
  tut01 (0,229)→WorldMap; tut01 (512,117)→tut02
  (44,323); tut02 (1,329)→tut01 (473,112). None lands
  on either floor. Correction: (311,293)/(329,299) are
  treasure boxes 1583/1584, not a CC Point / MapJump
  point. tut02 boxes: 1404@(132,111), 1405@(370,169),
  1583@(311,293), 1584@(329,299).
- **Slot writers (audit §§20.2, 20.6):** the active
  foot-change slots are the map's own file records —
  no script wrote them. FUN_020480C8 is the slot writer
  by code (table label "SetFootMark"); FUN_020481B8 is
  a flag setter (label "SetKidanLv"); FUN_0204803C
  (label "SetFootChange") writes 4-byte marker records
  at field-struct +0x3C68 and is not a ground-byte
  setter. Neither map's script calls any of the three.
- **Chasm simulation (audit §20.4):** cell-exact BFS
  on the extracted attribute grids: in tut01 a path
  from the entry stand to the east side exists on
  paper, crossing the 0x1000 band at file y 232–233
  (the "chasm" is a zone band, not a void); in tut02
  the arrival reaches the eastern floor only if the
  persistent cross-map actor records are not statics —
  the neck is at file x 188–196, suspect disc k8 at
  (190,188). Live stands still cannot be aligned to
  the file frame (record + camera fields sum to a
  constant spawn anchor), which is round 21's first
  job.
- **tut02 event chain (audit §20.5):** PartyTut01
  (Bulma call 1, flag 2601), UpLock soft lock
  redirecting to box 1583, PoisonLock on the cloud
  dummy at (412,324), PartyTut02 (Bulma call 2),
  DownLock, and the crystal zone registered only when
  the global progress counter GetMaxFlag(0,1199)==55,
  calling c0002_01 (Jackie Chun).
- **Bench (audit §20.7):** the landing lip at (63,192)
  re-confirmed hard; the s≈92–108 contour (Task 4)
  walked and closed — its ledge runs only north–south
  into the landing tier. The post-battle level-up
  allocation screen is solved: RIGHT allocates, A
  opens the confirm dialog, selection starts on No,
  UP+A commits. New clean banks: r20-e35 (256,0) Zeni
  2900 (richest), r20-e43 (56,192) 2540, r20-e52
  (35,192) 2380. The Prologue remains incomplete;
  tut02 was never entered this round.
