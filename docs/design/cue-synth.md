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
