# Navigation and counted guidance — design notes from prior art

**Source of the ideas:** [buu420](https://github.com/buu420)'s *Foresight* (Chrono Trigger,
Windows/Steam) and the Digimon World 2 work in `beetle-psx-libretro`. Read for **technique**,
not code: none of it is copied, and none of it is applicable verbatim — Foresight is a C#
in-process hook on a different game, running in a different host. What transfers is the
*design*, and it is credited here because it is his.

**Why this matters for OGA.** We have the Chrono Trigger SNES RAM map
(`reverse-engineering/ctds/`) and a reader layer. What neither has is a **guidance** layer:
telling a blind player how to *get somewhere*. Foresight is the most developed example of that
in the wild, and several of its findings are things we would otherwise learn the hard way.

---

## 1. Counted navigation, not continuous steering

Foresight speaks **"left 3, then down 2"** — discrete cardinal legs with counts, revised at
each turn — rather than a stream of bearings. One step is one tile (256 fixed-point units
locally, 128 on the world map).

Three findings worth keeping:

- **Whole legs, not waypoints.** "Route progress follows whole cardinal legs and keeps an
  existing valid approach." Counting to a waypoint *inside* a leg restarts the instruction
  and the count; counting to the end of a leg does not.
- **Prefer fewer turns among equal-length paths.** Their first version produced "eight turns
  where one turn sufficed" — because the search only minimised *distance*. The fix is in §2.
- **Announce up to three legs up front, then only the new leg at each turn.** Saying the whole
  route is noise; saying nothing until the turn is too late.

⛔ **Their counting bug is our counting bug waiting to happen.** Footsteps were tracked at 384
units per beat while speech assumed 256 locally and 128 on the world map, so a six-step replay
produced four beats walking and three running. **Navigation and footstep counting must share
one unit constant per map type**, and the tracker should *throw* if guidance and movement
disagree rather than drift silently.

## 2. The pathfinder keeps HEADING in its search state

This is the single most transferable idea, and it is a two-line insight with real consequence.

Their search state is `(point, xDirection, yDirection)` — not just `point` — so it can track
**turns as a secondary cost**. The queue is ordered `(distance + estimate, turns, ...)`, and a
turn is charged when the heading into the node differs from the heading out of it.

Why it matters:

- Equal-length routes no longer pick arbitrarily; they pick the one with fewer turns.
- **A longer arrival at the same position cannot improve any continuation**, so the position
  budget is charged **once per position, not once per heading** — otherwise retaining heading
  multiplies the search space by nine for nothing.

Their bounds, all worth copying as *shapes*: goals capped at 64, neighbours per node capped at
16 (a local movement graph is bounded — if it is not, the graph is wrong), and a visited ceiling
(131,072 after measuring their own worst map: "reachable Mountain of Woe/Frozen Cliffs routes
exceeding 65,536 positions"). Exceeding any bound returns **`LimitReached`, which is spoken**:
*"The route search limit was reached. Try a closer destination."* A bounded search that fails
silently would be a hang.

## 3. A destination has APPROACH POINTS, not one position

`NavigationTarget` carries a **list** of approach points, and the search is run against all of
them. Their reasons, each of which we would hit:

- A wide exit exposes several cells; **the chosen approach stays valid when the set changes or
  its order changes**, so do not re-plan (and re-speak) because an alternative appeared.
- A goal **proven not to reach** its target during this guidance is excluded — and that memory is
  **cleared when the destination moves**, "because rejected standing points belong to the
  actor's previous position, not to a moving chase for its whole lifetime."

## 4. Arrival is a THREE-state verdict, and "in range" is not one of them

The most important correctness idea here, and it generalises straight to Dissidia and FE11.

- **`ConfirmFacings(point)`** — "standing in range is not enough; the native handler only tests
  the actor on the side the player faces." For a destination the game activates with Confirm,
  this returns *which facings* actually work, evaluated **on the live frame**, so a moved actor
  is judged where it is now.
- **`ConfirmPending(point)`** — true where the destination is *geometrically* in reach but the
  game would not give it Confirm **right now**: its native scan gate is off, another touched
  actor overrides, or the native state is unreadable. **"Such a point is neither ready nor
  unreachable: wait there."**

That third state is the one our readers do not have. Our adapters are largely ready/not-ready;
the honest middle is *standing in the right place and waiting for the game to agree*.

- And a rule about moving contact: for an audited moving interaction, **only the player's own
  Confirm input is permitted** — "never synthesize the catch." That is OGA's read-only rule,
  independently arrived at.

## 5. Discovered visibility = the game's OWN test

They do not guess whether a target is visible. `IsDrawn` is "the same test the native engine
uses": the actor's flag is in `1..7F` **and** it is inside the camera window. They measured
that the hide opcode "clears +0xD0 and nothing else."

Consequence they hit and we would too: **a point test hides an exit whose centre sits exactly on
the camera window's exclusive bottom edge.** Their fix — exits and chests use **rectangle
overlap with positive area**, so touching an edge alone does not reveal a tile. Then a follow-up
bug: limiting approach goals to *visible* tiles "prevented routing to a known staircase's
reachable entry on row 14", which is why destination discovery and connected-entry geometry are
**separate** concerns.

## 6. Counted footsteps, with a fraction that survives a pause

`FootstepTracker` measures movement between **accepted input ticks** — "input requests alone
cannot produce sound, and discontinuities cannot accumulate footsteps." Details worth having:

- A **discontinuity guard of 16 rendered pixels** per tick, independent of the local/world step
  length: more than that is a jump, not walking.
- **"A stationary pause must not erase the fraction already walked."** The counter retains whole
  units *and* the remainder; a 2.5-step leg is two full beats and a partial.
- At a completed endpoint, **include the last full beat the old instruction promised** before
  starting the new count.
- Identity changes, clock discontinuities, scripted movement and unavailable capture **discard**
  the fraction rather than carrying it.
- **Held input against a wall produces no sound** — direction held, no displacement, no beat.
- World walking finishes its native step **after key release**, so they admit at most **one**
  additional world step for 250 ms, bounded by the remaining distance.

## 7. Data-driven scene catalogs, generated from the game's own scripts

Their `game-navigation.json` is 2.5 MB of **generated** data: 669 scenes (id, name, exits,
actors, regions) and 240 world destinations, extracted by walking the game's own scene scripts —
not hand-mapped, and carrying `ExtractionWarnings` and `MissingScripts` so gaps are explicit.

For OGA the analogous move is a **generated catalog per game**, checked in, with the generator
under `tools/` and the gaps recorded in the file. It replaces hand-written per-room tables in
adapters, and it is the difference between "we mapped this" and "we mapped what we noticed."

## 8. Completeness as a test, not a claim

The strongest habit in the whole project, and the one most directly portable:

> `FieldContentCoverageTests.EveryScriptInteractiveActorIsOfferedOrAccountedFor`

They enumerate **every** interactive actor in **every** scene — 1,973 of them — run the real
navigation source over each, and require that each is **either offered or accounted for by a
named owner**: 1,523 offered; 441 only-battles (encounter layer), 4 story contacts, 2 audited
scripted pickups, 2 parked outside the map, 1 party-change-only. **Nothing is simply missing.**

That is the same discipline as OGA's gate lessons — *enumerate the class, not the instance* —
applied to game coverage instead of to code. Our readers should have the equivalent: for each
game, the set of reachable rooms/targets is either covered or listed with a reason.

## 9. Diagnostics that survive the failure they report

A small line with a large lesson:

```csharp
// Survives Stop(), which a failed plan performs before anyone can read the
// destination back. Without it the one case this field explains reports zero.
private int plannedApproaches;
```

And they log **raw actor facts with the target counts on the first read and when counts
change** — "at most 32 actor records and no repeated output for unchanged inventories." Enough
to explain a failure, not enough to flood.

---

## What this means for Chrono Trigger on SNES specifically

OGA already has the addresses (`reverse-engineering/ctds/prior-art.md`) verified live in
BizHawk: location `0x0100`, tile `0x0102`/`0x0103`, context `0x0D13`, busy `0x0D76`, party HP at
`0x2603`+, and the object tables (`0x1600` facing, `0x1800`/`0x1880` position, `0x1A00` move
length — **the footstep source**, `0x1C01` event flag).

So the SNES work is **not** an address problem. It is exactly the layer Foresight has and we
do not:

1. a **passability graph** built from the read tile data,
2. the heading-aware search of §2,
3. counted legs spoken as §1,
4. footsteps counted from `0x1A00`/`0x1A01` (move flag and length) with the fraction rules of §6,
5. a **generated** target catalog for §7, and
6. the coverage test of §8 so we know what we have not covered.

⛔ **The SNES has no Confirm-facing problem of the same kind** — CT's field interaction is
touch/Confirm by facing, so §4's `ConfirmFacings` idea transfers, but the addresses are ours to
find. Do not assume Foresight's PC/DS addresses apply: theirs is the Steam port, ours is the
SNES original, and the two are different binaries.

---

## What we should NOT take

- **Their host.** Foresight is a Windows in-process hook and the DW2 work is C inside a PS1 core.
  Neither is OGA's architecture, and the reader layer is where the effort is either way.
- **Their scope.** 14,259 lines for one PS1 game is a warning, not a template.
- **Anything from `blind-soldier`** — it carries **no license file**, so it is all rights
  reserved. Read it for ideas at most; do not copy from it.
- **`foresight` and `beetle-psx-libretro` are GPL-3.0 / GPL-2.0-or-later**, which is compatible
  with OGA's GPL-3.0 — but compatibility means *we may reuse under the same licence with
  attribution*, never that we may absorb it quietly. These notes take the ideas and cite them.

## Credit

Design read from buu420's [foresight](https://github.com/buu420/foresight) and
[beetle-psx-libretro](https://github.com/buu420/beetle-psx-libretro) (`dw2-accessibility`).
His work is the reference implementation for counted navigation in a blind-accessible
emulator, and it is worth reading in full — `docs/navigation-counted-guidance.md` and
`docs/footstep-distance-counting.md` in that tree are the two to start with.
