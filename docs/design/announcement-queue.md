# Announcement queue: priority, interruption, coalescing

Status: **implemented in core** (`Core/announce.h`, `Core/announce.cpp`), host-tested by
`scripts/announce-test.sh`. Not yet wired into the adapters or the iOS/Android speech engines;
see "Host wiring" below. This doc is the single spec: it supersedes the level table that used
to live here and `docs/proposals/announcement-queue.md` (kept for its evidence section).

The mechanism layer (`say(text, interrupt)`, VoiceOver queue flag vs `AVSpeechSynthesizer`,
stop state) is in `announcements-vs-live-regions.md`. This doc is the policy layer: **what
may speak, when it may interrupt, and what gets merged or dropped.**

## Why one line in flight

The earlier drafts controlled what we *hand* to the platform. That is not enough: once a line
is handed to VoiceOver or TalkBack with `interrupt=false`, **the screen reader queues it
itself**, and nothing on our side can take it back. Replacement, expiry and "silence wins
ties" all stop working at the handoff — a stale HP line sitting in VoiceOver's queue is still
read as if it were current. An Android polite live region is not a queue either: rapid text
changes can merge and TalkBack may skip the ones in between.

So the queue keeps **at most one utterance in flight** and releases the next only when:

- the host reports it finished (`announce_speech_done(id, success)`), or
- on hosts that cannot report that (TalkBack live region), its estimated duration has passed
  (`300 ms + 55 ms per byte`, tunable), or
- a watchdog fires because a done report was lost (2 x estimate + 2 s), which is logged.

Every utterance carries an id, so a late "finished" from a line that was interrupted can never
release the line that replaced it.

## Priority levels

Three levels. The old fourth level, `critical`, is gone from the queue: app UI feedback (ROM
failed to load, setting changed, core crashed) is carried by the control's own label/value or
spoken by the host directly, per `announcements-vs-live-regions.md`. It never waits in a game
queue.

- **High** — *requested*: the player pressed a query key (Where-am-I, next/prev enemy, menu
  re-read). An adapter answering a command speaks the answer as High. (The old "an event that
  answers a request inherits requested" rule had no mechanism; this replaces it.)
- **Normal** — *event*: game state changed discretely (dialogue advanced, battle started, HP
  crossed a band, item picked up).
- **Low** — *ambient*: continuous state (position, HP drift). Rate-limited and short-lived.

Within a queue, the best eligible item speaks first: highest priority, then oldest.

## Interruption

- A higher level may cut off a lower one in flight: High cuts Normal and Low; Normal cuts Low.
- A High may cut off a High **in the same group**: the player asked again, the old answer is
  stale (WhereAmI pressed twice).
- A High in a **different** group waits for the one speaking: WhereAmI then NextEnemy means
  the player wants both answers.
- Low never cuts anything off. Normal never cuts Normal.
- High is always handed over with `interrupt=true` (it may cut off the screen reader's own
  reading — the player asked); Normal and Low use `interrupt=false` unless they are cutting
  off a lower line of ours.

## Coalescing

- **Groups and replacement.** Every item names a short stable group (`"whereami"`, `"enemy"`,
  `"hp"`). At most one item per group is *pending*; a newer one replaces it. The line already
  speaking is never replaced — interrupting is the priority mechanism. A lower-priority item
  cannot replace a higher one pending in its group; it is suppressed instead.
- **Dedup keys.** Exact-repeat suppression against the last line *spoken* in the same group.
  The key carries the value (`ally:2:hp:41`), never just the label, or HP 41 → 40 is wrongly
  eaten. High items bypass dedup: the player asked, so they hear it.
- **Rate limit by time, not distinctness.** A guard that asks "is this different from last
  time" is no guard against a value that differs every frame (CFC2: 2741 gauge lines in 5
  minutes). Default minimum interval: **Low 2 s**; Normal and High none. Adapters may set
  their own per item (cursor echoes ~300 ms). Inside the window the newest item replaces the
  pending one, and only the latest speaks when the window opens.
- **Expiry.** Default 1.5 s for Low and Normal; High never expires. Measured from when the item
  became *eligible*, not when it was queued, so the rate limit never kills the settled final
  value — it only kills lines that could have spoken and didn't because something else was.
- **Thresholds, not streams** (adapter responsibility): announce HP when it crosses a band, not
  on every hit, and give bands hysteresis — an exit margin like the melee-range rule in
  `docs/proposals/dissidia-battle-audio.md` — so HP sitting on a boundary cannot flicker the
  line. Footsteps are game audio, never narrated.

## Bounds

- At most 8 pending items and 16 distinct groups per queue. Text is copied and cut at 512
  bytes on a UTF-8 boundary; empty text is rejected.
- Full queue: evict the lowest-priority, oldest item. If everything pending outranks the new
  item, the new item is refused. A new High is never the one dropped for a Low.
- Nothing blocks the frame loop and nothing allocates after creation.

## Stop

The player's stop key clears pending Low/Normal and forgets the line in flight (the host stops
the platform voice itself). Low/Normal stay silenced until the next High item, which speaks and
lifts the silence — the existing `SpeechEngine.isStopped` contract.

## Retry

VoiceOver drops an announcement posted while it is reading a focused element — and, contrary
to what this project's docs used to say, it *does* report that: the
`announcementDidFinishNotification` userInfo carries `announcementWasSuccessfulUserInfoKey`.
The host passes it through as `success`. A High line reported unsuccessful is retried once; a
Normal or Low line is not (it would be stale by then). The Repeat control and the screen
element's `accessibilityValue` remain the player-driven recovery paths.

## Play vs diagnostics

Speech is never gated by the diagnostics switch. `diag_verbose` only changes logging: one line
per decision when on, and when off a per-group rate every 10 s
(`suppressed=N in last 10s group=hp (dup a, replaced b, expired c, other d)`) — a count nobody
prints is a count nobody has. `DumpState` stays on the log path only.

## User-requested announcements

Always available, High, per system:

- Where-am-I: self position plus nearest landmark (arena edge, door, platform).
- Opponent query (next/prev enemy): direction + distance + name/HP band — **on demand only**.
- The Dissidia/DBZ adapters expose these as commands 0–4 (`WhereAmI`, `NextAlly`,
  `PrevAlly`, `NextEnemy`, `PrevEnemy`); other adapters map the same commands onto their state.

## Spoken history (repeat / previous)

The queue keeps the last `kAnnounceHistoryMax` (16) lines it actually HANDED TO THE
PLATFORM, and two player commands walk it: `RepeatNewest` and `RepeatOlder`.

WHY THE QUEUE AND NOT THE UI. The app side already had a one-line mirror
(`SpeechEngine.lastSpoken`), which answers "what did it just say" but gives a player who
missed two lines ago no path to it — reviewed accessibility mods keep 50 messages with
repeat / previous / next. More importantly a UI-side mirror records what the app was
TOLD, not what was said: a line the queue suppressed as a duplicate was never spoken, and
repeating it would be a lie. So the history is recorded in `emit()` — after a line wins
its place in the queue — and never in `announce()`.

- **Repeat is a player action, so it speaks High**: it cuts off whatever is speaking, lifts
  a stop, and bypasses dedup and rate limits. This also sidesteps the staleness rule above
  ("a Normal or Low line is not [retried] — it would be stale by then"), which does not
  apply to something the player just asked to hear again.
- **A repeat does not re-enter the history**, so walking back advances instead of sliding
  the ring toward itself.
- **A repeat re-speaks under the line's ORIGINAL group**, so it can never burn one of the
  16 group-table slots — the table is a hard cap and a repeat should not be able to
  exhaust it.
- **Consecutive identical lines collapse**, so a High retry re-speaks without leaving a
  second entry the player would have to walk past.
- **`RepeatOlder` clamps** at the oldest recorded line and returns false rather than
  wrapping; the UI says "Nothing older to repeat." itself, which is why the core stays
  silent on refusal.

⛔ BOUNDARY, STATED HONESTLY: the DS Pokémon LUA script speaks through `EmitSpeech`
directly and does not route through this queue, so its lines are not in the ring. The
script has its own repeat on its own key, which the controller layer still prefers where
it exists.

The rule is proven, not just exercised: `scripts/announce-test.sh` builds with
`SABOTAGE_REPEAT_INCLUDES_SUPPRESSED` (which moves the recording into `announce()`, i.e.
counts a deduped line) and REQUIRES the test to fail.

## Lock-on beacon (audio, not speech)

The beacon never uses the speech queue. The per-game encoding lives with the game
(`docs/proposals/dissidia-battle-audio.md` for Dissidia). General rules:

- Stereo pan = left/right (a nudge, not a hard mix), repeat rate = distance.
- Pitch = **elevation** in 3D arena games (Dissidia); forward/back in games without
  meaningful elevation (the BT2 encoding, `docs/research/bt2-accessibility-lessons.md`).
- ⛔ RESOLVED (2026-10-05): the Dissidia spec used pulse rate to tell the enemy from the EX
  core, and rate cannot carry both identity and distance — a near core and a far enemy pulse
  alike. Settled from the mod research rather than by playtest: **identity in timbre, distance
  in rate.** Two reviewed mods agree independently (Zomboid Access gives each kind of
  information its own sound and keeps rate for distance; FFXII keeps cadence for proximity and
  gives its route and in-battle beacons different timbres). See
  docs/research/how-mods-make-mechanics-accessible.md §3d and §6, and the updated lock-on
  bullet in docs/proposals/dissidia-battle-audio.md.
- Degraded timbre when the bearing is honest but the target is unconfirmed.
- Silent with no lock, a dead target, or player-requested silence. Speech ducks it — but only
  our own speech: with VoiceOver on, the app cannot see VoiceOver's own reading, so cues may
  still collide with it.
- Off by default until playtested.

## Host wiring (not done yet)

Sequenced; each step is host-testable except the last line of each platform.

1. **Core owns the queue.** One `AnnounceQueue` per `PokeCore`, created at attach with
   `AnnounceSink::speak` -> the existing `speechCb` path (the sink remembers id->text so no
   speech-callback signature change is needed) and `host_reports_done=false` (estimate
   pacing: safe on both platforms until completion hooks land). `announce_tick()` once per
   frame from `poke_frame()`; `announce_stop()` from the stop key. Additive C ABI:
   `poke_announce_done(core, id, success)`.
2. **Adapters** call `oga::announce()` with per-site group/priority instead of `Host::speak`.
   The `Host` gains a clock (`uint64_t now_ms`) — every literal `Host` initializer in the
   host tests grows one field in the same commit. Migrate Dissidia first (it has the host
   tests); FE/GBA/DBZ follow. Until a platform reports done, pacing is by estimate, which
   the host tests assert with a fake clock.
3. **iOS** (`SpeechEngine.swift`): the class already conforms to
   `AVSpeechSynthesizerDelegate` but implements no delegate methods — add `didFinish`
   (success) / `didCancel` (not) -> `poke_announce_done`. VoiceOver path — observe
   `UIAccessibility.announcementDidFinishNotification`, map the announced string back to
   its id, pass `announcementWasSuccessfulUserInfoKey` as `success`; until that hook
   exists the VoiceOver path stays estimate-paced. `announce(_:)` for UI replies bypasses
   the queue. Needs on-device proof with VoiceOver on and off.
4. **Android**: there is no `AccessibilitySpeech.kt` — the app is a WebView+TTS shell
   (`MainActivity.kt`, 71 lines) with NO native adapter bindings at all, so the first step
   is plumbing `poke_command`/`poke_adapter_ready` through JNI and routing adapter speech
   to TTS. Only then: TTS path passes the id as `utteranceId` with
   `UtteranceProgressListener.onDone`/`onError`; TalkBack live-region path runs with
   `host_reports_done=false` (no signal exists). Needs on-device proof with TalkBack on
   and off.
- `announce_speech_done()` is thread-safe; everything else runs on the frame thread, and
  `announce_tick()` is called once per frame.

## What stays off

Passive/ambient speech ships OFF. It turns on per game only after a playtest shows: no
interruption of requested lines, no stale-queue buildup over 10 minutes of play, and the
coalescing caps holding under fast state churn. Until then the app speaks events and answers,
and nothing else.
