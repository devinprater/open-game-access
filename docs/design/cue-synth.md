# Cue synth (item 5) — what was built, and how to judge it

Status: BUILT. The lock-on beacon is the first cue family, on both the core side (the
adapter reports a silent snapshot) and the app side (the synth makes the tone). It is off
by default and has not been heard by its owner yet.

Companion docs: `docs/proposals/dissidia-battle-audio.md` (the full family spec),
`docs/research/how-mods-make-mechanics-accessible.md` §3d and §6 (where the encoding comes
from).

## The problem this solves

Speech is discrete and slow. It can say "locked, 30 metres away" but it cannot say it
thirty times a second, which is what a continuous bearing needs. So the app grows a third
sound, beside the game's own audio and speech: a tone it generates itself.

## The encoding

Taken from the reviewed mods, and in particular from the owner's own request:

- **CENTRED, not panned.** "since we have lock on, maybe just have it centered, and have a
  slightly low pitched beep that gets faster the closer we are to the targeted entity."
  A lock-on cue does not need to say which way to turn — the game already turns you. This
  is also why the beacon does not use the pan channel, even though the spec's general rule
  allows it: the player's stated preference wins over the general rule in a lock context.
- **IDENTITY IN TIMBRE.** The enemy is the low, duller carrier (660 Hz); the EX core is the
  high, brighter one (1180 Hz). Two different sounds, so both stay identifiable when the
  pulse rate is the same — the case that broke pulse-rate identity.
- **DISTANCE IN RATE.** The pulse quickens from ~1.6 Hz at a far corner (45 world units) to
  ~11 Hz in melee (4 units). Rate never carries identity.
- **SILENCE IS A CUE.** No battle, or no lock, means no tone. A battle with no lock is
  quiet rather than a resting hum, so "I hear nothing" always means "nothing is locked".
- **GAIN RISES AS THE TARGET CLOSES**, but never to zero at the far end — silence there
  would be indistinguishable from no lock at all.

## Why the adapter reports a snapshot instead of beeping

The spec's own note said the Adapter interface had no per-frame cue query, and that
`CmdLock` carries a BEACON CONTRACT comment saying so. That gap is now closed:

- `oga::CueSnapshot` (Core/adapter.h) — four plain fields: battle, locked, is_core, dist.
  No strings, no speech.
- `Adapter::cue_snapshot` — an OPTIONAL trailing member. NULL means "this adapter has no
  cue data" and the host stays silent. Trailing so the existing 8-field initializers keep
  compiling and value-initialize it to nullptr.
- `poke_cue_snapshot()` (pokecore.cpp, declared in pokecore.h) — returns 0 (nothing), 1
  (battle, no lock), 2 (enemy), 3 (EX core), and writes the distance.

⛔ **It deliberately does NOT attach the adapter.** It is called every frame, long before
the player has asked the reader anything; attaching there would build the adapter's host
against a RAM image whose structures do not exist yet — the exact failure the lazy attach
in `poke_command()` exists to avoid. The cue therefore begins once the first command has
attached the reader, which is when play actually starts.

⛔ **0 is the fail-closed value.** A host that gets 0 stays silent rather than beeping at
nothing, and a snapshot that is refused CLEARS every field, so a caller can never read a
stale distance from the previous battle.

## Why the tone is mixed into the emulator's own audio graph

The session already owns an `AVAudioEngine` with an `AVAudioSourceNode` render callback
pulling the emulator's PCM. The synth renders into that same buffer, after the core's
frames land and only over the frames it actually produced — writing into a starved tail
would put a tone exactly where the existing fade-out is trying to remove a click. One
output path, so the cue and the game can never drift apart.

## What is deliberately not here yet

- **The other three families** the spec names — own-HP tone, EX/meter tone, timer tick. The
  voice pool is sized for them (`CueSynth.Family`), so adding one is a new update function,
  not a re-architecture. Two of the three are blocked on RAM that is not mapped: the battle
  timer has no address anywhere in the adapter, and the melee-range constant for the
  hysteresis rule needs RE plus playtesting.
- **Per-family levels reachable mid-fight.** The spec's CFC2 scheme was four keyboard keys
  found by feel; a touchscreen has none. Today the only control is the Settings switch. A
  finer control (a rotor action or a gesture) is worth adding once the cue is playable.
- **Ducking is wired but crude.** `CueSynth.setSpeechDucking` exists and lowers the cue to
  35%; nothing calls it yet, so the beacon does not yet duck under speech. Wiring it needs
  the speech engine to report start and stop, and the spec already notes the platform
  cannot see VoiceOver's own reading — so a cue may still talk over a VoiceOver utterance.
  That limitation is inherited, not introduced.

## How to judge it

This is the one item in the plan the owner can evaluate himself, by ear, with no sighted
helper: play a battle, lock on, and listen. The questions worth answering are whether the
rate reads as distance, whether the two timbres are distinguishable, whether 11 Hz is too
fast or not fast enough in melee, and whether the far-end gain is audible enough to know a
lock exists at all.

The acceptance bar in `dissidia-battle-audio.md` §4 is unchanged: a full battle, start to
finish, using only cues plus the existing on-demand speech.

## One-shot event fields: the health-beep family (kh BBS, 2026-10-24)

The sound-first pass (the owner's rule: never speak what the game already cues with a
sound; in high-action play prefer sounds) adds a second, EVENT-shaped family beside the
continuous beacon. Where the beacon is a state ("locked, this far"), these are moments
("a hit just landed") that the adapter detects from memory edges and must deliver
exactly once. They ride the same snapshot as trailing fields — `foe_beep` /
`own_beep` (with `foe_beep_hz` / `foe_beep_pan` and the own-side pair) and the
gauge-full pulses `focus_cue` / `dlink_cue` — under the same contract as `core_gain`:

* The adapter's watches stash events during `on_frame`; the snapshot copies them out
  and CLEARS them, but only on a poll that passes its field gates — a refused poll
  consumes nothing. The core exposes them through `poke_cue_health()` (a bitmask plus
  the foe/own pitch and pan out-params), separate from `poke_cue_snapshot` so the
  beacon's ABI stays untouched.
* The host polls once per frame after the frame step. Polling twice in a frame is
  safe (the second poll sees silence); polling slower than the frame rate coalesces
  simultaneous hits on the two sides into one frame's fields, which is the correct
  granularity — the two sides have separate fields precisely so a traded blow sounds
  both beeps.

**What the app side must do with them** (the adapter half is implemented and
host-tested; this half is specified here because the synth's Swift/AVAudioEngine side
cannot be exercised from the Core tests):

* Bit 1 (foe beep): a bright sine tick at `foe_hz`, ~35 ms with a fast decay, panned
  to `foe_pan` (+0.8: the foe's side is the RIGHT). Bit 2 (own beep): a duller thud —
  sine at `own_hz` through a lowpass, or a sine with a faster-decaying harmonic —
  ~55 ms, panned to `own_pan` (−0.8: LEFT). The pitch already encodes the health
  fraction logarithmically (foe 1200→300 Hz, own 800→200 Hz); the synth must play the
  frequency it is given, not re-map it.
* Bit 4 (Focus full) / bit 8 (D-Link full): the gauge-full earcons — short bright
  blips in the beacon family's carrier register (Focus 1560 Hz, D-Link 1180 Hz),
  centred, ~60 ms. Fixed recipes: the snapshot carries no parameters for these
  because nothing about them varies.
* All four belong to the battle-cue family for settings purposes: the existing
  Settings "battle cues" switch gates them together with the beacon, and the
  per-family level discipline in §5 applies — the health beeps get their own level,
  the gauge-full earcons share the beacon's readiness family level. The adapter-side
  speech/sound split for gauge-full moments (kh BBS's `KhbbsHudGaugeCues`) is a
  separate axis: it decides whether the event is an earcon or a spoken line at all.

The general shape for future adapters: hit/health events are one-frame fields on the
snapshot (adapter detects, core carries, synth sounds), never speech callbacks, and
never a second polling channel.

### App-side implementation (2026-10-24)

The app half above is now BUILT on both platforms, sharing the recipes and the
numbers. What each side does when an adapter fires an event:

* **Core bridge first — one snapshot per frame.** An adapter's `cue_snapshot` is
  not a pure read: it consumes the one-shot fields as it fills them (and
  Dissidia's EX-gain test advances its previous-gauge word per call). When each
  `poke_cue_*` entry point took its own snapshot, the first poll of a frame ate
  what the later polls exist to voice — iOS's field poll silently discarded the
  health beeps and Dissidia's EX-gain pulse. `pokecore.cpp` now caches ONE
  snapshot per frame (keyed by frame counter + adapter; see `CueSnap`) and all
  three entry points answer from it, in any order. That cache is also what makes
  `poke_cue_snapshot`'s state 4 reachable again end-to-end; on the Swift side,
  `updateBeacon`'s guard now admits state 4 as its own comment always described
  (the pickup blip rides the beacon voice for the frame it is reported).
* **iOS** (`CueSynth.swift`, `GameSession.refreshCue`): the session polls
  `poke_cue_health()` every frame, ahead of the beacon branches (one of them
  returns early), and hands the bitmask + pitch/pan to `CueSynth.updateOneShots`.
  Four dedicated voices sit outside the family pool — a traded blow must sound
  both beeps at once — riding the family levels (both hit beeps on
  `Family.health`, the earcons on `Family.beacon`). Recipes as specified above:
  foe tick 35 ms pure sine, hard decay; own thud 55 ms with a faster-decaying
  second harmonic; earcons 1560/1180 Hz, centred, 60 ms. Pitch and pan are
  played exactly as delivered. Gated by the existing battle-cues switch
  (`cueBeaconEnabled`, also `audioEnabled`), off by default.
* **Android** had NO cue synth at all — the beacon itself was unimplemented
  there, whatever the library text claimed. Built now, mirroring iOS:
  `PspAdapterHostCuePoll` takes one snapshot per frame and answers beacon +
  field + health from it (this host has no legacy three-call ABI to keep);
  `PspAccessibilityScript.cuePoll()` carries it over JNI;
  `PspGameSession.pollCues()` drives `PspCueSynth`, a Kotlin port of the same
  encoding (beacon and field beacon included, same constants), which mixes
  into the game's own PCM at the session's audio drain — the one-output-path
  rule at Android's seam. The gate is a new Settings switch, "Battle cue
  sounds" (`CueSettings`, SharedPreferences `battle_cues`, default OFF,
  matching iOS's default), read per frame so flipping it mid-session lands
  on the next frame.

**Verification, stated exactly.** Core: the full host adapter suite passes,
including the Android launcher contract test (which pairs every Kotlin
external against its JNI symbol — the cue poll makes 19) and its mutation
suite; `pokecore.cpp` compiles clean with the cache (the local tree's two
drifted headers, `oga_core.h`/`snes_core.h`, supplied from current copies
for the compile only — a pre-existing tree gap, not part of this change).
The Android JNI/host C++ compiles clean against JDK JNI headers with a stub
`android/log.h` (one pre-existing JDK-vs-Android signature quirk in
untouched bridge code aside). The one-shot envelope/pan math was proven
numerically by a C++ mirror of the synth (the cue-capture precedent):
foe tick right-dominant and ~35 ms, own thud left-dominant and ~55 ms,
traded blow sounding both sides, earcon exactly centred, voices
self-silencing with zero residue, and the 300→1200 Hz pitch ratio
preserved. **Not proven anywhere yet:** the Swift and Kotlin sources
themselves have not been compiled (this machine has no swiftc, no
Kotlin compiler, no Xcode, no Android SDK), and no human has heard a
beep from either app. What stands between here and the owner hearing
one: an Xcode build of the iOS app (CI does this on push — this batch
is local and unpushed), an Android build on the Legion (SDK lives
there), and a battle on a real device with the battle-cues switch on.

## Per-game settings (2026-10-24)

Both values this doc's features answer to are now per-game settings
(the player's design): the battle-cues switch that gates this whole
synth, and the gauge-full sound/speech split the one-shot fields
above implement. The model — global defaults in Settings, per-game
overrides in a panel on each game's library page, resolution at
session start (per frame for the app-side gate, once at start for
the adapter seam) — the store keys, and the checklist for exposing a
new setting live in docs/design/per-game-settings.md.
