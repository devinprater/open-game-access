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
| ⛔ **`[+0x20]/[+0x24]` equals the city percentage ON SCREEN** | **NOT OBSERVED** |

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
