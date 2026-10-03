# OGA announcement queue

Status: **superseded** by `docs/design/announcement-queue.md` (implemented in `Core/announce.*`). Kept for section 1, the evidence. Where the two differ, the design doc wins: notably one line in flight, three levels with same-group-only High interruption, Low interval 2 s, and expiry measured from eligibility.

Original status: proposal. Problem: `Host.speak(text, interrupt)` in `Core/adapter.h` is a bare wire from adapter to platform speech. It has no priority, no replacement group, no dedup key, no expiry, no player context. Every lesson from CFC2 and pd-access says this wire will flood, drop play speech when diagnostics are silenced, and speak stale lines as if they were fresh.

## 1. Problem, with evidence

- From CFC2 (CLAUDE.md, 2026-09-19, "speech lagged keypresses"): a change-guard on counters that always change (`fed == last_fed`) never suppresses. One session produced 2741 gauge lines in 5 minutes, one every ~25 ms, each a file write from the speech worker. The same shape recurred in `gt_poll_and_log`, where "log only distinct strings" passed every redrawn number ("188", "191", "198"). Rule carried over: a guard asking "is this different from last time" is no guard against a value that differs every time. OGA adapters that read HP, positions, or step counters per frame will hit this on day one.
- From CFC2 (same session): one `gt_ok` switch gated 18 call sites with opposite audiences. Three were shipping play speech (quit/consent dialogs, hints, per-row captions); fifteen were development reports. The only way to stop the flood was to turn the hook off, which silenced the quit dialogs. Fix was decoupling: `cfc2_guitext.on` (play) vs `cfc2_guitext_diag.on` (diagnostics). OGA today routes adapter speech and the debug reading log through adjacent paths (`Host.speak` vs `Host.log` into `GameSession.debugLines`); without an explicit play/diag split, silencing diagnostics risks silencing play again.
- From CFC2 (same session): `gt_focus_report` cleared the caption window while `menu_speak` competed for the same captions, discarding captions before they were spoken. Lesson: producers must not share one mutable slot without ownership rules. OGA adapters share one `speak` channel today.
- From pd-access (`src/accessibility/accessibility_announcement.c`): the working model. Menu announcements replace (`interrupt=1`, retained current-menu text, reason logged: dialog/focus/value/repeat). HUD announcements queue (`interrupt=0`) with id, player, type, flags, channel. Weapon-change and status carry a source plus player plus interrupt flag. Every call logs group, reason, elapsed microseconds, accepted/available. Policy (grouping, interrupt choice) lives in the accessibility module; gameplay never calls platform speech directly (AGENTS.md: give announcements priorities, dedup keys, replacement groups, expiry, player context; never call platform speech from gameplay).
- From CFC2 performance notes: the speech path must never block the game loop (1000 VirtualQuery/s on the speaking worker delayed keypress announcements; fixed by hoisting immutable checks and budgeting to 33 ms). OGA `speak` is called from the adapter path on or near the frame loop, so queue enqueue must be non-blocking and bounded.

## 2. API sketch

A small C queue sits between adapters and `Host.speak`. Adapters call `oga_announce`; the queue owns policy; the host drains to platform speech.

```c
typedef enum oga_priority { OGA_PRIO_LOW = 0, OGA_PRIO_NORMAL = 1, OGA_PRIO_HIGH = 2 } oga_priority_t;

typedef struct oga_announcement {
    const char*      text;           // UTF-8, copied on enqueue
    oga_priority_t   priority;       // HIGH interrupts, NORMAL/LOW queue
    const char*      group;          // replacement group, e.g. "whereami", "ally", "enemy", "hud"
    const char*      dedup_key;      // exact-repeat suppression, e.g. "ally:3:hp42"; NULL = none
    uint32_t         expiry_ms;      // drop if still queued after this; 0 = no expiry
    int              player;         // player context, -1 = none or shared screen
    uint64_t         enqueued_at_ms; // set by the queue (monotonic clock)
} oga_announcement_t;

// Adapter-facing call. Returns 1 accepted, 0 suppressed (reason logged via Host.log).
int oga_announce(const oga_announcement_t* a);
```

- `group` is a short stable string, not an enum, so new adapters add groups without touching the core ABI. The queue keeps at most one pending (not yet speaking) item per group; see section 3.
- `Host.speak(text, interrupt)` stays as the drain interface. The queue maps `priority == OGA_PRIO_HIGH` to `interrupt=true`, else `interrupt=false`. `Host.log` carries every accept/suppress decision with group, reason, and elapsed time, mirroring `accessibility_announcement.c`.
- Suggested `AdapterCommand` (in `Core/adapter.h`, raw values mirrored by Swift `AdapterCommand`) to group mapping:
  - `WhereAmI` maps to group "whereami", priority HIGH (player asked; interrupts).
  - `NextAlly`, `PrevAlly`, `NextUnactedAlly` map to group "ally", priority HIGH.
  - `NextEnemy`, `PrevEnemy` map to group "enemy", priority HIGH.
  - `DumpState` maps to no speech at all: it writes via `Host.log` only (diagnostic channel, never the play channel).
  - Unsolicited adapter output (background narration, tile or status changes on `on_frame`) maps to group "hud" or a game-specific group ("hp", "position"), priority NORMAL or LOW, never HIGH. Background text must never interrupt a requested answer.
- Player context: FE and DBZ adapters pass the side the line is about (player 0 allies, enemy lines player -1 or the owning slot). Hosts may ignore it today; it is recorded in the log so future per-player beacons or filtering have the field.

## 3. Rules

- Replacement semantics: a new announcement replaces the still-pending (queued, unspoken) item in the same group. New `WhereAmI` replaces pending `WhereAmI`; it does not touch "ally" or "hud". Rationale: the PD menu group keeps one retained menu text; the newest cursor row is the truth. Never replace the currently-speaking utterance, only queued ones; interrupting speech is the priority mechanism, not replacement.
- Dedup keys: exact-match suppression against the last spoken item with the same key. The key includes the value ("ally:2:hp:41"), never just the label ("ally:2"), or HP 41 spoken twice across a unit switch is wrongly dropped. The repeat key bypasses dedup (an explicit player request always speaks). Per the CFC2 Fighter Awards lesson, identical repeats on the same screen suppress; a group change re-arms.
- Expiry: every background ("hud", LOW or NORMAL) announcement carries `expiry_ms` (suggested default 1500 ms). If it has not reached the platform voice by then, drop it and log `decision=expired`. Requested (HIGH) announcements do not expire while queued behind one speaking line, but a newer same-group item still replaces them. Stale HP from 5 seconds ago must never be read as current.
- Rate-limit by TIME, not distinctness: per the CFC2 rule, any producer that can fire per frame (HP, position, step echoes) gets a minimum interval per group (suggested 800 ms for "hud", 300 ms for cursor echoes). Within the interval, newer items replace the pending one and only the latest is eligible when the interval opens. Report a RATE a human can act on: the log line for a suppressed item includes `suppressed=n in last 10s group=X`, not just one silent drop. Counters must be printed, per the CFC2 lesson that a count nobody prints is a count nobody has.
- Queue bounds: fixed capacity (suggested 8). A full queue drops the LOWEST priority oldest item first, never the newest HIGH. Enqueue never blocks, never allocates unboundedly, copies text with a length cap (PD uses `ACCESSIBILITY_ANNOUNCEMENT_TEXT_MAX`; OGA should define `OGA_ANNOUNCE_MAX`, suggested 512 bytes) and rejects empty or oversize input with a logged reason.
- Play/diag switch decoupling: two independent toggles, following `cfc2_guitext.on` vs `cfc2_guitext_diag.on`. The `Host.speak` path is play and is never gated by a diagnostics switch. `Host.log` verbosity (per-suppression lines vs aggregate rate lines) is diagnostics. `DumpState` stays on the log path even when speech is stopped. Turning off diagnostics must never silence a quit dialog, a WhereAmI answer, or any HIGH item.
- Stop speech sticks: keep the existing host behavior where stop silences background text until the next request. The queue mirrors it: stop clears pending NORMAL/LOW and marks silenced; only a new HIGH (player-requested) item lifts silence. This is the `SpeechEngine.isStopped` and `stopAll` contract carried into the queue.

## 4. Host-side mapping notes

- iOS (`Sources/OpenGameAccess/SpeechEngine.swift`): the queue drains through the existing `speak(_:interrupt:)` bridge. HIGH maps to `interrupt=true` (today: `synth.stopSpeaking(at: .immediate)` on the direct path; `announceThroughVoiceOver(trimmed, queue: false)` on the VoiceOver path). NORMAL/LOW maps to `interrupt=false` (direct queue; VoiceOver path uses `NSAttributedString` with `.accessibilitySpeechQueueAnnouncement: true`, noting the file warning that a plain String always interrupts). `lastSpoken` remains the repeat source and is set only for items actually handed to the platform, not suppressed ones. `pendingSpeech` (pre-audio-session buffer) stays as-is; the C queue feeds above it. `stop()` and `stopAll()` additionally tell the C queue to drop pending NORMAL/LOW and enter silenced mode. `announce(_:)` (UI replies, always immediate) bypasses the C queue entirely.
- Android (`app/native-overlay/.../accessibility/AccessibilitySpeech.kt`): the queue drains through `speak(text, interrupt)`. HIGH maps to `interrupt=true`, which today selects `TextToSpeech.QUEUE_FLUSH` in `speakImmediately`; NORMAL/LOW selects `QUEUE_ADD`. With a screen reader running (`screenReaderRunning()`), the existing live-region path applies: the assertive region for HIGH, polite for the rest, via `announcementView.accessibilityLiveRegion` plus `contentDescription` set (clear-first-to-restart on repeats, as the file documents). The JNI entry stays non-blocking per the file warning (called inside the frame loop); enqueue from the emulator thread, drain on the main or TTS thread. `pendingSpeech` (pre-`ready` buffer, cap 32) and `lastSpoken` with `repeatLast()` keep their roles; only drained items update `lastSpoken`. `stop()` clears pending non-HIGH as well as calling `tts?.stop()`.
- Both hosts log drain results (accepted, queued-behind, expired, suppressed counts) through the existing debug channels (`GameSession.debugLines`, cap 400, on iOS; `Log.i(TAG, "[speech] ...")` on Android), keeping play speech and diagnostic verbosity on separate switches per section 3.

## 5. Test plan

- Unit tests live with the adapter tests (`Core/*_test.cpp` pattern, e.g. `gba_adapter_test.cpp`): drive `oga_announce` with a stub `Host` recording `speak` and `log` calls. No ROMs, no emulator, per the scripts/git-hooks/pre-commit guard.
- Replacement: enqueue WhereAmI A then WhereAmI B before draining; assert only B speaks. Then enqueue WhereAmI C plus ally D; assert both speak (different groups do not replace).
- Dedup: same text plus same key twice; assert one speech, one logged suppression. Same text with a cleared group (group change re-arms) speaks again. The repeat path (HIGH re-request) bypasses dedup.
- Expiry: enqueue LOW with `expiry_ms=100`, advance the fake clock 200 ms, drain; assert dropped with `decision=expired` in the log.
- Sabotage cases (each verified to fail the test it guards, CFC2 `run_*_test.ps1` style):
  - Always-changing value: HP 42, 41, 40 enqueued back-to-back on group "hud". Without rate limiting this speaks three lines; assert at most one speech plus a suppression count. Sabotage: delete the time check (keep only the distinctness check) and the test must fail with three speeches.
  - Distinctness-only guard: feed "188", "191", "198" style redraws; assert rate-limited. Sabotage: replace the interval check with a last-text comparison; the test must fail.
  - Replacement across groups: sabotage the group comparison to always match; the cross-group test (WhereAmI plus ally) must fail showing the ally line eaten.
  - Dedup key missing the value (key "ally:2" only): feed ally 2 HP 41 then ally 2 HP 40; assert both speak. Sabotage: key on label only and the second is wrongly suppressed, failing the test.
  - Diag gating play: with diagnostics off, send HIGH WhereAmI plus LOW hud; assert HIGH still speaks. Sabotage: gate the drain on the diag flag; the test must fail silent.
  - Unbounded queue: enqueue 64 LOW items; assert memory bounded (cap 8), newest HIGH still accepted, oldest LOW dropped first.
- Integration: `GameSession.sendAdapterCommand` plus the `poke_command` path sends one HIGH item per tap; hold-tap or double-tap produces replacement, not stacking. Android: same via the JNI `speak` entry with TalkBack on and off.
