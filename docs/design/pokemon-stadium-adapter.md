# Pokémon Stadium — adapter design (spec ahead of the core)

Status: **design only.** Open Game Access has no N64 backend today, so no
adapter code ships yet. This document is the transcription source for the
adapter when a core lands: every address and format below is crowned —
measured on the running game and verified against the screen — in
`docs/reverse-engineering/pokemon-stadium.md`, which is the evidence record
behind this spec. Anything not proven there is listed under "Not yet known"
and the adapter must stay silent about it, per the house rule: model the
game, not the screen; announce only what the state proves.

## Game identity

- Pokémon Stadium (N64), USA Rev 2.
- ROM MD5: `6dc6820cef755fc1253d06df45c9bd2a`.
- Addresses are Rev 2 addresses. Layouts may transfer to other revisions;
  addresses do not (the project's BSS-shift lesson from the PSP work applies
  here too — re-derive per build, never inherit).

## State the adapter reads

### The party store (both teams, all six)

Player block at `0x800AE690`, foe block at `0x800AE8AC`; six records each,
stride `0x54`, in store order (not pick order). Per record:

| Offset | Field |
|---|---|
| +0x00 | current HP (u16, little-endian) |
| +0x03 | species id → name via the Gen 1 table |
| +0x06 | status byte (below) |
| +0x07 | level |
| +0x08/+0x09/+0x0A/+0x0F | move ids, slots Down/Right/Up/Left (Gen 1 indices) |
| +0x20..+0x23 | current PP, in **Left, Down, Right, Up** order — not slot order; mind this |
| +0x24 | max HP |

Status byte: sleep = low bits (turn counter), `0x08` poison, `0x10` burn,
`0x20` freeze (encoded, never yet observed live), `0x40` paralysis.
Poison and burn are verified live against the panel; sleep and paralysis
likewise.

The active battler is the record whose HP matches the HUD word; records keep
their own Pokémon across switches, so bench speech reads the store directly.

### Battle state words

- HUD HP (u32): player `0x800FCC58`, foe `0x800FCC5C`. These lag nothing and
  anchor the dialog signature below.
- Battle-phase word `0x80285294`: 0 = command menu, 1 = move menu,
  2 = switch screen, 3 = turn executing. Garbage outside battle — **gate all
  battle speech on it.**
- "Keep battling?" dialog word `0x800A85D0`: 0 = Continue, `0xFFFFFFFF` =
  Save and Quit. It lags the cursor and can read −1 spuriously at battle
  intro; trust it only with the dialog signature (player HUD HP > 0 AND foe
  HUD HP == 0 AND hand words in range, X 100–130, Y 104–110). At a
  confirmation snapshot, hand X == 123 alone authorizes reading it as Save
  and Quit. This signature banked battles 1–3 via Save and Quit.

### Stat stages (player's active battler)

Array `0x80285318..1F`, eight bytes, stage + 7 encoding; mirror at
`0x801E8B0C` (can lag a turn across switches — read the main array).

| Index | Stat | Effective word |
|---|---|---|
| 0 | Special | `0x802852EE` |
| 2 | Defense | `0x802852EA` |
| 3 | Attack | `0x802852E4` |

Indices 1, 4, 5, 6, 7 (Speed, Accuracy, Evasion among them) and the foe's
stage array are **not yet mapped** — see "Not yet known". Speech about a
stage change is allowed only for indices 0, 2, 3 until the rest are crowned
the same way (a move stepping exactly one index, the effective word
laddering by the stage multiplier, HP untouched).

## Speech formats

Turn start (phase word 0, on entry):

> "Your Charmander, 111 of 126. Enemy Rhyhorn, paralyzed, 44 of 152."

Species, status, current/max HP — from the store plus the HUD words.

Move menu (phase word 1):

> "Flamethrower, 24 PP. Slash, 20. Dig, 10. Fire Spin, 15."

The four moves by name with PP left, from the active record (mind the PP
order quirk above).

Bench queries (on demand, both teams): every record's species + HP, e.g.
"Bulbasaur 132 of 132. Ivysaur 95 of 141." CPU teams show reversed nicknames
on screen; speech always says the species.

Events, announced from state changes only: damage dealt/taken (HP deltas),
a status landing (status byte transition), a faint (HP reaches 0 + the
active slot changes), a switch, a stat-stage change (stage array diff, known
indices only). Anything the state cannot prove — a miss versus an immunity,
why a move failed — stays unspoken.

Between battles: the "Keep battling?" prompt speaks its two options with the
selection, but only under the dialog signature above.

## Cues (OGA pattern)

- A soft chime when the command menu is waiting (phase word 0): the game
  otherwise sits silent, so a blind player needs an "it's your move" signal.
- A low-health warning tone off the crowned HP words.

Both are cheap because the underlying words are already crowned; levels and
shapes follow `docs/design/cue-synth.md`.

## Not yet known (the adapter stays silent on all of these)

- Confusion — never observed. No trainer through battle 4 carries a fishable
  confusion move; the hunt climbs the cup ladder, reading each trainer's
  full six first and fishing only when a confusion move is present.
- Freeze (status bit encoded, never seen live) and substitute.
- The foe's stat-stage array, and stage indices 1, 4, 5, 6, 7.
- The continues-remaining count (the dialog's "Continue = 1" line is static
  text, not the count).
- Road screens: division-select cursor, Gym Leader Castle position, the VS
  Mewtwo entry screen, the rental grid's already-picked state.
- The second send-in's input rules (battle 4 blocker): the first send-in
  works with one D-pad Down per fainted pick above the target, then A; the
  second send-in has never landed with that pattern, and the pick screen's
  actual input semantics are unproven. No adapter behaviour depends on this
  — it is bench knowledge, recorded here so it is not re-derived.

## Acceptance

The Dissidia standard, from the completion plan's Phase 5: a blind player
completes the Poké Cup Poké Ball division and the first Gym Leader Castle
gym screen-unavailable, on speech + cues alone. Every moment the player had
to guess becomes a gap item against this spec. Until an N64 core exists in
OGA, this document and the evidence record are the deliverable.
