/*
 * announce.h — the announcement queue: what may speak, when, and what is merged or dropped.
 *
 * WHY THIS EXISTS: Host::speak(text, interrupt) is a bare wire. It has no priority, no
 * replacement group, no dedup key, no expiry. Every per-frame producer (HP, position, cursor
 * echoes) floods it, and — the part that is invisible from the adapter side — once a line is
 * handed to VoiceOver or TalkBack with interrupt=false, THE SCREEN READER QUEUES IT ITSELF.
 * Nothing on our side can take it back, so "stale HP is never read as current" cannot be
 * enforced after the handoff. This queue therefore keeps AT MOST ONE UTTERANCE IN FLIGHT and
 * releases the next only when the host reports the current one finished (or, on hosts that
 * cannot report that, when its estimated duration has passed).
 *
 * Policy lives here; platforms only speak. The spec is docs/design/announcement-queue.md.
 *
 * THREADING CONTRACT: every function below is called from ONE thread (the frame loop), EXCEPT
 * announce_speech_done(), which platform callbacks may call from any thread. It only stores
 * into an atomic slot; the frame thread applies it on the next announce()/announce_tick().
 * Nothing here blocks or allocates after announce_create().
 */
#ifndef OGA_ANNOUNCE_H
#define OGA_ANNOUNCE_H

#include <stdint.h>
#include <stdbool.h>

namespace oga {

/// Three levels. App UI feedback (ROM failed to load, setting changed) is NOT a level: it is
/// carried by the control's own label/value, or spoken by the host directly (SpeechEngine
/// announce()), and never enters this queue. See docs/design/announcements-vs-live-regions.md.
enum class Priority : uint8_t {
    Low    = 0,  // ambient: continuous state (position, HP drift). Rate-limited, short-lived.
    Normal = 1,  // event: discrete game-state change (dialogue advanced, battle started).
    High   = 2,  // requested: the player pressed a query key. An adapter answering a command
                 // speaks the answer as High — there is no separate "inherits requested" rule.
};

/// 0 in a timing field means "the default for this priority"; kAnnounceOff means "none".
constexpr uint32_t kAnnounceDefault = 0;
constexpr uint32_t kAnnounceOff     = UINT32_MAX;

constexpr int kAnnounceTextMax    = 512;  // bytes incl. NUL; longer text is cut on a UTF-8 boundary
constexpr int kAnnounceMaxPending = 8;    // queued (not yet speaking) items, across all groups
constexpr int kAnnounceMaxGroups  = 16;   // distinct group names ever seen by one queue
constexpr int kAnnounceNameMax    = 32;   // group name bytes incl. NUL
constexpr int kAnnounceKeyMax     = 64;   // dedup key bytes incl. NUL
/// Spoken lines the player can walk back through. WHY A RING AND NOT ONE SLOT: "repeat"
/// as a single last-line mirror answers "what did it just say", but a player who missed
/// two lines ago has no path to it. Reviewed accessibility mods keep 50 (Kingdom Access)
/// with repeat / previous / next; this is the same idea at the useful window. The QUEUE
/// owns it because the queue is the only thing that knows what actually reached the
/// platform -- a line suppressed as a duplicate never did, and must not be repeatable.
constexpr int kAnnounceHistoryMax = 16;

struct Announcement {
    const char* text;        // UTF-8, copied on enqueue. Empty is rejected.
    Priority    priority;
    /// Replacement group, a short stable string ("whereami", "enemy", "hp"). At most one
    /// PENDING item per group: a newer one replaces it. The item already speaking is never
    /// replaced — interrupting is the priority mechanism, not replacement.
    const char* group;
    /// Exact-repeat suppression against the last line SPOKEN in this group. Put the VALUE in
    /// the key ("ally:2:hp:41"), never just the label, or HP 41 -> 40 would be wrongly eaten.
    /// nullptr = no dedup. High items bypass it (the player asked, so they hear it).
    const char* dedup_key;
    /// Max time the item may wait AFTER it became eligible to speak. Defaults: Low/Normal
    /// 1500 ms, High never. Measured from eligibility, not enqueue, so a rate-limited item
    /// is not killed by its own rate limit — the settled final value still gets through.
    uint32_t    expiry_ms;
    /// Minimum time between spoken lines in this group. Defaults: Low 2000 ms, Normal and
    /// High none. Inside the window the newest item replaces the pending one.
    uint32_t    min_interval_ms;
    /// Which side the line is about; -1 = none / shared. Logged only, for future beacons.
    int         player;
};

/// The platform side. `utterance_id` must come back through announce_speech_done() so a
/// late "finished" from an interrupted line cannot release the line that replaced it.
/// iOS: announcementDidFinishNotification (match on the announced string -> id).
/// Android TTS: the utteranceId passed to speak(). Android TalkBack live region: no signal;
/// set host_reports_done=false and the queue paces by estimated duration.
struct AnnounceSink {
    void (*speak)(void* ctx, const char* utf8, bool interrupt, uint32_t utterance_id);
    void (*log)(void* ctx, const char* utf8);   // diagnostics channel; never gates speech
    void* ctx;
};

struct AnnounceConfig {
    bool     host_reports_done;  // false: release the next line after the estimate instead
    bool     diag_verbose;       // true: one log line per decision; false: 10 s aggregates
    uint32_t est_base_ms;        // estimate = base + per_byte * bytes. 0 -> 300
    uint32_t est_ms_per_byte;    // 0 -> 55 (conservative for fast screen-reader rates)
};

enum class Decision : uint8_t {
    SpokeNow,            // handed to the platform during this call
    Queued,              // pending; will speak when eligible
    Replaced,            // pending, and it replaced an older pending item in its group
    SuppressedDuplicate, // same dedup key as the last line spoken in this group
    SuppressedSilenced,  // player pressed stop; only a High item lifts that
    SuppressedLower,     // a higher-priority item is already pending in this group
    RejectedInvalid,     // null/empty text or group
    RejectedGroupTable,  // more than kAnnounceMaxGroups distinct groups
    RejectedFull,        // queue full of items that outrank this one
};

struct AnnounceStats {
    uint32_t spoken, interrupted, replaced, duplicate, expired, silenced,
             lower, rejected, evicted, retried, watchdog;
};

struct AnnounceQueue;

AnnounceQueue* announce_create(const AnnounceSink& sink, const AnnounceConfig& cfg);
void           announce_destroy(AnnounceQueue* q);

/// Adapter-facing call. Non-blocking; may speak before returning.
Decision announce(AnnounceQueue* q, const Announcement& a, uint64_t now_ms);

/// Call once per frame: applies finished-speech reports, expires stale items, opens rate
/// windows, and releases the next line.
void announce_tick(AnnounceQueue* q, uint64_t now_ms);

/// Thread-safe. `success=false` (VoiceOver dropped it) retries a High line once.
void announce_speech_done(AnnounceQueue* q, uint32_t utterance_id, bool success);

/// The player's stop key. Clears pending Low/Normal and forgets the line in flight (the host
/// stops the platform voice itself). Low/Normal stay silenced until the next High item.
void announce_stop(AnnounceQueue* q, uint64_t now_ms);

/// Walk the spoken history. Repeating is a player action, so a repeated line is spoken as
/// High: it cuts off whatever is speaking, lifts a stop, and bypasses dedup and rate
/// limits. Neither adds to the history, so walking back does not slide toward itself.
/// `newest` speaks the most recent line and resets the walk; `older` steps one line back
/// and clamps. Both return false when there is nothing (more) to repeat.
bool announce_repeat_newest(AnnounceQueue* q, uint64_t now_ms);
bool announce_repeat_older(AnnounceQueue* q, uint64_t now_ms);
/// Lines currently recorded, for the UI to say "nothing to repeat" honestly.
int  announce_history_count(const AnnounceQueue* q);

void          announce_set_diag_verbose(AnnounceQueue* q, bool on);
AnnounceStats announce_stats(const AnnounceQueue* q);
int           announce_pending_count(const AnnounceQueue* q);
bool          announce_in_flight(const AnnounceQueue* q);

const char* decision_name(Decision d);

} // namespace oga

#endif // OGA_ANNOUNCE_H
