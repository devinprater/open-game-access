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

Decoded against the character records (`rec = (ptr - 0x020CD754) / 0x24C`):

| f=2000 (title screen) | f=12000 (in game) |
|---|---|
| rec-1 +0x158 | — |
| Goku | — |
| Gohan | — |
| Piccolo | **Piccolo** |
| Krillin | **Krillin** |
| Tien | **Tien** |

**Facts established by measurement:**

1. **The array is live state.** It changes with game progression — 6 entries at the
   title screen, 3 in game. A static template would be byte-identical across runs.
2. **`0x020CC794` is a count word.** Its **low u16 equals the number of entries**
   (`0x0006` → 6 entries, `0x0003` → 3 entries). The high u16 stayed `3` in both runs.
3. **The array is a sliding window over the character records.** In both snapshots
   the *last* entry is Tien (`rec4+0x158`) and the entries are consecutive records.
   Only the **start** of the window moves — so this looks like a rotating or
   consumed-from-the-front list, not a fixed roster.
4. **The rest of the character table is unchanged** between the two runs (names,
   `+0x180` chain and the stat fields at `+0x1F8`/`+0x208` are byte-identical).

⛔ **What is still NOT established: what the window means.** It is a
code-referenced (75×), live, counted list of characters that does not match the
player's party at either snapshot. Do not narrate from it. Plausible shapes — a
load/initialisation queue, a "characters present in the scene" list, or a
battle-roster window — are all still open, and picking one without evidence is
exactly the error that produced the two contradictory claims above.

### ⛔ The lesson, and it is about method

**Two confident, opposite conclusions came from the same data — both from reasoning
about a snapshot instead of measuring change.** The first assumed a meaning; the
second assumed "doesn't match the party ⇒ not live". Neither was tested. One extra
run at a different frame count settled both.

**The generalisable rule: to learn what a field DOES, measure it across a state
change — do not infer its meaning from one snapshot.** Comparing two runs of the
same session at different frames is the cheapest such change, and it needs no input
scripting at all:


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
