# Applying the mod research to OGA's adapters

Concrete adoption plan. Every claim below was checked against this repo's actual code, not
recalled. Companion documents: `how-mods-make-mechanics-accessible.md` (the design
catalogue) and `accessibility-mods-survey.md` (the architecture survey).

Scope: what to change, in what order, and what NOT to build.

---

## 1. What the audit found: the queue is already used correctly

I read every `Say()` call site in all five native adapters
(`fe_adapter.cpp`, `dq9_adapter.cpp`, `dbz_adapter.cpp`, `gba_adapter.cpp`,
`dissidia_adapter.cpp`) — 84 `Priority::` uses in Dissidia alone.

**The result is good news, and it means one whole rule is already done.**

- **Player-requested lines are `High`.** Dissidia's `CmdDirections`, `CmdMarkers`,
  `CmdWhereAmI`, the DQ9 nearby scan and DBZ party queries all speak `High`, which is
  correct: the player asked, so it must never be deduped or dropped
  (`announce.h`: "High items bypass it (the player asked, so they hear it)").
- **Automatic and ambient lines are `Normal` or `Low`.** Dissidia's `boardhint`
  ("Press X to engage."), `boardalert` ("You're out of Destiny Points!"), `bonus`,
  `playplan` and the `qte` prompts are `Normal`. That is rule R2 — *never interrupt what
  the player asked for* — implemented already.
- **Refusals are named in speech, not silent.** "No board.", "Board unreadable.",
  "Cursor unreadable.", "Not tracked yet.", "Location unknown. The map is still loading."
  This is rule R8, and it is the row most mods skip. OGA does it.

⛔ **So do not "improve" the priority scheme.** It is right, and the docs
(`docs/design/announcement-queue.md`) already specify it. The remaining work is
elsewhere.

---

## 2. The four real gaps, in cost order

### Gap 1 — A `dir`/direction vocabulary exists per adapter, but no shared spatial form

Two different, both reasonable, neither shared:

- **DQ9 speaks camera-relative steps**: `"%ld step%s %s"` → *"3 steps up, 1 step right"*,
  with `"right next to you"` inside 1.2 units, and it is explicitly camera-relative
  (`ux`/`uz` = the unit vector from camera to player). This is rule R6 done well — the
  player's own unit, in the frame the stick acts in.
- **Dissidia speaks compass names on a grid**: *"Open: north, east. Blocked: west,
  south."* Correct for a fixed-camera tactical board with no rotation, and it has the
  honesty caveat already written in (`// Grid-legal is necessary, not sufficient.`).

⛔ **Do not extract a shared helper yet.** This project's rule is that a member exists
only when a real game needs it (`oga_core.h`: "add a member only when a second backend
actually implements it"). Two adapters with different geometry do not prove the same
helper. **Wait for the NES/DS tile readers to want the same thing**, then extract.

**What to adopt instead, and it is free:** the research's spatial vocabulary is richer
than either adapter's, and these are additions to *wording*, not architecture.

- **Distance bands, not raw counts.** FFXII and SF6 both move to bands once far
  ("distance band halved/doubled" as an announcement trigger). DQ9's `"7 steps up"` is
  exact and useful at 7; at 40 it is not. Bands: `next to you`, `N steps`, then a band
  word.
- **The clearance/obstacle verdict set**, for any adapter that can see a tile ahead —
  straight from SF6's string table: `Clear ahead`, `Low step`, `Waist-high obstacle`,
  `Wall`, `Blocked`, `Clear for N`, `drop ahead`, `Exit ahead/left/right/behind`,
  `No way through in range`. A DS/NES reader gets a real, tested vocabulary for free.
- **`at N o'clock` as an alternative to compass**, and make it a setting the way
  Wasteland 2 does (`UseClockPositions`). Some players navigate by clock far better than
  by compass; it costs one format string per adapter.

### Gap 2 — Repeat exists; a *history* does not

`Sources/OpenGameAccess/ControllerInput.swift:314` already implements `.repeatLast` from
`speech.lastSpoken` — one line. Kingdom Access keeps **50** messages with repeat / previous
/ next; Duel Links has repeat-current-item *and* repeat-last-announcement. The gap:

- **One line is not a history.** A player who missed two announcements ago has no path to
  it. The queue (`Core/announce.h`) already knows what it spoke — but it keeps no record.
- **Cheapest real fix:** a small ring of the last N *spoken* lines (N=8–16) inside the
  queue, plus two commands (`RepeatPrev`, `RepeatNext`) alongside the existing
  `StopSpeech` in the append-only `Command` enum. The queue is the right owner: it is the
  only thing that knows what actually reached the speaker rather than what an adapter
  asked for.
- ⛔ Note the ordering hazard already documented: a `Normal`/`Low` line is stale by the
  time it would be repeated (`announcement-queue.md`: "a Normal or Low line is not [retried]
  — it would be stale by then"). **Repeating is a player action, so it speaks `High`** and
  bypasses the staleness logic entirely; the entry just has to still be in the ring.

### Gap 3 — Nobody can see what the app will say, without grepping source

The mods' own string tables (`SF6Access/lang/en.txt`, 1,004 entries) are why their
announcement vocabulary is reviewable. OGA's is scattered across five adapters as inline
`snprintf` format strings, so "what does the app say for a DS game?" is unanswerable
without reading C++.

**Fix, and it is cheap:** `scripts/audit-adapter-speech.py` (added in this commit) walks
`Core/*.cpp`, extracts every spoken literal and format string with its group and
priority, and prints a per-adapter inventory. Use it to spot a line that is `High` when it
should be `Normal`, or a sentence that would be ambiguous spoken aloud.

### Gap 4 — The audio-cue path is specced, unbuilt, and the research validates it

`docs/proposals/dissidia-battle-audio.md` is a real spec (pan = left/right, cadence =
distance, pitch = elevation, hysteresis at 1.1X). It is unbuilt — `oga_core.cpp` records
`/* no audio path yet (reader cues are text) */` for both Game Boy and PSP.

**The research confirms the spec and independently catches the same bug its own docs flag.**

- FFXII's beacon: *"A sound behind you is quieter, duller, and about a fifth lower in
  pitch than one in front"* with pan independent of the behind-filter — i.e. **pan carries
  left/right through the full 360°, and a separate channel carries front/back**. The
  Dissidia spec's "stereo pan = left/right" plus "pitch = elevation" is the same split.
- Zomboid: threat = double thump, weapon-in-range = wood-block tick, escape route = two
  rising whistles. **Each kind of information gets its own timbre.**
- ⛔ **And this is the bug worth fixing before anyone builds it:** the Dissidia spec uses
  pulse *rate* for both per-side identity (enemy vs EX core) and proximity. Its own
  `announcement-queue.md` already flags this — "Rate cannot carry both identity and
  distance — a near core and a far enemy can pulse alike. Carry identity in timbre or a
  two-note figure instead, and keep rate for distance." **The research agrees from two
  independent mods**: Zomboid keeps rate for distance and uses distinct sounds for
  identity; FFXII keeps cadence for proximity and a distinct timbre per beacon (route
  beacon vs target beacon, "a second sound takes over"). So the open question is
  answerable: **identity in timbre, distance in rate.** No playtest needed to settle it.

---

## 3. What the research says NOT to do

- **Do not copy the Family A mechanism.** 14 of 31 mods inject a DLL and call NVDA. There
  is no cartridge to inject into and no NVDA on iOS or Android. Only their *choices about
  what to say* transfer (already extracted).
- **Do not build a shared spatial/framework layer yet.** Two adapters with different
  geometry is not evidence. Wait for the NES/DS readers.
- **Do not promise a StarFox adapter.** `blind-starship` is a decompilation port with no
  ROM image to read, and there is no N64 core. Recorded in `Core/n64_adapter.cpp`.
- **Do not port the audio path into a core.** The spec is right that cues are synthesized
  host-side and are "NEVER in-engine voices and NEVER guest writes" — the adapter contract
  is read-only RAM plus speak/log/buttons, and cues must not change feedback semantics.
- ⛔ **Do not let a ray-style "exit" claim stand as fact.** SF6 measured that a beam which
  "looks far" is not a way out (a 1.6 m alley 6 m away subtends ~5°; three 90° beams miss
  it, and even 24 rays do). Any OGA reader that infers an opening from distance alone will
  lie in exactly the same way. Take passages from the map's own connectivity, or label the
  claim ("open", not "exit") — Dissidia already does the honest version with "Grid-legal is
  necessary, not sufficient."

---

## 4. Ordered plan

Cheap and real first; flagged where it needs a playtest or a core.

1. ~~**Add the last-N spoken ring to the queue** plus the repeat commands as append-only
   `Command` values (Gap 2).~~ ✅ **DONE** — `kAnnounceHistoryMax` = 16 in `Core/announce.h`,
   recorded in `emit()` (only what was SPOKEN), with `RepeatNewest`/`RepeatOlder` as
   append-only `Command` values 38/39. The `cmd > StopSpeech` gate in pokecore.cpp was
   widened in the same commit. Proven by `SABOTAGE_REPEAT_INCLUDES_SUPPRESSED`.
2. **Adopt the wording sets** (Gap 1): distance bands, the clearance/obstacle vocabulary,
   and a clock-position option. Format strings only, no architecture. **NOT DONE** — held
   back on purpose: it touches adapter wording, which is best changed alongside a live
   playtest rather than blind.
3. ~~**Run `scripts/audit-adapter-speech.py` across all adapters**~~ ✅ **DONE and extended**
   into a real gate: `--check` exits non-zero on a mechanical violation, `--coverage`
   reports which design rules each adapter follows, and `scripts/audit-speech-test.sh`
   proves the gate catches each violation class and produces no false positives. The tree
   currently passes with 0 violations.
4. ~~**Settle the beacon encoding as "identity in timbre, distance in rate"**~~ ✅ **DONE** —
   `docs/proposals/dissidia-battle-audio.md` now specifies distinct TIMBRES for the enemy
   and the EX core with rate reserved for proximity, and `docs/design/announcement-queue.md`
   records the same question as resolved. The spec also carries a new §1b of cue rules
   inherited from the research.
5. **Then** build the host-side cue synthesiser, which is the real ceiling on every spatial
   design here. Needs an iOS AudioUnit path and an Android equivalent, and it is a product
   decision, not a research one.
6. **Then** the NES/DS tile readers, at which point the shared spatial helper is justified —
   and the Zelda 1 reader is a Lua port gated on the NES core, not an adapter to write
   (`accessibility-mods-survey.md` §7).
