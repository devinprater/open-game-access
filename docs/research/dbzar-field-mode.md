# DBZ: Shin Budokai — Another Road — the STORY MODE reader (field mode)

The story mode in this game is **not a menu**. It is a real-time field: the player flies a
wide landscape and has to stop villains from destroying the **cities** on it. This document
is the map of that mode, measured, and it is where the adapter's addresses come from.

Read `dbzar-story-status.md` first for the message/cutscene work — this file is about the
part the player actually plays.

## ⛔ Read this before trusting ANY address in the decompile

**`EBOOT.dec` is RELOCATABLE.** The file carries `.rel.text` (57,072 bytes of entries) and
the loader patches `lui`/`addiu` pairs in place on load. So:

- `addiu r18, r18, 0x1780` in the static file does **not** mean the array is at `0x08805780`.
- The real field-mode arrays sit **~2 MiB higher**: `0x08A852D0`, not `0x08805780`.
- Read the LIVE instruction (`scripts/dbzar-live-addresses.mjs`) — do not do the arithmetic
  from the file. The static value is the relocation's *addend*, not the address.

This is the same family of trap as the `iRam`/`cRam` one recorded for this title, with the
opposite cause: those were Ghidra artefacts, and this one is real loader behaviour.

## The verified layout

    ENTITY(i) = 0x08A852D0 + i * 0xF0          i = 0..0x24  (37 slots)
        +0x00 float x   +0x04 float y   +0x08 float z
        +0x30 i32 team     0/1 = player side, 2/3 = enemy side, -1 = slot unused
        +0xD1 byte visible-this-frame (the city module sets it from a 50.0 check)

    CITY(i)   = 0x08A876B0 + i * 0x70          i = 0..4   (5 cities; the module walks 5)
        +0x00 u16  city id        0 = empty slot
        +0x10 float x      +0x18 float z      (the proximity test reads 0x10 and 0x18)
        +0x20 i32  current health
        +0x24 i32  max health      percent = +0x20 / +0x24
        +0x28 float radius         its square is the proximity bound
        +0x34 i32  render/task handle array (3 entries), -1 when none

    AR MODE FLAG  = 0x089B51D4   (EBOOT .data — NOT relocated; verified live: 0 = no, 1 = yes)
    CHAPTER INDEX = 0x089B51D5
    CHAPTER TABLE = 0x089B51D8   u32 per chapter (7 entries, then 0)

Both accessors were disassembled **live** to confirm them:

    FUN_000172fc(i) { return i * 0xF0 + <0x08A852D0>; }     entity
    FUN_000184c8(i) { return i * 0x70 + <0x08A876B0>; }     city

## ⭐ The damage bands are the game's OWN, not invented

`FUN_0001a90c` computes

    ratio = [city + 0x20] / [city + 0x24]

and swaps up to **three task handles** as that ratio crosses thresholds. The values in the
comparisons are 0.8, 0.5 and 0.3. So the game's own damage vocabulary is:

| ratio | band |
|---|---|
| ≥ 0.8 | healthy |
| < 0.8 | damaged |
| < 0.5 | hurt |
| < 0.3 | critical |

The reader announces a crossing of exactly those edges, which is why a player never hears a
number the game itself does not act on.

## Where the other numbers come from

| quantity | source | note |
|---|---|---|
| city health % | `[+0x20] / [+0x24]` | the game's own arithmetic |
| cities alive | count of `id != 0` over the 5 slots | a fresh save had all five at 0 |
| enemies | count of `team >= 2` over 37 slots | `team == -1` is an unused slot |
| player position | first entity whose `team` is 0 or 1 | ⛔ which of 0/1 is 1P is NOT pinned |
| chapter | byte at `0x089B51D5` | a live value only while in Another Road |

## ⛔ Not solved, and here is exactly what that means

- **Which enemy is attacking which city.** `FUN_0001964c(city, side)` returns a boolean from a
  proximity test over entities 0..2 (player side) or 3..0x24 (enemy side), and writes a
  repair/damage amount. It never stores *which* entity was in range. Naming the attacker
  needs a new instrument, not a different address.
- **The mission text and its completion state.** The mission table at `0x089B3BD3` is a run of
  one-byte ids into a second table, and the mission *strings* are in the message archives
  (`MSG_AR_MISSION_*`, 5 of them). Neither the active mission nor its pass/fail is pinned.
- **Senzu Beans** (how many times an enemy can still be beaten). The message ids exist
  (`MSG_AR_FRIEND_SENZU`); the counters are not located.
- **A field-mode audio beacon.** In battle the project has a lock-on cue contract; field mode
  has no equivalent agreed, so the adapter ships no cue rather than inventing one.

## ⭐ THE FIELD IS REACHABLE — the route, and what it cost to find

Chapter 1's route, measured by pressing with a RAM dump after each step:

    cutscenes  ->  CHARACTER SELECT  ->  TEAM EDIT (the booster screen)  ->  the field

⛔ **THE ENTITY ARRAY IS A USELESS ORACLE ON THE WAY IN.** It reads `team != -1` on all 37
slots from the moment Another Road starts, at every screen, so "placed entities" stays 0 until
a stage is actually loaded and tells you nothing about progress. What works is the **resident
message ids**: the field's own families (`MSG_AR_FIELDPLAY_*`, 268 of them, and `MSG_AR_CITY_*`,
24) are loaded when the field loads, so counting them tells you the field is up. A scene
cutscene shows only `MSG_AR_<chap>_<scene>_<line>` ids.

The screen after Team Edit is the field: a city-name banner ("South Village") with its gauge,
a small flying character, and a portrait down the left side.

## ⭐ WHAT THE FIELD LOOKS LIKE IN RAM — measured, not decompiled

A full 24 MiB dump taken with the field live, Chapter 1:

    3 cities in play, ALL with id 0x0000:
        city[0] id=0x0000  cur=1500 max=1500  pos=(-663, -809)    r=500.0
        city[1] id=0x0000  cur=1500 max=1500  pos=(-512, -2840)   r=500.0
        city[2] id=0x0000  cur=1500 max=1500  pos=(1700, -600)    r=500.0
    2 empty slots, id 0xFFFF, cur 0, max 0, r 0:
        city[3] id=0xffff  cur=0    max=0     pos=(0, 0)          r=0.0
        city[4] id=0xffff  cur=0    max=0     pos=(0, 0)          r=0.0

    2 live entities:
        ent[0] team=0  pos=(2383, 497, 141)   (+0xD1 = 1)
        ent[3] team=1  pos=( 804, 497, 623)   (+0xD1 = 1)

⛔ **A POPULATED CITY HAS id 0, AND AN EMPTY SLOT HAS id 0xFFFF.** This is the opposite of the
guess the adapter shipped with (`id != 0` = present), which read every real city as absent and
every empty slot as present. It was found only by reaching the field and reading it: no amount
of decompiling shows it, because the id is *data*, and the code that writes it is in the field
loader. Presence is now **capacity** (`max > 0`), which is also what the percentage is computed
from — one read, used for both.

Also confirmed live: a city's health and max are EQUAL at full (1500/1500), and the health scale
is per-stage (1500 here), so the percentage must always be the ratio, never a divisor baked in.

**Still not matched:** the field HUD's own gauge shows a BAR, not a percentage, so the ratio
cannot be checked against a printed number on this screen. The percentage the reader speaks is
the game's own arithmetic (`+0x20 / +0x24`) and is now read from a real stage, but the HUD gives
it nothing to be compared against. Where a percentage IS printed — the Chapter Select screen's
"City DF." — is the place to close that.

### The digits, identified by SHAPE rather than by a description

The damaged row's number was settled from its glyph BITMAP, which is the only way here: the
glyphs are about 20x40 px and a vision model has already been wrong twice on this HUD.

    glyph 1: a closed loop with a tail descending and curving left to a flat foot   ->  9
    glyph 2: a curve at the top, a diagonal down-left, a full horizontal base       ->  2
    (the full-health row's known '0' is a plain symmetrical oval -- neither)

So the row prints **92%**, which also agrees with what the vision read said. The reader's ratio at
that moment was 90.0% and the bar measured 90.5%.

⛔ **THE RESIDUAL GAP IS ALMOST CERTAINLY THE CAPTURE ORDER, AND THAT IS A REAL INSTRUMENT
FAULT.** These instruments grab the WINDOW first and read RAM afterwards. On a city that is
actively taking damage the two describe different instants, and the RAM value would read LOWER than
the printed value -- which is the direction observed (90.0 against 92). The project's standing rule
is the opposite order ("free the RAM read FIRST, then grab the window"), and `dbzar-pair.mjs`
already does it that way; the chase/panel instruments do not. So the honest reading is:

    the printed number and the reader's ratio agree to within the damage that occurred between
    the two reads

and an exact same-instant comparison needs (a) RAM first, then the frame, and (b) a city that is
not actively being hit, so neither read can drift. Both conditions are available; the capture that
has them has not been taken.

### ⭐ THE PANEL AND A DAMAGED CITY DID CO-OCCUR — and the mid-scale comparison is a MATCH

The earlier conclusion that the two never appear together was WRONG, and the reason is useful: the
panel is drawn when a CITY IS IN VIEW. Chasing the enemy produces frames with the camera on empty
sky, which draw no panel; a frame captured while the enemy's attack landed showed both.

Measured, one frame, RAM and screen read in the same process:

    city 0:  cur=1350  max=1500  ->  90.0%          (the reader's ratio)
    its gauge bar on screen:  287 px of a 317 px full bar  ->  90.5%   (measured in code)

Two points now exist on the scale, both measured in pixels rather than described:

    FULL      bar 317/317 px  = 100.0%      RAM 1500/1500 = 100.0%
    DAMAGED   bar 287/317 px  =  90.5%      RAM 1350/1500 =  90.0%

⛔ **WHAT IS STILL NOT SETTLED: the printed DIGITS.** A vision read of the same row returned "92%",
while the bar measures 90.5% and the reader's ratio is 90.0%. The digits are about 8 px tall, and
the vision model has already been wrong on this HUD twice in this session, so neither the "92" nor
a claim that it says "90" should be published. The BAR is the reliable printed quantity here and it
agrees with the reader to within half a percentage point. Settling the digits needs the glyph
compared programmatically against a known digit from the same frame -- the tool is written
(`dbz-glyph.py`) and the boxes are located (row 2's "100%" supplies a known 1 and 0), but the
comparison has not been completed.

⛔ **The panel is view-dependent, and that is why the hunt kept failing.** Four separate watchers
recorded zero panel frames while cities were visibly damaged, because the player was parked on the
city looking at empty sky. Any future capture of this panel must first get a city on screen.

### The printed panel and a DAMAGED city have not been observed together

Hunted across every capture in one session -- 40 frames from a passive recorder, 31 from a drive,
26 from a watcher, plus the point captures. The result is consistent and worth stating plainly:

    EVERY frame that draws the printed city panel shows 100% / 100% / 100%
      (2 frames: the field's opening ALERT presentation)
    EVERY frame that shows a city below full health draws the BOSS-FIGHT layout instead
      (Dabura's name, a countdown timer, his own health bar -- no city list at all)

So the two states may not CO-OCCUR. The panel is the mission's early ALERT presentation; once the
enemy's damage lands, the presentation is a boss fight. If that holds, the printed percentage
cannot be compared at mid-scale on this mission, and the honest statement is:

    the reader's ratio is CONFIRMED against the game's own printed number at FULL health only

The percentage the reader speaks is still the game's own arithmetic (`+0x20 / +0x24`, the same
ratio the game's own band ladder compares in `FUN_0001a90c`), and both sides now read from a real
stage. The missing piece is only a printed mid-scale value to match against.

**Two instruments were fixed along the way and both are recorded because they produced false
readings:** a first movement probe held every control and reported that NONE moved the player,
while a full dump comparison showed `up` moves `ent[0]` -- the probe was wrong, not the game; and
the flying watcher steered by pressing left/right only, when `up` is the control that actually
advances the player, so nine minutes of "flying" never moved it and the distance stayed at 1651.
A watcher that does not move the player cannot damage a city, and its flat result says nothing
about the game.

## ⛔ THE READER TALKED OVER A BATTLE — the liveness gate, and its limit

Driving the game for this comparison found a defect that no decompile would have shown and that
the host tests could not: **`AR_MODE` stays 1 for the whole of Another Road.**

Measured in one session, with the field's city array left behind by a finished mission:

    cities read 52% / 100% / 100%   -- continuously, unchanged, through ALL of:
      a boss battle (Dabura), a battle pause menu, a "CONTINUE? Yes/No" prompt, a "Time Up"
      screen, and a fresh main-menu boot, with AR_MODE = 1 the whole time

So the reader would have SPOKEN a dead mission's city health during a battle, with no refusal.
That is worse than silence: a plausible lie the player has no way to catch.

**The fix is a second, stronger condition.** `0x089B03E4` is a POINTER (measured `0x09AF1760`)
while the field's own task objects are resident, and reads 0 once a battle has torn the field
down. Measured: `0x09AF1760` in the live field AND on the mission's own Time Up frame; `0` on
every battle, pause menu, main menu and fresh boot. Every spoken field answer, the per-frame
damage watcher, the cue snapshot and `Ready()` now require BOTH conditions.

⛔ **SCOPE OF THAT EVIDENCE, stated because the gate is narrower than it looks:** it answers "is
the field module alive", NOT "is the mission still running". It does not refuse on the Time Up
frame, because the module is still resident there. It refuses the battle and menu screens, which
is the failure that was measured. A stronger gate would need a mission-state word; that is
recorded as open rather than claimed.

**Why the tests did not catch it:** every existing case set `AR_MODE = 1` and expected speech --
which is exactly the mid-battle situation the reader must now refuse. Fixing the gate made 39
checks fail, and that is the suite working. The fixture now establishes both conditions, a new
case pins the refusal (mode set, field pointer clear), and a mutation that drops `FIELD_LIVE`
from `Ready()` is detected. The first pass of that mutation SURVIVED at 55/55, because the
existing "story mode left" case only cleared `AR_MODE`, which both gate forms answer -- a second
case was added specifically because one case could not distinguish them.

## ⭐ THE PRINTED-VS-READ COMPARISON — matched, and what it does and does not prove

The field's own HUD **does print percentages**, in a panel down the LEFT side, under an
"ALERT!!" banner. A `dbzar-pair.mjs` capture (RAM and window in ONE process, so both describe
the same frame) shows, at Chapter 1:

    HUD PRINTS (magnified crop of the top-left panel)      ADAPTER READS (same frame)
      South Village   100%                                   city[0] 1500/1500  -> 100%
      City A          100%                                   city[1] 1500/1500  -> 100%
      City B          100%                                   city[2] 1500/1500  -> 100%
      (two more rows exist where cities are absent)          city[3] id=0xffff  -> not listed
                                                             city[4] id=0xffff  -> not listed

Three printed entries, three cities read, three percentages agreeing, and the two empty slots
correctly excluded from both. **The reader's number is the game's own number.**

⛔ **WHAT THIS DOES NOT PROVE, and it matters: only the TOP of the scale is confirmed.** Every
city read 1500/1500, so "100% = 100%" is consistent both with the ratio being correct AND with a
reader that reports a constant 100 whatever happens. The falsifying case is a city that is
DAMAGED — a bar that falls with the ratio. Until that is seen, the scale is matched at one point
and nowhere else. `dbzar-waitdrop.mjs` is the instrument for it (wait for `cur < max`, capture
both); it needs an enemy to actually attack a city, which did not happen in the window watched.

⛔ **AND THE VISION MODEL GOT THIS QUESTION WRONG TWICE, IN THE SAME SESSION.** Asked to look
at the top-left corner for percentages it answered "no numbers in the image" — twice — while
three printed percentages sat there, and it named unrelated games for the same frames. It only
transcribed them when handed a **magnified crop**. The rule already in the skill is the one that
applies: trust its transcribed strings and its counts, settle geometry by measuring pixels in
code, and magnify a region before believing a negative about what is in it.

### Where the gauge actually is on this screen (measured by colour, not by eye)

    city-name banner (red-outlined box):  x 1088..1482, rows 308..382
    the city GAUGE below it:              x 1088..1448, rows ~389..418
    the left-side list of city + percent: the panel under the "ALERT!!" banner

## The radar: controls and the cue

### The target ring (player's decisions, 2026-10-08)

One ring over all three things on the map, in the order the stage makes you care about them:

    1. ENEMIES  -- nearest first
    2. CITIES   -- worst health first
    3. ALLIES   -- nearest first

Stepping past the end of one kind walks into the next, so it is ONE ring rather than three
lists. A kind with no members is SKIPPED, not stopped on: an empty kind must not be a dead end.

| control | command | what it does here |
|---|---|---|
| next / previous item | `MenuNext` / `MenuPrev` | step the ring, then read the new target |
| read item | `NextUnactedAlly` | re-read the current target |
| next enemy | `NextEnemy` | the whole city list, worst first |
| previous enemy | `PrevEnemy` | the chapter / city / enemy summary |

**Nothing new was appended to the command enum.** The existing next/previous/read commands
already mean "walk a list and read what you land on" on every other adapter, so the field ring
rides them and the C ABI is untouched. That is deliberate: an append costs a bound widening and
a Swift enum case in the same commit, and it buys nothing here.

### The cue: BECAUSE NOTHING AIMS YOU, IT MUST CARRY A BEARING

This is the one place the project's "a lock-on cue is CENTRED, not panned" rule is suspended,
and the reason is mechanical rather than a matter of taste. That rule exists because in
Dissidia the game turns the player toward the lock, so panning the cue would ask them to steer
something already being steered. **Field mode steers nothing.** The player flies, so the cue's
whole job is to say which way.

| carried in | what it encodes |
|---|---|
| **pan** | the bearing, full 360 degrees: +90 degrees (right) is hard right |
| **pulse rate** | distance, with a floor and a ceiling |
| **rate again, coarser** | front/back — the pulse slows to about a third when the target is behind |
| **timbre** | identity: enemy 520 Hz, city 780 Hz, ally 1120 Hz |

Enemy / city / ally get three different tones so the thing you must kill never sounds like the
thing you must protect at the same pulse rate.

⛔ **THE PAN NEEDED A RENDERER FIX.** The cue renderer wrote the same sample to both channels —
it was mono — so a voice's `pan` did nothing at all. It now applies equal-power panning
(`cos`/`sin` on the pan angle) rather than a linear crossfade, because a linear pan dips about
3 dB in the middle and a target dead ahead would then sound *quieter* than one to the side and
read as farther away.

### The bearing problem, and how it is solved without the game's help

**The game stores no facing.** Nothing in the field layout says which way the character points,
and the camera is not trustworthy. So the adapter measures a heading from its own recent
positions: `heading = atan2(dx, -dz)`, i.e. the direction of TRAVEL, in the same convention as
the map's own axes (x east, z south).

Three consequences, all deliberate:

- The clock direction is relative to the **direction the player is moving**, so it stays true
  no matter where the map's camera happens to be. "The city is at 2 o'clock" means "turn until
  your nose points 2 o'clock".
- At rest the position jitters by fractions of a unit, and a heading taken from jitter is a
  random direction delivered with confidence. So a heading needs **1.5 world units of movement**
  before it counts.
- A heading older than **2.5 seconds** is reported as NOT live (`heading_live = false`). The
  spoken answer says "Move to refresh direction", and the cue drops its pan to centre: it keeps
  telling you the distance but refuses to tell you a direction you have already left. Silence
  about direction beats a confident lie about it.

### What the cue does with no selection

It falls back to the **nearest enemy** (the player's instruction: the radar should keep pointing
at the thing that ends the stage). With the ring's kinds ordered nearest-first, that fallback and
the ring's own default selection are the SAME answer rather than two behaviours that can
disagree. The fallback's real reachable case is the ring sitting on a kind that goes empty --
all cities destroyed while enemies fly on -- and it is tested by exactly that.

## ⛔ WHAT IS VERIFIED AND WHAT IS NOT — read this before trusting the reader

Grades of evidence, stated separately because they are not the same claim:

| claim | grade |
|---|---|
| the mode flag goes 0 -> 1 on entering Another Road | **observed live** |
| the two accessors compute `0x08A852D0` / `0x08A876B0` | **read from LIVE `.text`** (disassembled in place) |
| the field layout (strides, offsets, team values) | **decompiled**, and consistent with those live accessors |
| the city module walks exactly 5 cities and 37 entities | **decompiled**, with the loop bounds in the live code |
| the damage bands are 0.8 / 0.5 / 0.3 | **decompiled**, from the three ratio comparisons |
| the three cities in play read 1500/1500 with radius 500, ids 0x0000 | **OBSERVED LIVE** (full dump, field loaded) |
| 2 empty slots read id 0xFFFF, max 0 | **OBSERVED LIVE** |
| entities carry real world positions in a loaded stage | **OBSERVED LIVE** (team 0 and 1, y=497 both) |
| ⭐ **`[+0x20]/[+0x24]` equals the percentage the HUD PRINTS** | **MATCHED at full health, first time (2026-10-08)** — see below |
| ⭐ the ratio tracks a city that is DAMAGED | **OBSERVED (2026-10-09)** — bar 287/317 px = 90.5% against a RAM ratio of 90.0% |
| ⭐ a printed DIGIT at mid-scale | **SETTLED: the damaged row prints 92%** — confirmed by glyph SHAPE, not by a description (see below) |
| ⛔ the printed number, the bar, and the reader's ratio at ONE instant | **WITHIN 2 POINTS, not exact** — 92% printed, 90.5% bar, 90.0% RAM, with the capture ORDER as the likely cause |

That last row is the open link. A populated city record (an id present with a non-zero max)
was never reached with the field on screen, so the percentage arithmetic is *read from the
game's own code* but not yet *seen matching its own HUD*. Every other row above rests on a
read that was actually taken.

**What would close it:** enter a stage past the cutscene, dump `0x08A876B0 + i*0x70` for each
populated city, and read the on-screen "City DF." percentages at the same instant. Then the
pair is matched like the message-text work was matched, and the reader stops being a
decompile-derived claim.

⛔ And a driving note for whoever continues: the Chapter 1 cutscene chain is long
(`MSG_AR_000_02_*` -> `001_00_*` -> `001_01_*`, confirmed by the resident message ids in RAM),
and this session did not get past it to the field. The dump-and-match is the reliable way to
know WHERE the game is; a vision read of the screen was wrong six times in a row here.

## Why the field-mode arrays read all-zero outside a stage

At the title/menu the two arrays are still allocated and still hold their **static** contents
(a jump/access table, and one city id `0x0035`), so `team` reads -1 across the board and the
city ids read 0. That is the honest "not in a stage" picture, and the adapter's mode gate
(`AR MODE FLAG == 1`) is what keeps it from speaking any of it.

## Tooling (in `scripts/`, host-side, read-only)

| script | purpose |
|---|---|
| `dbzar-live-addresses.mjs` | scan LIVE `.text` for `lui`/`addiu` pairs → the RUNTIME address map |
| `dbzar-adapter-test.sh` | the adapter's host test (synthetic RAM, 28 checks) |
