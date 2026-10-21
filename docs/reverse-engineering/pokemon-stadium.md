# Pokémon Stadium (N64, USA Rev 2) — reverse-engineering notes

Status: **DATA / SPEC, no adapter yet** (2026-10-20). OGA has no N64
core, so nothing here ships in the app today. What exists is a
screen-verified memory picture of the battle and the road to it,
gathered in a custom mupen64plus bench harness (outside this repo),
plus the completion plan the data serves: a blind player completing
Stadium (cups, Gym Leader Castle, VS Mewtwo, Round 1) on speech and
cues alone, rentals only. When an N64 core lands, this doc is the
adapter's transcription source.

Working notes behind this summary (session-by-session evidence,
bench traps, dead ends): held with the bench, summarized here to the
claims that survived screenshot verification.

## Identity

- Pokémon Stadium (USA), Nintendo 64 — the 3D battle game built
  around the Game Boy titles.
- Development image: **Rev 2 ("V1.2")**, MD5
  `6dc6820cef755fc1253d06df45c9bd2a`, held OUTSIDE this repository
  per the no-ROMs rule. **Every address below is Rev 2 only** — the
  Rev 1 / decomp layouts differ and must not be mixed in.
- Bench: custom harness around mupen64plus (scripted controller
  input, RAM snapshots, screenshot capture) driving the real game
  end to end. Two methods from this bench are load-bearing:
  - **The supervised method:** scripted runs kept losing races
    against the game's own timing (a late scripted press would
    answer a dialog nobody intended). The fix is a supervisor
    process that releases each input gate only on a proven memory
    state (battle phase, HUD values, dialog signature). Every
    banking result below used it.
  - **Seeded saves:** winning a battle and choosing Save and Quit
    writes a suspend save that stores cup progress only — a seeded
    boot resumes directly at the next battle with the registered
    team fully healed, PP restored. That ladder is how later
    trainers are reached in short runs.

## The road in (all screenshot-verified)

- Title → Stadium → cup select → division → rental grid → team
  pick → lineup → battle plays end to end.
- **Cup cursor:** `0x8008157C`, defaults to 5 = Poké Cup.
- **Area cursor:** u16 at `0x80117EF8`. (An earlier external report
  had the cursor pair swapped; the *other* word, 0x80114D20, is not
  the cursor.)
- **Three-way menu cursor** (Battle / Rules / Registration — one
  screen serves both the cup flow and Gym Leader Castle): byte at
  fragment-65 base + `0x114E`, 0 = Battle, 1 = Rules,
  2 = Registration. Read the fragment base from `gFragments[65]`
  each boot (observed `0x80144DA0`); do not hardcode the sum.
- **Rental popup cursor:** byte at `0x80183358` — 0 = OK,
  1 = OK to Register, 2 = Reselect some Pokémon, 3 = Reselect all
  Pokémon (last value inferred, the rest poll-verified).
- **Lineup button map** (printed on the "All ready!" screen itself):
  B = pick slot order per the screen's icons; the standard menu
  route consumes exactly 20 A presses to reach the pick screen, so
  **press 21 is the first pick** — choose its button deliberately.
  An A-heavy spam tail silently makes the A-bound Pokémon the lead;
  that artifact explained two early contradictory runs.
- At the rental popup, the party store (below) is still empty — the
  picked six live elsewhere during team select (fragment-88 data is
  the suspect); battle records are written at battle setup.

## Battle: the party store (crowned, screen-verified)

Both teams live as six fixed records each, in PICK order:

- Player block `0x800AE690`, foe block `0x800AE8AC`, stride `0x54`.
- Per record:
  - `+0x00` current HP, u16 LE
  - `+0x03` species id
  - `+0x06` status byte — classic encoding: low bits = sleep turns
    remaining, `0x08` poison, `0x10` burn, `0x20` freeze,
    `0x40` paralysis. Every value observed live against the screen
    panel **except freeze**, which has never occurred in a fished
    battle.
  - `+0x07` level
  - `+0x08 / +0x09 / +0x0A / +0x0F` move ids, slots D / R / U / L
    (Gen 1 move indices)
  - `+0x20..+0x23` current PP, in **[L, D, R, U] order** — not the
    same order as the move bytes; this mismatch is real and cost a
    run before it was pinned.
  - `+0x24` max HP
- The records are LIVE battle records: HP, status, and PP update
  in place through the battle, and a switch leaves each record with
  its own Pokémon. The active battler is whichever record's HP
  matches the HUD word.
- Battle-3 wrinkle (JUGGLER): the foe store reads half-written
  before that battle (zeroed L-slot moves, a garbage PP byte) but
  tracks the battle live once it starts — PP decrements land in the
  same records. Do not trust a pre-battle read for that fight.
  Related: the odd pair (move 118, PP byte counting down from 208)
  is **Metronome** — its PP byte starts at 208 and decrements per
  use. Exact packing left open.
- Foe nicknames in the store are reversed with a trainer initial
  appended; speech should name the species from `+0x03`, never the
  nickname.

## Battle: state words (crowned)

- **HUD HP (u32):** player `0x800FCC58`, foe `0x800FCC5C`. These lag
  nothing and are the dialog signature's anchor (below).
- **Battle-phase word:** `0x80285294` — 0 = command menu,
  1 = move menu, 2 = switch screen, 3 = turn executing. Garbage
  outside battle; gate all battle speech on it. (Sibling
  `0x80285288` carries the same values minus the 3.)
- **"Keep battling?" selection word:** `0x800A85D0` — 0 = Continue,
  `0xFFFFFFFF` = Save and Quit. **Caution:** the word lags the
  cursor and can read -1 spuriously at battle intro. Trust it only
  with the dialog signature: player HUD HP > 0 AND foe HUD HP == 0
  AND hand words in range (X 100–130, Y 104–110). At a confirmation
  snapshot, hand X == 123 (cursor on Save and Quit) alone
  authorizes the confirming press. This signature banked battles 1
  and 2 via Save and Quit; seeded boots then resumed at battles 2
  and 3 respectively.
- The dialog's "Continue = 1" line is static text, not a
  continues-remaining count. That count is still unlocated.

## Battle: stat stages (partly mapped)

- **Stage array:** `0x80285318..1F`, eight bytes, stage + 7
  encoding, for the player's active battler. A mirror sits at
  `0x801E8B0C` in the same order.
- **Index 0 = Special** (Growth probe: 7 → 8 → 9 → 10; effective
  Special u16 at `0x802852EE` laddered ×1 → ×1.5 → ×2 → ×2.5).
- **Index 3 = Attack** (three Swords Dances: 7 → 9 → 11 → 13;
  effective Attack u16 at `0x802852E4` laddered ×1 → ×2 → ×3 → ×4).
- **Index 2 = Defense** (crowned 2026-10-20 in the battle-3
  rematch: a foe Hitmonchan's Metronome rolled a Leer/Tail
  Whip-class move — index 2 stepped 7 → 6, the player's HP was
  untouched that turn, and the effective Defense word at
  `0x802852EA` fell 75 → 49, the ×2/3 stage −1 multiplier. The
  word's identity is by elimination against the crowned Attack
  and Special words and the battler's base stats.)
- **Still open:** Speed, Accuracy and Evasion among indices
  1, 4, 5, 6, 7, and the foe's own stage array (no foe self-buff
  observed yet — though this cup is full of Metronome carriers,
  and a rolled self-buff is how the Defense probe landed, so a
  long battle is a realistic way to catch the foe's array).
  The dedicated Screech probe never fired: two re-fish runs failed
  on CPU move *choice*, not access — the carrier was fielded,
  used only damaging moves, and fainted within three turns. The
  Metronome accident closed the question first.
- **Confusion: still unobserved.** No fished team so far carries a
  fishable confusion move: ROCKER's six has none (his Magnemite has
  no Supersonic — an earlier assumption, corrected), and JUGGLER's
  team leans entirely on Metronome. The hunt climbs the cup ladder
  via the seeded-save method, reading each trainer's full six first
  and fishing only when a confusion move is actually present.
  Battles 4–8 open once battle 3 is won. Substitute is likewise
  unobserved.

## Speech spec (from the completion plan, for the future adapter)

The full adapter design now lives in `docs/design/pokemon-stadium-adapter.md`
(state model, speech formats, cues, open gaps, acceptance); the summary below
is the short form.

Model the game, not the screen; announce only what the state
proves:

- Turn start: "Your Charmander, 111 of 126. Enemy Machop, burned,
  44 of 142." — species, status, current/max HP from the store +
  HUD words.
- Move menu: the four moves by name with PP left, from the active
  record (mind the PP order quirk).
- Bench queries: every record's species + HP, both teams.
- Events from state changes: damage dealt/taken, a status landing
  (status byte transitions), a faint (HP 0 + active slot change), a
  switch, a stat-stage change (stage array diff). Claims the state
  cannot prove (miss vs. immunity) stay unspoken.
- Cues: a soft chime when the command menu is waiting (phase word
  0 — the game otherwise sits silent), and a low-health warning
  tone off the crowned HP words.
- Road screens still owed speech state: division select cursor,
  castle position, VS Mewtwo entry, continues-remaining count.

## Bench traps worth the next person's time

- VM restarts wipe the bench: restore = rootfs symlinks for the
  mupen64plus core/plugins + Xvfb on :99, and runs MUST launch with
  `SDL_AUDIODRIVER=dummy` (without it the emulator crawls at
  ~1.5 fps on SDL audio sync).
- Kill stale emulator processes by scanning /proc for the exe —
  `pkill -x` misses them, and two stale emulators sharing the
  display starve a run.
- Static input scripts lose dialog races (above). Gate on memory
  state, not frame counts, wherever a dialog can appear.
- Post-win and post-save snapshots are transition garbage (the
  stage array reads patterns like [40, 165, …] there). Discard
  them; only in-battle probes count.
- Suspend-save hashes differ between banks of the same battle —
  the file carries more than cup progress. Assert a seed's hash
  *before* boot instead of theorizing about exit writes, and keep
  canonical banked copies separate from run leftovers.
