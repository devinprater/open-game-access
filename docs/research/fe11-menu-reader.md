# FE11 menu reader — research design (fresh boot, no save)

Status: IMPLEMENTED Oct 1 2026 in `Core/fe_access.cpp` (front-menu reader) +
`Core/fe_adapter.cpp` (MenuState/Next/Prev/Left/Right), proven live on two
boots (title / menu / Normal / Hard speech in `~/fe-proof-*.log`). CORRECTIONS
to the September claims, found during implementation: (a) the whole menu string
bank loads TOGETHER — "difficulty descs resident ⇔ Difficulty" was wrong (the
early snaps were title art, not menu); Main-vs-Difficulty now separates on the
stage byte 0x020E3CA8 (00 menu, 01 difficulty, 12 snaps / two boots) with the
CA9 sanity neighbour. (b) The 020E6048 struct is live on title too — "zeroed
in Title" was wrong for the same reason. (c) Fresh-boot DOWN never moves the
menu cursor (5 presses, menurows.txt) — nav re-speaks the anchor. Save-file
rows, file-select/preps, and prologue advance remain queued RE below.
All claims below are backed by fedump runs in September 2026; dump/shot
paths are in the appendix. Absolute RAM addresses are BOOT-OBSERVED values
for content-scan hits and struct locations — never hardcode them; the scan
method next to each one is the normative spec. Heap structs were observed
stable across same-path boots but the heap is known to shift with different
input histories, so every reader must search, never assume.

Harness note (fixed Oct 1 2026): the September runs below used a scratch link
(`~/fedump-fe`) because `scripts/fe-dump.sh` + `scripts/build-host.sh` did not
link at HEAD (missing `psp_*` backend and mGBA 7z objects). The supported link
now works: `Core/host_harness_stub.cpp` (abort-on-call PSP/7z stubs, host-only,
never in app lists) + `dq9_adapter.cpp` in both source lists. All October
re-proof runs used the repo harness directly.

Method per screen: (1) screenshot both LCDs per state and read with vision
(not pixels-diffing); (2) content-scan RAM dumps for exact `fe/text-db.txt`
strings; (3) cursor-hunt: flip-flop (A→B→A returns) + no-press control
resample, small ordinals only; (4) re-proof across 2+ boots.

## 1. Title / opening movie

Screens observed (bottom LCD; top mirrors with art variants):
- Logo splash: `FIRE EMBLEM` / `Shadow Dragon` plaque /
  `© 2008-2009 Nintendo / INTELLIGENT SYSTEMS`.
- If left idle the game auto-plays an opening movie: white-cloud fade →
  key-art crop → Anri cinematic → Falchion-on-stone. Any button skips.
- No menu strings resident: `Start a new game.` ABSENT pre-menu
  (`at-*.ram`, `mw-r0.ram`); `Main Menu` at 0x020CFB3C is static rodata,
  present even on title — it is NOT a menu-state signature.

Signature (content-scan): menu bank strings ABSENT + `020E6048`-area menu
struct zeroed (observed 0xAC/172 pre-menu vs 0xF8/0x70 live). Weak alone;
pair with "no map" (`gMapStateManager` NULL, already in `fe_ready`'s
negative path) and OCR of `FIRE EMBLEM` if needed.
Ready gate: title is a valid reader state (speak "Title. Press Start.").

## 2. Main menu

Banner (graphic, both LCDs): `Main Menu`. Bottom LCD shows the banner +
the active row's description only — the row list itself is NOT legible in
256×192 captures (4× upscale verified: background art only). Top LCD shows
banner + same description.

Signature strings (all resident once the menu is up; 2 same-path boots,
identical addresses):
`023CAD50 Start a new game.` `023CAD62 Continue from save data.`
`023CAD7B Resume from a suspend point.` `023CAD98 Copy save data.`
`023CADA8 Delete save data.` `023CADBA See more choices.`
`023CADCC Open the wireless-play menu.` (+ `023CADE9` WFC,
`023CAE0E` delete-all, `023CAE27` rewatch, `023CAE45` soundroom…)
Scan method: ASCII content-scan anywhere in `02000000–02400000`.

Menu descriptor table (structural signature, 3 same-path boots identical):
11 records at boot-observed `02253560`, stride `0x10`:
`{+0 desc_ptr, +4 heap link, +8 hash, +12 id-name ptr}` in this order:
Delete-all, WFC-setup, wireless-menu, rewatch, soundroom, Continue, Copy,
Delete, See-more, New-Game, Resume. Id names are `MEMH_/MFMH_` enum tags
(`MFMH_NEWGAME` etc. at `023CC1D0–023CC25C`), NOT display text.
Scan method: pointer-run scan — ≥8 consecutive u32s spaced 0x10 apart,
each pointing into `023CAD00–023CB000`. Do not hardcode `02253560`.

Selection signal: INPUT-ECHO (no RAM cursor exists — evidence):
- Approach 1 (ordinal diff, DOWN walk): snaps landed pre-menu (menu fade
  timing drifts boot to boot); invalid, discarded.
- Approach 2 (ordinal diff, late walk): boot reached an UNEXPLAINED
  Cavalier class-guide state at f2700 with no A presses in the plan
  (shots + `023CB2D1`/`020E3DAC` prove it). Cause unknown — possibly a
  mis-timed START + attract interaction. Docs the rule: every snap must
  be screenshot-gated; open-loop frame plans are not state proofs.
- Approach 3 (structural): the descriptor table is fully static across
  menu-vs-difficulty states (16 B records byte-identical) — no active-row
  flag inside it. No small-ordinal stepper anywhere outside the `020E`
  display buffer.
Input-echo spec: entry anchor = row of `Start a new game.` (fresh boot,
no save: the only actionable row; Continue/Resume greyed). DOWN/UP: no
verified multi-row walk exists — treat rows as unconfirmed; wrap behavior
UNKNOWN, exit/re-entry reset UNPROVEN. Next work: screenshot-gated
single DOWN + A(Continue-without-save) error-path probe.

## 3. Difficulty (`Select a Difficulty`)

The fully proven screen. Fresh boot: START → A (New Game) → A (confirm).
Options are Normal/Hard navigated with LEFT/RIGHT (2 columns, no rows).

Signature: banner graphic `Select a Difficulty` + description bank
`023CAF4A Recommended for beginners…` (Normal),
`023CAFCB Recommended for those seeking a challenge.` +
`023CAFF6 Enemies are up to five degrees tougher than` +
`023CB022 those in Normal mode. No prologue is included.` (Hard).
Description shown = selection (OCR/fuzzy-match per `fe/text-reading.md`).

Selection signal — RAM cursor, PROVEN (2 boots: `s*` + `dw-*`):
- `020E6049`: byte, 1 = Normal, 0 = Hard. Passes flip-flop
  (Normal→Hard→Normal: 1→0→1) AND 10-frame no-press control resample.
  Address identical across both boots. Sibling byte `020E6048` also flips
  (0xF8/0x4D vs 0x70/0x4D — larger enum, record for context, not signal).
- Display tile buffer `020E7E00–020E8000`: glyph-ID runs flip with the
  description (`…0026 0028 002A…` vs `…00CE 00D0…`); md5-distinguishable
  per selection. Secondary/content signal; same 2-boot stability.
- NOTE: `020E6049` retains its last value into later states (observed 00
  through Prologue narration) — the reader MUST gate it on "difficulty
  screen active" (banner/desc-bank + no-prologue-yet predicates), never
  read it bare.
Scan method for the struct: anchor on the `020E7E00` tile region flip or
search the `F8 01 4D 00 10 34` neighborhood pattern; re-verify per path.

## 4. Hard submenu (H1–H5 stars) — BLOCKED (proven absent on fresh boot)

Three distinct entry approaches, all screenshot-verified:
1. Repo `fe/plans/hardmenu.txt` (A once after Hard): stayed on
   `Select a Difficulty`/Hard description.
2. Scratch `hsub` (A once + RIGHT walk): same, description unchanged;
   only heap-noise flip-flops (`0219xxxx`, incoherent).
3. Scratch `hsubretry` (A ×3 spaced + DOWN walk): 1st A = no-op
   (tile-hash identical pre/post), 2nd A confirmed Hard and started the
   Prologue narration (`Prologue` banner, `Long ago, Medeus, king of the
   dragonkin, conquered the continent of Archanea,` with typewriter
   scroll). DOWNs during narration: no state change.
Verdict: pressing A on Hard STARTS THE GAME. No H1–H5 star screen exists
on the fresh-boot path (stars likely unlock-gated). `Choose an enemy
level.` (`023D2684`) / `(More stars means stronger foes.)` (`023D269B`)
cluster with wireless/setup strings (`023D2600` tutorials, `023D2642`
fog-of-war, `023D26EA` unit list) — suspected multiplayer/setup home,
NOT new-game difficulty. Recorded as open; do not map star rows until a
screen showing them is captured.

## 5. Main menu with a SAVE — the grid is a 2-COLUMN GRID, and the old plan chose New Game

**Unblocked 2026-10-08: two FE11 USA saves exist** (`~/fe/saves/fe11-usa-ch10.sav`,
`fe11-usa-finalboss.sav`, both `YFEE` at +0x0, 262144 bytes, ~49-67% non-0xFF so neither is wiped).
`fedump` and `fe_access` both take one via `SAVE=<path>`; the iOS app already passes a save
automatically (`ROMStore.savePath` -> `poke_load_rom`), so this is a real user path, not just a
harness trick.

⛔ **THE MAIN MENU IS A 2-COLUMN GRID, NOT THE VERTICAL LIST THE READER MODELS.** Measured from a
4x-upscaled capture of the TOP screen (`grid0.png`) with the save loaded:

    left column   : Continue / Suspend Point / New Game     <- cursor STARTS on New Game
    right column  : Copy Data / Erase Data / Extras

Three consequences, each measured:

1. **DOWN is inert.** After a DOWN the bottom description bar still read `Start a new game.`
2. **UP reaches Continue.** After one UP the description bar OCRs as `[.]Continue from save data`
   (`scripts/fe-ocr.sh` on the bottom screen).
3. **`save-continue.txt` was selecting New Game.** It pressed A five times from f1200 with no
   direction, so screenshots of that run show the **Prologue** narration ("A young man hailing
   from the Altea region appeared with a divine blade in hand.") -- the save was never used. The
   plan now presses UP first.

⛔ **AND THE TIMING WAS WRONG.** The menu is not up early: shots at f1100-f2200 read
`TOUCH TO START` (the title). `START` at f1600 then `A` at f2400 is the measured arrival.

### ⛔ THE SAVE ITSELF DECIDES WHETHER CONTINUE WORKS (measured 2026-10-08)

**Continue was never broken.** Two things hid the answer.

1. **`fe11-usa-ch10.sav` is not loadable by this build.** Continue with it returns to the main menu
   and its Suspend Point row does nothing; a dense 200-frame capture after the A press shows the
   menu re-rendering, never a slot screen. The other save works. So every earlier "Continue does
   not advance" run was blaming the button for a save the game itself refuses.
2. **"A does nothing on Continue" was a misread.** A control run -- A on New Game, the cursor's
   *starting* row -- advanced to the difficulty screen and changed 44.91% of pixels. A confirms on
   this menu. Continue needs the UP first only because the cursor starts on New Game.

What the save path actually is, measured with `fe11-usa-finalboss.sav`:

    Continue -> Chapter Saves  (slot list: Endgame / Epilogue / NO DATA, PLAY TIME footer)
             -> a slot with map savepoints leads to a Map Savepoints list
             -> the chapter loads: gMapStateManager valid, 15 units, Marth under the cursor

### The file-select screen is now READ (stage byte 02)

`Core/fe_access.cpp` gains `FeFileSelectActive()`, gated on `A_FE_STAGE` = `0x020E3CA8`, measured
over **two boots** with a save loaded:

    00 = title / main menu       01 = maps and the difficulty screen
    02 = Chapter Saves (save file-select)

The main menu with the **same** save loaded still reads 00, so stage 02 is *the screen*, not "a save
exists". The screen's own strings (`Chapter Saves`, the slot labels) are static rodata resident on
every screen and are **not** usable as anchors; the reader says that rather than pretending. The
highlighted slot has **no located cursor** -- the candidate table at `0x0224F540` is byte-identical
on the main menu, and the `0x020E604A` byte that flips with an UP animates on its own (0x50/0x52/0x32
with no input between two snaps), so `MenuNav` names the screen and states the limit instead of
predicting a row.

Negative controls, both passing: the same save on the main menu reads `Main menu`; the loaded
chapter reads the map cursor (`Cursor 14, 24 ... Marth, 26 HP, unacted ... 33 squares reachable`).
`scripts/adapter-tests.sh`: 252 checks, 0 failures.

⛔ **HARNESS HAZARD: the run overwrites the save it was given.** `Core/pokecore.cpp` `NdsTick`
flushes the emulated SRAM back to `SAVE=` once a second. Copy a save to `~/fe/work/` before a run;
`~/fe/saves-backup/` holds the originals.

### How this was measured, and one method that failed

- **The BOTTOM description bar names the live row.** `scripts/fe-ocr.sh` on the bottom screen is the
  ground truth; the button labels themselves are low-contrast and OCR poorly.
- **Frame hashing beats reading.** `ds-diff.py` showed the TOP screen changing in a 2.72% region
  (x 76..178, y 89..102) per UP press while the BOTTOM stayed byte-identical -- that is the cursor
  moving, proved without reading a pixel.
- ⛔ **A DOWNSCALED VISION PASS ON A 256x192 SCREEN PRODUCES CONTRADICTORY ANSWERS.** The same image
  was read as "six-button grid, New Game highlighted" in one pass and "title screen TOUCH TO START"
  in the next. Use a 4x nearest-neighbour crop (`ds-crop.py`) or OCR; do not read the raw capture.

### Still open on this screen

- **Confirm IS observed to enter the chapter -- with the right save.** With
  `fe11-usa-finalboss.sav`: Continue (UP then A) -> Chapter Saves -> slot -> a map with 15 units.
  With `fe11-usa-ch10.sav` it never leaves the menu; that save is the variable, not the input.
  Touch works and is measured (`TOUCH <frame> <x> <y> <0|1>`); a tap both focuses and confirms when
  the panel is already focused.
- ✅ The reader now reads the grid as a grid. `FeFileSelectActive()` covers the Chapter Saves screen
  (stage 02); the main menu still speaks its anchor row ("Start a new game.") because the anchor row
  IS the live row on a fresh boot. Where a direction went is never predicted.

## 6. Battle Preparation / unit list — BLOCKED (unreachable without save)

- `fe/plans/save-continue.txt` header documents the chain: Continue →
  Battle Preparation (units in force, map NOT loaded, manager NULL) →
  FIGHT → map. None of it is reachable on fresh boot.
- Strings pre-identified for future scans: `023D2E2C Select units to
  send into…`, `023D30FD Begin the battle.`, `023D3195 Select units to
  deploy. (R Button: Unit List)`, `023D26EA Display a list of your
  units.`
- The new-game path reaches only Prologue narration → Prologue maps
  (the `skip-intro`/`units` plans cover map-phase units, which
  `Core/fe_access.cpp` already reads). Reaching Chapter-1 preps needs a
  full Prologue playthrough — out of budget. The map-phase R-button unit
  list screen is likewise uncaptured.

## 7. Ready-gate additions (proposed, NOT implemented)

`fe_ready()` today = map-cursor-ok only. Add, in order:
1. `TitleActive()`: `gMapStateManager` NULL AND menu bank absent AND
   (`020E6048`-struct zeroed OR title OCR hit).
2. `MainMenuActive()`: `Start a new game.` content-scan hit AND `020E6049`
   struct live (`020E6048` nonzero) AND manager NULL.
3. `DifficultyActive()`: §2 predicates AND difficulty desc-bank resident
   (`023CAF4A` + `023CAFCB`) — then `020E6049` is readable.
`fe_ready() = TitleActive() || MainMenuActive() || DifficultyActive() ||
ReadCursor().ok`. Prologue narration (`Prologue` banner graphics +
`023C93FE`-cluster narration bank) SHOULD join once its advance/skip
inputs are proven — currently unproven, keep out.

## 8. Command mapping (speech strings)

| State | WhereAmI | MenuNext/Prev | MenuState |
|---|---|---|---|
| Title | "Title screen. Press Start." | n/a (no rows) | "Waiting to start" |
| Main menu | "Main menu. {description}" (description = echo-tracked row: fresh boot: "Start a new game.") | move echo cursor; speak new row's description | "Row {i} of {n}: {description}" (n unconfirmed — speak description only until wrap is proven) |
| Difficulty | "Choose difficulty. Normal: prologue tutorial. Hard: tougher enemies, no prologue." | Normal↔Hard; speak active description | "Difficulty: Normal/Hard" (from `020E6049` gated, OCR fallback) |
| Prologue narration | "Story narration. Press A to continue." | A advances page | "Narration page" (no page count yet) |
| File select / preps / unit list | BLOCKED — speak "Not on a map yet" (current behavior; do not invent rows) | — | — |

B/A glyphs extract as blanks in OCR (see `fe/text-reading.md`); substitute
button names in the reader. Stylized card/label fonts are OCR-weak — the
description box is the reliable text source on every menu screen.

## Appendix — runs, dumps, shots

Tool: scratch `~/fedump-fe` (see harness note). ROM:
`~/roms/Fire Emblem - Shadow Dragon (USA).nds`. Scratch plans (NOT in
repo): `~/drow2.txt`, `~/hsub.txt`, `~/mrow.txt`, `~/mrowlate.txt`,
`~/hsubretry.txt`, `~/attract.txt`; scripts `~/ramscan.py`,
`~/cursorhunt.py`, `~/ptrscan.py`, `~/ppm2png.py`, `~/upscale.py`.
Repo plans used: `mainmenu`, `menudiff`, `hardmenu` (re-run at HEAD).

| Run | Dumps | Shots (top+bottom PNGs in scratch) |
|---|---|---|
| mainmenu ×2 boots | `mm0/1/2.ram` | `fe-mm*.png` (menu @ f950: Main Menu + Start-a-new-game) |
| menudiff | `s0/1/2.ram` | `fe-s2*.png` (Normal desc after flip-flop) |
| hardmenu | `h0/1.ram` | `fe-h1*.png` (stayed on difficulty/Hard) |
| drow2 (control) | `dw-mm/d0/d0b/d1/d1b/d2/d2b.ram` | `fe-dw-d{0,1,2}.png` (Normal→Hard→Normal, visual proof) |
| hsub | `hs-diff/hard/s0/s0b/s1/s2/s3.ram` | `fe-hs-s{0..3}.png` (never left difficulty) |
| mrow (early, invalid) | `mr-r0..r4.ram` | pre-menu title art — discarded for cursor |
| mrowlate (anomaly) | `mw-r0..r4.ram` | title → unexplained Cavalier class guide @ f2700 |
| attract (no input) | `at-1000/2000/3000/4200.ram` | opening movie frames (clouds/key-art/Anri/Falchion) |
| hsubretry | `hr-hard/a1/a2/a3(+b)/w1/w2/w3.ram` | difficulty/Hard → Prologue narration |

Windows scratch (this machine): `%LOCALAPPDATA%/hermes/cache/scratch/fe-*.png`,
`ramscan.py`, `cursorhunt.py`, `ppm2png.py`, `upscale.py`, `fe-*.txt` plans,
`fe-psp-stub.cpp`.
