# Dissidia battle audio-cue spec (proposal)

Status: SPEC. Nothing in this file is built. Section 3 marks what guest-RAM
state is already verified, what is mapped but needs re-confirmation for
per-frame use, and what is still unmapped. Do not build cue families on
unmapped state.

Design rule, inherited from CFC2: everything in-battle is an audio cue;
speech stays for menus and on-demand queries only. Cues layer under the
existing pause speech; they never replace it.

Formatting rule: no tables in this doc. Lists only.

## 1. Cue families

- Lock-on beacon (family: beacons).
  - Stereo pan follows the target's X position (CFC2 rule).
  - Per-side identity: enemy and EX core use distinct TIMBRES, so they stay
    distinct even when both are near the centre.
    ⛔ RESOLVED (was an open question). This used to say "distinct pulse rates",
    which cannot work: rate also carries distance, so a near EX core and a far
    enemy pulse alike. Two reviewed mods settle it independently —
    docs/research/how-mods-make-mechanics-accessible.md §6 and §3d:
      * Zomboid Access keeps rate for DISTANCE and gives each KIND of
        information its own sound (threat = a low double thump, weapon range =
        a wood-block tick, destination = a soft bell, escape route = two
        rising whistles).
      * FFXII keeps cadence for proximity and gives its route beacon and its
        in-battle target beacon DIFFERENT timbres, switching by context.
    So: IDENTITY IN TIMBRE, DISTANCE IN RATE. Rate stays a proximity channel
    only; the two sides never share it. (CFC2's "pluck rate" note is the older
    scheme this supersedes.)
  - Elevation into pitch (PD hostile-targeting rule): 900 Hz carrier. Each
    chirp plays the carrier for its first half, then the elevation result
    for its second half: toward ~600 Hz when the target is below aim,
    900 Hz when aligned, toward ~1500 Hz when above. Clamp at 45 degrees
    and smooth toward the new target per observation.
  - Proximity into cadence (PD rule): chirp rate quickens as distance
    closes. At or inside melee range X, switch to a continuous tone.
    Exit-only hysteresis at 1.1X: once continuous, stay continuous until
    distance exceeds 1.1X, so collision and animation jitter around X does
    not flicker the cue.
  - Lock off means beacon silent. This matches the adapter's existing
    "Lock off" state.
- Own-HP continuous tone (family: health).
  - Low, hollow continuous tone (CFC2 rule). Presence or gain tracks the HP
    fraction; the exact mapping (thresholds vs smooth ramp) is SPEC and will
    be tuned by ear.
- Enemy HP: on-demand speech only, via the existing battle query (NextAlly
  in battle already reads foe HP, bravery, and distance). No continuous
  enemy tone: it would clash with the own-HP tone and the beacon. SPEC.
- EX and bravery tone (family: meter).
  - Soft, high tone tracking EX charge (full = 10000.0). Tone fills or
    brightens as EX nears full, with a distinct blip at EX full. Bravery
    value stays on-demand speech alongside enemy HP. SPEC, tune by ear.
- Clock and timer tick (family: tick).
  - Periodic tick while the battle timer runs; rate or pitch steps up in
    the final seconds. BLOCKED on RAM research: no timer address is mapped
    yet (see section 3).

## 1b. Cue rules inherited from the reviewed accessibility mods

Recorded here because they are the reason the encodings above are shaped as they
are. Source: docs/research/how-mods-make-mechanics-accessible.md.

- **One sound family per KIND of information.** Each family gets its own timbre and its
  own persistent on/off switch, and every change is spoken. Zomboid Access is the
  reference: threat position, weapon range, destination, escape route, a control loop as
  two pitches, a discrete event, speaker identity — seven families, seven sounds.
- **Identity in timbre; distance in rate.** Never share rate between the two.
- **Pan carries left/right through the full 360°; a SEPARATE channel carries front/back.**
  FFXII: a sound behind is quieter, duller, and about a fifth lower in pitch, while left
  and right keep working all the way round. The two channels are independent, which is
  what makes behind-and-left expressible.
- **Silence is a cue.** FFXII re-plans its route without narrating it, and Zomboid's
  driving tone is silent when you are on line. Do not announce a cue's own change.
- **Hysteresis on every threshold.** The exit-only 1.1X rule above is the same shape as
  SF6's arrival handling: confirm from the game's own prompt rather than a distance that
  can jitter across the line.
- ⛔ **A distance that "looks clear" is not a path.** SF6 measured that a 1.6 m alley 6 m
  away subtends about 5°, so three 90° beams miss it and even 24 rays do. Never let a
  proximity reading stand in for a passage.

## 2. Host-side implementation notes

- All cues are synthesized host-side (iOS AudioUnit, Android Oboe or
  platform equivalent). They are NEVER in-engine voices and NEVER guest
  writes. The adapter contract stands: read-only RAM plus speak, log, and
  real button presses (adapter.h). Cues change feedback only, never aim,
  damage, or movement.
- Interface gap, VERIFIED in source: the current Adapter interface has no
  per-frame cue query (Dissidia OnFrame now runs its menu/board speech
watch, still no cue query), and CmdLock carries an
  explicit BEACON CONTRACT comment: per-frame beeping needs a query API
  the interface does not provide yet. Building cues requires extending the
  interface, for example with a poll function returning a battle snapshot
  (own HP, foe HP, positions, lock state, timer). That extension is SPEC
  and not yet designed.
- Cue mixing: fixed small voice pool, one voice per family, plus beacon
  voices for lock candidates with a fixed cap (PD used ten slots; Dissidia
  needs fewer: enemy plus cores). No runtime allocation on the audio path.
- Gain-not-skip (CFC2 rule): a muted family still runs its voice and
  smoother, so muting fades instead of clicking.
- Per-family levels (CFC2 rule): one level per cue FAMILY, not per side.
  Lock candidates share the beacon level, so turning one side down can
  never make the pan readout lie about which side is which.
- Levels wrap 0/25/50/75/100 with key-up/key-down both wrapping. Default
  100, so one press mutes: the reason to mute is usually that the cue is
  in the way right now. Every change is spoken; speech is the only
  feedback, so an unspoken change is an invisible one.
- Levels persist across restarts. CFC2 used cfc2_audio.ini; the OGA mobile
  equivalent (NSUserDefaults, SharedPreferences, or an app settings file)
  is SPEC and undecided.
- Mobile bindings are SPEC and undecided: CFC2 used F5-F8 plus shift as a
  four-pack the user finds by feel mid-match. Touchscreens need an
  equivalent (gesture, rotor action, or settings sliders). Needs owner
  input before building.
- Cues duck under speech: cue gain drops while a speech utterance plays
  and restores after, so cues never mask pause speech. SPEC.

## 3. RAM research prerequisites

- Verification standard, from docs/reverse-engineering/fe11.md: a mapping
  counts as confirmed only when two independent sources agree, typically
  the decompilation AND a live read of the running game. Below, VERIFIED
  means the adapter source comments claim that bar is met; UNMAPPED means
  not yet traced; SPEC means proposed but unproven.
- Own and enemy HP: adapter maps S = [P+0x51C], HPmax = u16[S+8], damage =
  u16[S+2], HPcur = max(0, max minus damage), via BM = [0x08B955A0],
  P0 = [BM+0x14], pair = [P0+0x2F0] (ANSWER8/8B; source comment says
  validated live including defeat state). Status: VERIFIED in source, but
  re-confirm with a live read at cue-build time, because cues will read it
  every frame rather than on demand.
- Own and enemy positions: floats at P+0x80, P+0x84, P+0x88 (same
  ANSWER8/8B chain). Status: same as HP.
- Bravery and EX: s16[S+0x0E] bravery, s16[S+0x10] base, float[S+0x14] EX
  with full at 10000.0 (same chain). Status: same as HP.
- Lock-on target: P+0x2EC, where null means off, equal to P+0x2F0 means
  enemy, and anything else is an alternate object exposed only while
  present in the M+0x0C/+0x490 list, which is the lifecycle rule for a
  consumed or retired EX core (ANSWER9 s114; source comment says validated
  live). Status: same as HP.
- Battle timer: UNMAPPED. No timer or clock address appears anywhere in
  the adapter. The tick family cannot be built until the timer is traced
  to the two-source standard. Candidate lead, SPEC: the HUD draw tick
  cited in the adapter for DP cache refreshes may also touch the timer;
  needs real RE.
- Melee-range constant X for the hysteresis rule: UNMAPPED for Dissidia.
  PD used the unarmed weapon definition's real melee range (60 world
  units). Dissidia attack ranges differ per character and move, so SPEC
  options are a fixed world-unit constant found by RE, or proximity bands
  relative to lock distance. Needs RE plus playtesting.
- Pause cursor: VERIFIED (W = [[0x08B98940]]+0x234, index, count, and
  stride live-tracked including wrap). Already drives pause speech. The
  cue layer must duck under it, never fight it.

## 4. Acceptance criteria

- Blind-playable battle test: the owner completes a full battle, start to
  victory or defeat screen, using only cues plus existing on-demand speech
  (pause menu, self, foe, and lock queries), with the screen unavailable.
- During that battle the player can tell, without a sighted helper:
  - whether lock-on is on, and whether it is the enemy or the EX core;
  - rough direction, elevation, and distance of the locked target;
  - own HP danger level;
  - EX readiness;
  - time pressure (once the timer is mapped).
- Per-family mute check: muting each family fades it with no clicks, the
  new level is spoken, and the setting survives a restart.
- No-regression check: existing pause speech, board cursor, and DP flows
  behave exactly as before with cues enabled. Cues add no RAM writes and
  no input changes.
- Out of scope for v1: multi-enemy triangulation (battles here are 1v1
  plus cores; slot-cap policy still TBD), assist and summon cues, and menu
  echo during battle overlays.
