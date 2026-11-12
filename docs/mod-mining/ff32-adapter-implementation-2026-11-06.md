# §32 → adapter implementation pass (2026-11-06)

Devin's direction: "go through our adapters we already have and work on
implementing anything that we've found that could use more." This pass took
the §32 Final Fantasy mining findings (tricks catalogue §32.1–§32.14 plus
quick picks), with the intersecting §30/§31 items, through every registered
adapter in the OGA tree.

## Where the work lives

- Repo used: the Legion WSL clone `/home/devin/oga-work` (real git,
  HEAD == origin/main `05f3750` at start). The VM-local copy at
  `~/workspace/repos/open-game-access` is stale (HEAD `e13e0df`, not on
  main's lineage, dirty across the tree) and was **not** used.
- The clone's pre-existing dirty files (5 Swift app files +
  `docs/reverse-engineering/steinsgate-psp.md`) were left untouched;
  only Core adapter files were edited.
- Work is on local branch **`ff32-adapter-improvements`**, commit
  **`b7cedad`**, **NOT pushed** — for Sarah/Devin review.
- The GitHub connector was not used for any repo reads (AGENTS.md rule).

## Headline result

The tree already embodies most of §32. The announcement queue *is*
§32.5's discipline (value-keyed dedup, rate limits, expiry, a 16-line
spoken-history ring, silence-until-High), the PSP health watches already
run §31.14's latch + re-arm pattern, ALttP's objective is already
§32.12 state-keyed guidance, and no adapter computes routes — so the
route-centric findings (§32.2/§32.6/§32.10) have nothing to attach to
yet and are proposals below, not forced code.

Two genuine gaps were found, fixed, and test-covered:

### 1. DBZAR — city damage-band watch had no re-arm of its own (§31.14)

`Core/dbzar_adapter.cpp`, `OnFrame()`. The watch announced band
crossings (the game's own 80/50/30) on a bare per-frame edge and leaned
on the announcement queue's slot+band **dedup key** to avoid repeats.
That key suppresses against the group's *last spoken line*, so:

- a city repaired and then attacked again could have its second
  crossing silently eaten (same key, nothing else in the group since);
- edge jitter could still repeat whenever another city's line happened
  to intervene — the key never actually guaranteed once-per-crossing.

This was the one watch in the tree without the family's latch +
re-arm: SfTrackSide (Dissidia/MKU), the BBS health watch and the KHBBS
low-HP warning (whose comment documents this exact dedup hazard) all
latch per threshold and re-arm at threshold + 5.

Fix, mirroring that pattern: per-city band latches (`g_bandLatch`),
primed silently on first sight (bands already below the ratio start
latched), re-armed when the ratio recovers to band + 0.05, the dedup
key dropped (the latch is now the guard), and — the health watches'
"speak the worst" rule — a plunge speaks only the **lowest** band
newly crossed ("critical", not "below 80 percent"). Priority and group
unchanged (Normal, `dbzar-city`).

Tests (`Core/dbzar_adapter_test.cpp`): contract line 3 rewritten to
the new semantics; 4 new checks — first fall speaks; repair inside the
margin does NOT re-arm (second fall quiet); repair past the margin
re-arms (third fall speaks); a 90%→28% plunge speaks once and says
"critical". Suite: **56 → 60 checks, 0 failed**.

### 2. DQ9 — nearby-object cycle had no ordinal (§31.12)

`Core/dq9_adapter.cpp`, `CmdNearby()`. The §31.12 target-cycling
pattern is name + relative offset + "N of M"; DQ9's own party cycling
already speaks "%d of %d", and Khbbs/Dissidia cycles do too. The nearby
cycle was the outlier: "Someone. 5 steps up." with no position in the
cycle — and since DQ9's labels are deliberately generic, three
"Someone"s in a row are indistinguishable and the player cannot tell
the cycle moved.

Fix: the line is now "Someone, 1 of 2. 5 steps up." (label,
cursor + 1 of n, directions). Tests: the three existing nearby checks
strengthened to assert "1 of 2" / "2 of 2" / back to "1 of 2".
Suite: **26 passed, 0 failed**.

## Test results (Legion WSL, branch ff32-adapter-improvements)

Baselines were taken before editing; all suites were green before and
after. Full run after the changes:

| Suite | Before | After |
|---|---|---|
| dbz (Saiyans) | 68/0 | 68/0 |
| dbzar (Another Road) | 56 checks/0 | **60 checks/0** |
| gba | pass | pass |
| dissidia | PASSED | PASSED |
| dq9 | 26/0 | 26/0 (3 checks strengthened) |
| mku | 145/0 | 145/0 |
| khbbs | 554/0 | 554/0 |
| alttp | 73/0 | 73/0 |
| osk-echo, android-compose | pass | pass |
| `scripts/adapter-tests.sh` overall | exit 0 | exit 0 |
| `scripts/announce-test.sh` (incl. 8 sabotage cases) | exit 0 | exit 0 |

## Per-adapter audit

| Adapter | Findings considered | Verdict |
|---|---|---|
| KHBBS (8633 lines) | §32.5 speech discipline, §31.14 hysteresis, §32.8 cue staleness | **Already embodied.** Low-HP warning latches with 26/31 re-arm; health beeps + milestones are the pattern's origin; cue snapshot fails closed and clears one-shots on take. No change. |
| Dissidia | §32.6 same-reads cue/speech, §32.9 data-table story speech, §30 occlusion | **Already embodied** where data exists (CueSnapshotFill reuses the command reads verbatim; story lines are shipped data tables in `Core/story/`). Occlusion: proposal (below). |
| MKU | §31.1 debounce, §32.5 | **Already embodied.** Dialogue settles `kDlgStableFrames` before speaking; health watch is a faithful copy of the family pattern; village guide fails silent off its verified ground. No change. |
| DBZAR | §31.14, §32.7, §32.14 | **Changed** (band latch + re-arm, above). Ring already satisfies §32.7: every readable target is listed, empty kinds are named as empty, never deleted silently. |
| DQ9 | §31.12, §32.7 | **Changed** (nearby ordinal, above). Route planning/auto-walk remain deliberately the Lua mod's territory (documented scope decision in the adapter header) — not revisited. |
| DBZ / Saiyans | §31.1, §32.7 | **Already embodied.** Dialogue/menu/battle watchers all settle-debounced; the enemy-roster command honestly refuses ("Not applicable in this game") because no enemy map is verified — §32.7's honesty rule applied to a whole command. No change. |
| ALttP | §32.9, §32.12, §31.13 | **Already embodied.** Objective re-derives from the game's progress bytes on every press (state-inferred resync by construction); message text identified by content hash with a two-frame settle; guide aims at the objective area and refuses the unmeasured Dark World rather than guessing. No change. |
| Fire Emblem | §31.12 forecast template, cursor travel, attack solver | **Proposal** (below). The reader's verified scope is map questions + front menus; it has no forecast data at all. |
| GBA / NES / N64 | — | Script-bridge / stubs; nothing in §32 attaches. No change. |
| announce.cpp (shared) | §32.5, §31.7, §31.8 | **Already embodied**, and sabotage-tested (each rule has a build that must fail without it). No change. |

## Proposals (not implemented, with reasons)

1. **Baked nav grids + pure router (§32.2), one committed route
   (§32.6), leg merging + percent-of-route (§32.10), turn-penal
...[truncated 3907 chars]
<!-- RE-AUTHORED TAIL (2026-11-08): everything below this marker was
re-authored; the original text was lost to a snapshot-push truncation. -->

*Editorial note, 2026-11-08: the original ending of this note was lost
when a snapshot push truncated the file — the missing tail is the rest
of this Proposals section (~3,900 characters). The text below was
re-authored on 2026-11-08 at the owner's direction, reconstructed from
the note's own headline and per-adapter audit above (which summarise
each proposal and its blocker) and from the catalogue sections the
proposals cite (§32.2, §32.6, §32.10, §31.9, §30, §31.12). It is a
reconstruction, not the original wording.*

**1. The route package: baked nav grids + pure router (§32.2), one
committed route (§32.6), leg merging + percent-of-route (§32.10),
turn penalties (§31.9).** Not implemented because there is nothing for
it to attach to: no adapter in the tree computes a route or owns
navigation data. ALttP's guide aims at the objective area — a
direction, not a route — and DQ9's route planning and auto-walk are
deliberately the JVPA Lua mod's territory (the scope decision recorded
in that adapter's header and in the audit above), so importing a
second router there would fork behaviour the project chose not to
own. The package also has a hard prerequisite none of OGA's titles
meets yet: §32.2's grids are baked offline from map dumps (collision,
layers, hidden passages, warp edges) and the router is only
trustworthy because the identical code runs in an offline checker
against every baked map before shipping. No OGA title has map-dump
tooling today. When the first one does, adopt the package whole, not
piecemeal: one committed route object feeding spoken directions, the
beacon, the reachability filter and any auto-walk (§32.6 — no
consumer recomputing its own shortcut, a no-path result propagating
to all of them, §32.7's proved-unreachable-only rule governing the
filter); leg merging and percent-of-route on the platform progress
control so the spoken form stays listenable over walkmesh-grade
detail (§32.10); and a turn penalty in the search cost, on §31.9's
evidence, so the route the player hears prefers fewer turns among
equal-length candidates.

**2. Dissidia radar occlusion (§30).** The spec is settled — it is
Devin's directive, filed as §30: the beacon muffles (low-pass at the
same loudness, never merely quieter) when the tracked enemy is behind
solid geometry, the where-am-I query names the nearest cover object,
and both work while paused. What is missing is not design but data:
the muffle needs a player↔enemy line-of-sight verdict every frame,
and no LOS source has been verified in Dissidia's data yet. §30's own
implementation note sets the order of attack — prefer the game's own
visibility/LOS data where the engine keeps it; fall back to an
adapter-side raycast against collision geometry only if the game
keeps none — and the same test then feeds the cover search (nearest
cover = nearest occluder that would break the line). Until that
reverse-engineering is done there is no honest code to write: a
muffle driven by a guessed LOS would lie about exactly the thing the
cue exists to say. Sequencing: Dissidia first (§30's scope), BBS
arenas with occluders second; KHBBS's cue snapshot already fails
closed, which is the right substrate when the data lands.

**3. Fire Emblem tactics layer: forecast template, cursor travel,
attack solver (§31.12).** The reader's verified scope is map
questions and front menus; it reads no battle-forecast structures at
all, so there is currently nothing to format. JVPA's template is only
speakable because their mod reads the game's own forecast proc —
both BattleUnits' attack, defense, hit and crit, the hit counts, and
the opponent's weapon name taken from the proc's own text id — and
OGA's adapter has no equivalent verified reads yet. The prerequisite
is therefore a data pass, not a writing pass: verify Sacred Stones'
forecast proc and BattleUnit layout in the adapter's terms (the JVPA
addresses and field offsets are the map for that work; §31.12 records
them). Once the reads exist, the rest follows as one layer, in
§31.12's shapes: the forecast line with **net** damage (attack minus
the defender's battle defense) and "Cannot counterattack" stated
instead of zeros; the cursor-travel state machine (horizontal axis
first, per-cell announcements suspended in transit, a 90-frame stall
or 600-frame timeout answered honestly with "The cursor cannot get
there"); and the attack-position solver over the movement-cost map —
least movement cost, tie-break farthest from the enemy, weapon ranges
decoded from the item data's range byte, its nearest-tile fallback
announced as out of reach this turn rather than silently substituted.
