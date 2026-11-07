# JVPA source mining — 2026-11-05

**What this is:** the deep source-level pass over JVPA Games' blind-accessibility
mods, following the package-level mining of 2026-11-04
(`jvpa-mining-2026-11-04.md`, catalogue §29). The encrypted `.dat` containers
have since been decoded; this note is a close reading of the actual Lua.

**Standing caveat (unchanged from §29.17):** the mods were **not run**. This is
source reading, not play testing. Effectiveness figures quoted below are the
author's own bench notes, preserved in code comments (sample counts are given
where the author recorded them). Play evidence remains the community's and the
author's.

**Catalogue home:** the reusable techniques are distilled into
`accessibility-tricks.md` **§31** (§30 is the radar-occlusion directive filed
separately today). This note keeps the fuller per-title detail.

## Source accounting

Decoded tree `~/workspace/jvpa/decoded/` — 583 files total.

| Title | Concatenated script | Lines | Modules |
|---|---|---|---|
| Pokémon (all platforms) | pokemon.lua | 198,065 | 397 files under modules/pokemon/ |
| Fire Emblem: The Sacred Stones | fire_emblem.lua | 27,791 | 38 |
| Streets of Rage 1 | sor1.lua | 15,508 | 63 |
| Streets of Rage 2 | sor2.lua | 6,132 | 42 |
| Street Fighter II Turbo | sf2turbo.lua | 2,616 | 16 |
| Dragon: The Bruce Lee Story | brucelee.lua | 1,791 | 21 |

The bridge side (RetroArch fork, Tolk/NVDA glue) still ships outside the
packages, as noted in §29.1 — what the source adds is everything the *scripts*
do: constants, cascades, state machines, and the tuning history recorded in
dated comments (mostly 25/10–04/11/2026, i.e., actively maintained).

---

## Street Fighter II Turbo (SNES) — first assessment

SF2 Turbo was **not** in the October assessment at all. It is a complete
fighting-game accessibility mod in 16 modules (~1,060 lines of module code
plus the concatenated main). Verdict up front: it is an **event-placement**
mod, not a radar mod — every fight sound the game makes is intercepted,
attributed to a fighter and a cause, and re-placed in stereo at the right
position, at a user-tuned loudness. Menus, options, character select and all
on-screen text are fully spoken. What it does **not** do is any kind of
health or timer reporting, and it has no continuous proximity beacon.

### Architecture

- The main loop calls each module's `update()` **under its own `pcall`** and
  logs per-module errors: one broken module never takes the rest down.
- All module state resets on savestate load **and** whenever the emulator's
  frame counter runs backwards (rewind / RetroArch restart detection).
- **Local-player indirection:** the two fighter records sit at $0500 (P1) and
  $0700 (P2); the launcher sets `SF2_PLAYER=2` for the netplay joiner, and
  *every* positional-audio decision follows the local fighter. The same mod
  serves both seats of an online match.
- **Sighted-partner mode** (`SF2_MODE=voyant`): the speech-only modules'
  update functions are nilled and the speak functions become no-ops, while
  the placed/replaced *sounds* stay — a sighted player keeps the enhanced
  audio without the narration (§29.14's pattern, confirmed in code).

### Screen detection without hooks (menu_accessibility.lua, 409 lines)

snes9x's Lua cannot hook game code, so the current screen is identified by
**the return address the game's own code left on its stack** at $1E3C:
$13BE title menu, $1404 mode select, $1B4B options, $18A4 in-game. A
dispatcher return address ($120A) means the real one is one stack slot
lower; the copyright screen is recognised by its caller ($0642/$064C at
$1E3E). Guards against misfires:

- A screen must be stable for **STABLE_FRAMES = 8** frames before it is
  announced (transient stack states during transitions are ignored).
- The character-select announcement waits **SELECT_DELAY = 60** frames
  (fade-in) or the first cursor move, whichever comes first.
- Attract/demo screens are announced once each, latched, until a menu or a
  game resets the latch.

The options screen is fully spoken from RAM: cursor byte $37 steps by 2 —
0 difficulty ($1C6F, spoken as value+1 stars), 2 time limit ($1C70),
4 button configuration (per-player sub-cursors $38/$39; buttons are bit
flags $1C71–$1C7C, spoken for **both** players, player 2's non-interrupting
and suppressed for the first 60 frames because the game itself resets $39
through 1 on entry), 6 sound ($1C7F bit 3 = mono), 8/$0A music and SFX
test (spoken as hex). Character select reads the fighter id at record
offset +$D1.

One deliberate omission: **player 2's character pick between rounds is not
announced when chosen** — it is held until the country-name sound plays on
the map screen (cross-module sync: sound_replace stamps
`sf2_country_frame`, the menu module waits for it) or the fight starts.
User feedback, 01/11/2026: announcing the pick early spoiled the reveal
rhythm of the game.

### Sound-queue interception (sound_replace.lua, 195 lines)

The game queues sound requests at $1720 — 16 entries × 2 bytes (id,
routing byte, always 0), write pointer $1846, read pointer $1847, mask
$1E — and ships at most one entry per frame during vblank. The mod watches
the pointers and replays each request through the bridge with a
replacement MP3 where one exists. A replacement must match **three**
things: the sound id, the sound-set mode ($1CB8), and the loaded sound
package byte ($1CB7+slot). Fallback: if the loaded package differs but
every known entry for that id in that mode agrees on one file, use it
(this is how "Round" is heard in round 2 even when "Two"'s package is
loaded). Base replacement volume 0.9.

- **Duplicate suppression:** the game often requests the same sound twice
  within a frame; only repeats inside **REPEAT_FRAMES = 1** frame are
  collapsed. The comment records a failed alternative: cutting the
  previous copy of a sound killed the score-count beeps — the dedupe
  window is what survived testing.
- **Netplay rollback fix (03/11/2026):** the mod originally zeroed queue
  entries from the per-frame Lua loop. RetroArch netplay does **not**
  redo frame-loop memory writes inside rollback frames, so the erasure
  happened on one peer only → desync. The fix moves the write into
  **execution hooks on the game's own two queue-add routines** (hooked
  just after the $1846 write, at $C0:0B55 and $C0:0B7D), so the erasure
  runs inside the game's deterministic execution and rolls back with it.
  This is the sharpest form of §29.15's netplay-symmetry rule: *any
  adapter write that netplay state depends on must happen inside the
  game's execution, not in the frame loop.*
- **Differential testing method:** the author played the same fight twice
  on the analysis core, once with sound replacement and once without,
  and diffed the request streams — this is how sound $2F was caught
  (Bison's Psycho Crusher reuses the map-plane sample: package $50,
  slot 5).
- The announcer's own calls are among the replaced sounds (19 announcer
  MP3s in the package).

### Attribution cascades (fighter_pan.lua, 319 lines) — the core of the mod

Every generic fight sound (whooshes, impacts, blocks, landings,
breakables) has to be *attributed* — which fighter, which cause — before
it can be placed. The mod does this from the two fighter records:
+$03 state ($0A attack, $0C special, $08 guard, $0E hit, $14 down,
$04 jump), +$07 X (16-bit), +$0A height (ground = $C0), +$30 health,
+$D1 character, +$F3 bit 6 = facing right. Cascades, with the author's
measured accuracy:

- **Whiffs** ($05–$08): attributed to whoever most recently entered an
  attack state; a fresh entrant (≤ **FRESH_FRAMES = 2**) wins, ties go to
  P1. Your own whiff pans toward your facing at **PLAYER_PAN = 0.4**.
- **Impacts** ($09–$0E) and **blocked hits** ($11): the victim is found by
  cascade — (1) whose health byte dropped *this frame* (authoritative;
  validated on a 214-sound test), else (2) who was in guard state within
  **GUARD_FRAMES = 3** (269 blocked hits across 12 fights, all placed),
  else (3) the non-attacker.
- **Placement semantics** (all user-tuned): your hit *on* the opponent
  plays at the opponent with a **0.55 minimum pan** on his side
  (IMPACT_PAN_MIN, raised from 0.3 — "too central point-blank"); the
  opponent's hit *on you* plays at **0.8 × his facing** (HIT_ON_PLAYER_PAN,
  raised from 0.7) — from the side the blow comes from; *your blocked
  attack* plays at **−0.55 × your facing** (your side, opposite the
  opponent — the block is something that happened to *your* limb, at the
  opponent's guard, heard from your body's side; user request).
- **Landings** ($12 fall, $13 landing): attributed to whoever's height
  crossed back to ≥ $C0 within **LANDED_FRAMES = 5** — 302 of 350
  attributed in testing, the rest left centred; a thrown-state fallback
  added later recovered most of the remaining falls (70 of 300 had stayed
  centred).
- **Breakable decor** (bricks, barrels, crates): placed on the fighter
  thrown *into* it if one is in a hit/down/airborne-non-attacking state,
  else on the striker; in bonus stages (no opponent) your own breaks play
  in front of you at **DECOR_FRONT_PAN = 0.5** by the facing bit.
- **Volume ladder** (bridge allows up to 4.0; >1.0 is deliberate):
  whoosh 1.4, impact 1.4, **blocked 1.8 — the loudest, must never be
  missed**, landing 1.6, fall 1.2, voices 1.4.

### Opponent tracking (opponent_steps.lua, 92 lines)

The shared pan law (exported as `sf2_opponent_pan`, used by steps,
voices, projectiles): **pan = sign(dx) × (0.3 + 0.7 × min(|dx|/200, 1))**
— a 0.3 floor so a point-blank opponent is still clearly on a side, full
pan at 200 px. Footsteps: one per **20 px** of the opponent's ground
travel (cadence coupled to his actual speed), at least **8 frames**
apart, none while airborne; a **teleport guard** (>16 px/frame counts as
a round reset, not steps) and the accumulator resets on landing. Step
volume 1.6 near → 0.9 far over 200 px (raised from 1.0/0.35 at user
request — distance falloff was too aggressive to track by).

### Voices (voice_pan.lua, 171 lines)

Every voice MP3's duration was measured in frames with ffmpeg (a table
of ~25 cries) so the mod knows how long a cry lives. Voices play under
per-fighter sound IDs — a new cry cuts *that fighter's* previous one,
never the other fighter's. Pan is updated every 2 frames and only when
it moved by ≥ **PAN_STEP = 0.02** (bridge update throttling). Speaker
attribution: a character-unique cry goes to the fighter whose +$D1
matches; a mirror match or shared cry goes to the freshest entrant to an
attack/special state; "K.O." goes to whose health hit the **$FF
sentinel**.

The striking detail: **the local player's own voice is panned by screen
position** — camera X at $12E7, fighters measured on screen between
21–235 px across 216 captures, centre 128, half-range 108 — so your own
Honda headbutt sweeps across the stereo field the way a sighted player
sees it sweep. A user-requested experiment; the opponent keeps relative
pan. The opponent's KO cry uses the 0.55 marked-side floor.

### Projectiles (projectile_sound.lua, 127 lines) and jumps (jump_sound.lua, 84)

Projectile slots $0900/$0950, type at +$0D: 00 Hadouken, 01 Yoga Fire,
03 Sonic Boom, 04 Kikoken, 05 Tiger high, 06 Tiger low (02 Yoga Flame
and 07 Vega's claw deliberately excluded — not projectiles). The
replacement is a synthesized wind loop (the user's own chosen
`vent.ogg`). On spawn, **the sound starts at the shooter's voice pan
and glides to the projectile's true position over GLIDE_FRAMES = 10** —
the shot is heard to leave the hand. Pitch by type: Tiger high 1.15,
low 0.87, others 1.0. Fade in 3 frames, out 2 (the record empties the
same frame the shot dies; without the fades you get clicks — 82 shots
verified). Opponent's shot: volume 1.3 near → 0.55 far; yours: flat 0.55.

Jumps: takeoff = height leaves $C0 *while the state byte says jump*
($04) — throws and hit-states excluded by construction (248 jumps over
4 fights in testing). The jump sound (borrowed from the Bruce Lee set)
is capped at the MP3's real 81 frames; the opponent's jump is tracked
every 2 frames with the same 0.02 pan step; yours plays centred.

### Text reader (text_reader.lua, 157 lines)

Two non-interrupting channels:

1. **The game's text machine:** text number at $1850, read cursor
   (16-bit pointer into bank $C4) at $0068. The mod ships full texts
   (texts_data.lua, 383 lines: character profiles, win quotes, endings,
   credits, FR/EN) and speaks a text when the cursor *enters* its
   recorded address bounds. Re-trigger = a new text number, or the
   cursor moving backwards (the same text shown again).
2. **VRAM scraping:** tile grids at word $4800/$5400 read every 6
   frames; the low byte of each cell is ASCII. Known messages are
   matched by string/pattern with an `unless` guard ("CONTINUE" is
   suppressed when "V.S. OPTION" is on screen); blinking messages re-arm
   only after **GONE_FRAMES = 90** frames absent. "YOU HAVE MASTERED X"
   extracts the name with word-boundary checks so "KEN" is not matched
   inside another word.

### codes.lua — a cheat shipped as accessibility

The mod permanently applies the game's hidden 10-star Turbo code
(writes $20 to $1C87 every frame, because the game zeroes it at boot) —
a quality-of-life cheat shipped as an accessibility feature at user
request. The other two hidden codes (specials off in 1P; VS specials
toggle) are documented from disassembly but not applied.

### Scorecard against Devin's fighting-game standard

| Standard element | SF2 Turbo |
|---|---|
| Menu / options / character-select speech | ✅ Complete, incl. both players' button configs |
| Opponent proximity **radar** (continuous) | ✗ None — position is event-driven (steps, placed hits, tracked voices/projectiles) |
| Health reporting (milestones + on demand) | ✗ None — health bytes are read only as attribution signals and for the KO sentinel |
| Timer / round state speech | ✗ None |
| Position query | ✗ None |
| Timed-input cues | n/a — SF2 has no timed-defense mechanic; nothing to cue |
| Placed event audio | ✅ Exceeds the standard's cue side: full attribution cascades, per-cause volume ladder, screen-pan for your own fighter |
| Netplay | ✅ Both seats via local-player indirection; rollback-safe writes |

**What's reusable for OGA's fighters:** the attribution cascades and
placement semantics wholesale (they are game-independent — any fighter
with state/health/height bytes can run them), the pan law with its 0.3
floor, the step cadence + teleport guard, the voice/projectile tracking
with throttled updates, the spawn glide, screen-stack menu detection for
hookless cores, and the rollback-determinism rule. What OGA must add on
top is exactly what Devin's standard already demands and this mod lacks:
milestone health speech, a health/timer query, and (for games where
events are sparse) a proximity beacon.

---

## Pokémon — the source under §29.6/29.7

### Speech queue internals (common_pokemon_tolk.lua, 87 lines)

The §29.2 queue rule, exactly: a FIFO `pending` list; a non-interrupt
message is queued if a direction is held **or the queue is already
non-empty** (so ordering survives the moment of release — a burst that
starts queued stays queued in order). The flush runs in an
`onframeend` handler: a `released` counter must reach **RELEASE_FRAMES
= 3** consecutive direction-free frames, then all pending messages speak
in order as separate SPEAK commands. Command answers bypass via an
`M.direct` flag set by the command handler. `silence()` clears the
queue and sends CANCELSPEECH. Before speaking, each message passes a
normalisation pipeline: abbreviation expansion first, *then*
punctuation-only detection (split at the first ": " for the speaker;
if the body is only dots/ellipses/punctuation/spaces, it is spoken as
"point point point" appended after the speaker's name).

### A* engineering (common_pokemon_a-star.lua, 247 lines)

RapidFire Studio's A*, modified by JVPA on 25/10/2026:

- Heuristic changed from Euclidean to **Manhattan** — Euclidean
  "favored zigzags" on a 4-way grid.
- **Turn penalty +0.001** added to a step's cost when it changes
  direction relative to the previous step: among equal-length routes,
  the search prefers fewer turns — i.e. simpler *spoken* directions.
- Neighbourhood = every offset with Manhattan distance ≤ 2, resolved
  through a position index `at[y][x]` that holds **multiple nodes per
  tile, one per height level** (bridges walk over tiles you can also
  walk under). Distance-2 neighbours are how **ledge jumps** enter the
  graph.
- Open set is a **binary heap**, closed set a hash set. The stock
  version compared nodes pairwise — the comment records the result on
  big routes: "millions of checks, the game froze."
- A path cache keyed [start][goal] exists but its store line is
  effectively vestigial — noted so nobody catalogues it as working.

Path construction in the platform layer: the node grid is the map's
collision table trimmed 3 tiles per side; each node carries the tiles
around it (`special_tiles`) for the platform's step-validity rules
(ledges, one-ways, surf/ice). Before searching, **dynamic obstacles are
stamped into a copy of the collision map**: non-ignorable objects and
every warp *except the destination warp* become impassable (0xFF) — a
route never leads you through a door you didn't ask for. Map
connections try up to the **8 nearest border tiles** until one paths,
then append the step off the map as the final node; multi-tile objects
fall back to their other tiles when the main tile is unreachable.
Spoken form: each step becomes an optional HM prefix ("Cut on way…",
only when the player actually has the HM or the all-HMs option is on)
plus a direction word; consecutive identical steps are run-length
grouped and spoken one utterance per group ("3 north", "2 east", …).

### Radar orchestration (common_pokemon_radar.lua, 1,037 lines)

§29.6 gave the Manamon dB constants; the source adds the *orchestration*
— how 20+ simultaneous loops and beacons stay intelligible:

- **Mixer law made explicit:** their lua_audio mixer attenuates the
  opposite channel linearly, so Manamon-style dB values convert as
  pan = 1 − 10^(−|dB|/20) (clamped at ±100 dB) and gain = 10^(dB/20).
  Pan comes from the east–west offset only; **volume comes from
  Manhattan distance** × the per-kind step (§29.6), beacons at unity
  gain (0 dB reference).
- **Voice budgets per family:** people 4, doors 2, stairs 2, items 2,
  signs 2, gates 2, caves 2, objects 2 — hard caps on simultaneous
  beacons per kind, nearest first. Same-family beacon slots are
  staggered by (i−1)×9 frames so two beacons of a kind never fire on
  the same frame.
- **Map-change discipline:** on entering a map, **40 frames of quiet**
  (MAP_CHANGE_QUIET); families then start staggered by kind (people 0,
  gate +6, door +12, cave +12, sign +18, stairs +24, item +30, object
  +36). The map id must be stable for **MAP_SETTLE = 8** ticks before
  its data is trusted (GB changes the id a frame *before* the map
  loads), and map data is re-read at +30 and +90 frames after arrival.
- **Per-family cadence/pitch:** people and doors beacon every 35
  frames facing north / 38 facing south, south pitch ×0.85 — *except*
  person and sign sounds carry direction baked into the sample
  (pnj_up/pnj_down, panel_up/panel_down), so their pitch stays 1.
  Stairs are slower (75/85, south ×0.92; constants borrowed from the
  Zelda 3 mod). Signs: 100/110 and **outdoors only** — indoors, "signs"
  are mostly bookshelves, TVs and PCs (which the object family covers).
  Objects: 80/90 with a ~50-type taxonomy mapped to Manamon interact
  sounds (tv, bookshelf, fridge, healing_machine, …) and per-sound
  volume trims (television ×0.5, user request).
- **Items are loops, not beacons** (the rhythm lives inside Manamon's
  item.wav), and each item instance gets a **random base pitch in
  0.97–1.03** so two nearby items are distinguishable by ear.
- **Wall loops:** per-direction contact volumes N 0.40 / S 0.56 /
  W 0.20 / E 0.12 (low-pitched wall sounds are mixed louder because
  they carry less far); falloff **5 dB per tile** (lowered 04/11/2026
  from 8 — "wall at 3 tiles almost inaudible"), range 5 tiles; E/W
  walls also pan (base 4 dB + 1.8 dB/tile). Standing *on* a warp tile
  suppresses the distance-1 wall on the exit side (you're in the
  doorway, not facing a wall).
- **Sonars** (water/land/ice): directional loops to the first tile of
  that kind within 5 tiles, 6 dB/tile, pan ±0.6; the land sonar is
  active **only while standing on water** (it's the find-the-shore
  tool), and water/ice sonars are silent while you're standing on them.
- **Door axis:** range 10 tiles, requires an unobstructed straight
  line (checked tile by tile); N/S axis volume ramps 1.0 → 0.25 with
  distance, E/W fixed 0.8 panned to the side; four rotating voice slots.
- **Warp taxonomy:** 18 kinds (house, sliding, stadium, dungeon, old,
  factory, ancient, manhole, cave, gate, three stair materials —
  rock/wood/metal — ladder, hole, teleporter, elevator, boat, plain),
  each with a family and beacon/enter/exit/pass sounds. The traversal
  direction for the *entry* sound is inferred from whether the
  destination is outdoors. The game's own door sound is muted **only
  when a classified replacement will actually play**
  (`radar.warp_has_sound`), with per-platform timing quirks recorded:
  GB takes the warp the same frame as the step and the map id changes
  before the sound; DS records the warp one tile past the mat in the
  pushed direction; Black/White's two-tile mats are deduplicated.
- **Bump handling:** when the platform can hook the game's own bump
  sound, the radar plays the *classified* sound at the game's rhythm
  and its own detector stands down — one bump, one sound. The fallback
  detector requires **both** position-still **and** the same direction
  held for **STUCK_FRAMES = 12** (fix dated 04/11: a U-turn right after
  a pause used to trigger the obstacle search, which costs ~10 ms/frame
  in Black/White — audible crackle on slow PCs). Classification is
  throttled to the 14-frame bump repeat even when nothing is
  recognised.
- **Building-wall test = bounded flood fill:** from the bumped tile,
  flood the connected impassable region (≤80 cells inside an 8-tile
  box); if the region touches a door tile, it's a building wall, and
  the sound pans to the pushed side.
- The radar silences itself whenever the player is locked (dialogue,
  menus, cutscenes), off-map, or on title screens. People/items refresh
  every 15 frames or on movement. A DEBUG_BW profiler logs any phase
  taking ≥3 ms.
- Per-platform interface: each platform supplies `radar_passable`,
  `radar_people`, `radar_refresh`, `radar_is_stairs` (plus optional
  items/signs/gates/tile-kind/warp-kind providers) — the orchestration
  above is written once against that interface.

### Relocation deepening (common_pokemon_autodetect.lua + anchors)

§29.3 summarised signature relocation; the source mechanics:

- **Voting, not first-match.** Every signature match extracts a
  candidate address; candidates are plausibility-filtered (GBA:
  EWRAM 0x02000000–0x0203FFFF, IWRAM 0x03000000–0x03007FFF, or the ROM
  window; GB: 0xC000–0xDFFF). Addresses used by *recompiled* engines
  (ROM hacks) need **≥2 agreeing votes** — an isolated signature can
  land on a neighbouring function under a different compiler; plain
  addresses need 1. A value seen 3 times short-circuits as "settled."
  **Exception:** a signature that begins exactly at a function prologue
  (0xB5 push at +2, pattern ≥64 hex chars, match offset ≤1) is trusted
  on one vote.
- **ARM literal-pool extraction:** the address is usually *not* in the
  matched bytes — the match is an `ldr rX,[pc,#imm]`; the pool address
  is (match+4) aligned down + (imm low byte)×4; the u32 read there,
  minus a per-signature field offset, is the variable. Signatures
  deliberately match code that uses a *field* of the target struct
  (e.g. gBattleMons+42), never the base load alone. Signature entries:
  [pattern, extract offset, search-range lo, hi, field offset, version
  tag].
- **Engine-version election:** confirmed matches vote for the engine
  build via their tags; marker signatures unique to particular versions
  tally separately. pokeemerald-expansion hacks stamp "RHHEXP" plus
  version bytes in the first 4 KB — the chosen reference is the nearest
  version *not exceeding* it. Hook signatures carry per-version
  register maps (exported as `autodetect.hook_regs`).
- **Struct-size recovery:** the most frequent `movs rX,#n` multiplier
  near loads of an array's address/fields is taken as the struct
  stride under the hack's compiler.
- **Pool fallback:** binary hacks often write the new address over the
  old one in the *original* code's pool slot — so the base game's pool
  slot is read as a fallback candidate.
- **Neighbour shift, exact rule:** an undetected RAM address follows
  its neighbours only within **0x80 bytes** in the same memory zone,
  and only if *all* detected neighbours shifted identically.
- **GB GSC special case:** landmark/map-group tables are found by
  chained signatures (the GetLandmarkName pattern → its bank-selecting
  caller → un-banking arithmetic). Map *names* are always read from the
  hack's own ROM — base-game name lists are wrong in hacks
  (mapid = group×256 + num).
- Cache: the launcher passes `JVPA_ROM_KEY` (CRC32+size) →
  `analyses/<key>_v8.lua` (cache format VERSION = 8), SHA-1 fallback;
  an offline analysis entry point exists
  (common_pokemon_analyse.lua, 75 lines).

**Anchors (common_pokemon_anchors.lua)** — the compiler-independent
route: start from **data** (texts/tables — byte-identical under any
compiler), find the function that loads it, then follow by **rank**
(the nth call inside the function, the nth constant of a kind it
loads). Rules are generated by a Python tool (`gen_anchors.py`) from
reference builds *with* symbols. **Quorum: an address is accepted only
if ≥2 independent rules agree.** Function ends (for caller
identification) are estimated by scanning forward for a return
(pop{…,pc} 0xBD / bx 0x47) followed by the next push{lr} 0xB5, capped
at 0x2000 bytes. Engine hooks: when a mid-function hook point isn't
found in the hack, hook the function's **entry** instead (arguments
are always in r0–r3). gMain-derived addresses are chained from the
relocated CB1 pointer (CB2 = CB1+4; inBattle at +0x439, FireRed
+0x438). Hooks that can fire twice per frame (cursor erase+draw) get a
once-per-frame dedupe.

**One more shim for §29.13's family** (common_pokemon_pa_paths.lua):
the RetroArch port monkey-patches `io.open`, `io.lines`, `dofile` and
`loadfile` so BizHawk-era scripts' hard-coded paths resolve into the
new per-console directory layout — the old code is untouched.

---

## Fire Emblem: The Sacred Stones — the source under §29.8

### Forecast in words, exact template (fire_emblem_gba.lua `nice_battle_forecast`)

> "Your unit is {unit}, {HP} HP, {net damage} Attack[ times N],
> {Hit}% Hit, {Crit}% Crit, The opponent is {unit}[ holding {weapon}],
> {HP} HP, {same} — or 'Cannot counterattack'."

Details that matter: "Attack" is **net damage** — the mod subtracts
the defender's battle defense from the attacker's battle attack before
speaking (the number a player actually plans with); multi-hit is a
"times N" suffix only when N>1; the no-counterattack case is stated
explicitly instead of printing zeros; the opponent's weapon name comes
from the
...[truncated 8184 chars]