# Announcement queue: priority, interruption, coalescing

Status: design (no ambient/per-frame cue ships until it passes the rules below).

The mechanism layer (`say(text, interrupt)`, VoiceOver queue flag vs
`AVSpeechSynthesizer`, stop state) is documented in
`announcements-vs-live-regions.md`. This doc is the policy layer: **what may
speak, when it may interrupt, and what gets merged or dropped.**

## Priority levels

| Level | Source | Examples |
|---|---|---|
| `critical` | App UI, errors | ROM failed to load, setting changed, core crashed |
| `requested` | Player pressed a query key | Where-am-I, next/prev enemy, menu re-read, stop |
| `event` | Game state changed discretely | Dialogue line advanced, battle started/ended, HP crossed a threshold, item picked up |
| `ambient` | Continuous game state | Player coordinates every frame, enemy footsteps, arena hum |

Higher levels always win. Within a level, newer replaces older (see coalescing).

## Interruption matrix

- `critical` interrupts everything.
- `requested` interrupts `event` and `ambient`, never `critical`. Two
  `requested` in a row: the second interrupts the first (the player asked
  again; the old answer is stale).
- `event` never interrupts `critical` or `requested`. It interrupts `ambient`.
  An `event` arriving while `requested` is speaking waits — unless it is the
  *answer* to that request, in which case it inherits `requested`.
- `ambient` never interrupts anything. If anything else is speaking or queued,
  `ambient` is dropped, not queued. There is no backlog of position updates.

## Coalescing rules

Per-frame data must collapse before it reaches the speaker:

- Same-key replacement: only the newest reading of a key survives
  (`player-pos`, `enemy-hp:<id>`). No queue of stale positions, ever.
- Rate cap: `ambient` keys speak at most once per 2 seconds, and only if the
  value changed enough to matter (position moved a tile, HP crossed 25/50/75%).
- Thresholds, not streams: HP is announced when it crosses a band boundary,
  not on every hit. Footsteps are game audio, not speech — never narrated.
- Silence wins ties: if a coalesced line is still waiting when its key updates
  again, the old text is discarded without speaking.
- `once`: identical text never speaks twice in a row; repeats reset the
  rate-cap clock instead of queuing.

## User-requested announcements

These are `requested` level and always available, per system:

- Where-am-I: self position plus nearest landmark (arena edge, door, platform).
- Opponent query (next/prev enemy): direction + distance + name/HP band — **on
  demand only**. Enemies already make movement noise through game audio; the app
  does not narrate them unprompted.
- The Dissidia/DBZ adapters expose these as commands 0–4 (`WhereAmI`,
  `NextAlly`, `PrevAlly`, `NextEnemy`, `PrevEnemy`); other adapters map the same
  commands onto their own state.

## Lock-on beacon (audio, not speech)

With lock-on active, an optional beacon marks the target without spending the
speech queue. One tone carries the vector (per the BT2 mod's proven encoding,
see `docs/research/bt2-accessibility-lessons.md`):

- Stereo pan = left/right, pitch = forward/back, repeat rate = distance.
  (Lock-on still centers the *meaning* — the pan is small, a nudge, not a mix.)
- Slightly low-pitched beep; a distinct arrival figure when in range.
- Degraded timbre (hollow harmonic) when the bearing is honest but the target
  is unconfirmed — confidence is audible, not read out.
- Silent when there is no lock, the target is dead, or the player asked for
  silence. It never interrupts speech; speech ducks or mutes it.
- Off by default until playtested against the matrix above.

## What stays off

Passive/ambient speech ships OFF. It turns on per game only after a playtest
shows: no interruption of requested lines, no stale-queue buildup over 10
minutes of play, and the coalescing caps holding under fast state churn. Until
then the app speaks events and answers, and nothing else.
