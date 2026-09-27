/*
 * announce.cpp — see announce.h and docs/design/announcement-queue.md.
 *
 * The SABOTAGE_* switches below are compiled OUT of every real build. They exist so
 * scripts/announce-test.sh can prove each test actually guards the rule it names: the script
 * rebuilds with one switch on and REQUIRES the test to fail. A test that still passes with its
 * rule removed was never testing that rule.
 */
#include "announce.h"

#include <atomic>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace oga {

namespace {

constexpr uint32_t kDefaultExpiryMs      = 1500;
constexpr uint32_t kDefaultLowIntervalMs = 2000;
constexpr uint64_t kAggregateWindowMs    = 10000;
constexpr uint64_t kWatchdogFloorMs      = 2000;

struct Group {
    char     name[kAnnounceNameMax];
    char     last_key[kAnnounceKeyMax];
    bool     has_last_key;
    bool     has_spoken;
    uint64_t last_spoken_at;
    // Suppressions inside the current aggregate window, printed as a RATE a human can act
    // on. A count nobody prints is a count nobody has.
    uint32_t win_dup, win_replaced, win_expired, win_other;
};

struct Item {
    bool     used;
    char     text[kAnnounceTextMax];
    char     key[kAnnounceKeyMax];
    bool     has_key;
    Priority priority;
    int      group;
    int      player;
    uint32_t expiry_ms;
    uint32_t interval_ms;
    uint64_t enqueued_at;
    uint64_t seq;
};

struct InFlight {
    bool     active;
    Item     item;
    uint32_t id;
    int      attempts;
    uint64_t release_at;   // estimated end (no done reports) or watchdog (with done reports)
};

} // namespace

struct AnnounceQueue {
    AnnounceSink   sink;
    AnnounceConfig cfg;
    Group          groups[kAnnounceMaxGroups];
    int            group_count;
    Item           pending[kAnnounceMaxPending];
    InFlight       flight;
    bool           silenced;
    uint64_t       next_seq;
    uint32_t       next_id;
    uint64_t       window_start;
    AnnounceStats  stats;
    // Written by any thread in announce_speech_done(); read by the frame thread.
    // Layout: bit 63 = valid, bit 32 = success, low 32 bits = utterance id.
    std::atomic<uint64_t> done_slot;
    std::atomic<uint32_t> published_id;
};

namespace {

void copy_utf8(char* dst, int cap, const char* src)
{
    size_t n = strlen(src);
    if (n > (size_t)(cap - 1)) {
        n = (size_t)(cap - 1);
        // Never cut inside a multi-byte sequence: back up to the lead byte.
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

const char* prio_name(Priority p)
{
    switch (p) {
    case Priority::Low: return "low";
    case Priority::Normal: return "normal";
    case Priority::High: return "high";
    }
    return "?";
}

void logf(AnnounceQueue* q, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logf(AnnounceQueue* q, const char* fmt, ...)
{
    if (!q->sink.log) return;
    char buf[kAnnounceTextMax + 160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    q->sink.log(q->sink.ctx, buf);
}

void decision_log(AnnounceQueue* q, const char* decision, const Item& it)
{
    if (!q->cfg.diag_verbose) return;
    logf(q, "[announce] decision=%s group=%s prio=%s player=%d text=\"%s\"", decision,
         q->groups[it.group].name, prio_name(it.priority), it.player, it.text);
}

int find_or_add_group(AnnounceQueue* q, const char* name)
{
    for (int i = 0; i < q->group_count; i++) {
#ifdef SABOTAGE_GROUP_ALWAYS_MATCH
        return i;
#endif
        if (strcmp(q->groups[i].name, name) == 0) return i;
    }
    if (q->group_count >= kAnnounceMaxGroups) return -1;
    Group& g = q->groups[q->group_count];
    memset(&g, 0, sizeof g);
    copy_utf8(g.name, kAnnounceNameMax, name);
    return q->group_count++;
}

bool keys_equal(const char* a, const char* b)
{
#ifdef SABOTAGE_DEDUP_LABEL_ONLY
    // Compare only up to the last ':' — i.e. the label without the value.
    const char* ea = strrchr(a, ':');
    const char* eb = strrchr(b, ':');
    size_t la = ea ? (size_t)(ea - a) : strlen(a);
    size_t lb = eb ? (size_t)(eb - b) : strlen(b);
    return la == lb && memcmp(a, b, la) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

uint32_t expiry_for(const Item& it)
{
    if (it.expiry_ms == kAnnounceOff) return kAnnounceOff;
    if (it.expiry_ms != kAnnounceDefault) return it.expiry_ms;
    return it.priority == Priority::High ? kAnnounceOff : kDefaultExpiryMs;
}

uint32_t interval_for(const Item& it)
{
    if (it.interval_ms == kAnnounceOff) return 0;
    if (it.interval_ms != kAnnounceDefault) return it.interval_ms;
    return it.priority == Priority::Low ? kDefaultLowIntervalMs : 0;
}

uint64_t eligible_at(const AnnounceQueue* q, const Item& it)
{
#ifdef SABOTAGE_NO_RATE_LIMIT
    return it.enqueued_at;
#endif
    const Group& g = q->groups[it.group];
    uint64_t t = it.enqueued_at;
    uint32_t iv = interval_for(it);
    if (iv && g.has_spoken && g.last_spoken_at + iv > t) t = g.last_spoken_at + iv;
    return t;
}

uint64_t estimate_ms(const AnnounceQueue* q, const char* text)
{
    uint64_t base = q->cfg.est_base_ms ? q->cfg.est_base_ms : 300;
    uint64_t per  = q->cfg.est_ms_per_byte ? q->cfg.est_ms_per_byte : 55;
    return base + per * strlen(text);
}

void emit(AnnounceQueue* q, const Item& it, bool interrupt, uint64_t now, int attempts)
{
    InFlight& f = q->flight;
    f.active   = true;
    f.item     = it;
    f.id       = ++q->next_id;
    f.attempts = attempts;
    uint64_t est = estimate_ms(q, it.text);
    f.release_at = now + (q->cfg.host_reports_done ? est * 2 + kWatchdogFloorMs : est);
    q->published_id.store(f.id, std::memory_order_release);

    Group& g = q->groups[it.group];
    g.has_spoken     = true;
    g.last_spoken_at = now;
    if (it.has_key) {
        copy_utf8(g.last_key, kAnnounceKeyMax, it.key);
        g.has_last_key = true;
    }
    q->stats.spoken++;
    if (q->cfg.diag_verbose)
        logf(q, "[announce] speak id=%u group=%s prio=%s interrupt=%d player=%d text=\"%s\"",
             f.id, g.name, prio_name(it.priority), interrupt ? 1 : 0, it.player, it.text);

#ifdef SABOTAGE_DIAG_GATES_PLAY
    if (!q->cfg.diag_verbose) return;
#endif
    if (q->sink.speak) q->sink.speak(q->sink.ctx, it.text, interrupt, f.id);
}

/// May `cand` cut off the line in flight? Higher priority always may. A High may also cut off
/// a High in the SAME group (the player asked again; the old answer is stale). A High in a
/// different group waits: WhereAmI then NextEnemy means the player wants both answers.
bool may_interrupt(const Item& cand, const Item& cur)
{
    if (cand.priority > cur.priority) return true;
    return cand.priority == Priority::High && cur.priority == Priority::High &&
           cand.group == cur.group && cand.seq > cur.seq;
}

void apply_done(AnnounceQueue* q, uint64_t now)
{
    uint64_t v = q->done_slot.exchange(0, std::memory_order_acq_rel);
    if (!(v >> 63)) return;
    uint32_t id      = (uint32_t)v;
    bool     success = (v >> 32) & 1;
    InFlight& f = q->flight;
#ifdef SABOTAGE_STALE_DONE
    (void)id;
    if (!f.active) return;
#else
    if (!f.active || f.id != id) return;   // a late report from a line we already replaced
#endif
    if (!success && f.item.priority == Priority::High && f.attempts < 2) {
        // VoiceOver drops an announcement posted while it reads a focused element, and says
        // so via announcementWasSuccessfulUserInfoKey. A requested answer gets one retry.
        q->stats.retried++;
        Item it = f.item;
        emit(q, it, true, now, f.attempts + 1);
        return;
    }
    f.active = false;
}

void drop_expired(AnnounceQueue* q, uint64_t now)
{
    for (Item& it : q->pending) {
        if (!it.used) continue;
        uint32_t ex = expiry_for(it);
        if (ex == kAnnounceOff) continue;
        if (now >= eligible_at(q, it) + ex) {
            q->stats.expired++;
            q->groups[it.group].win_expired++;
            decision_log(q, "expired", it);
            it.used = false;
        }
    }
}

/// Best candidate: highest priority, then oldest. Only items whose rate window is open.
Item* best_eligible(AnnounceQueue* q, uint64_t now)
{
    Item* best = nullptr;
    for (Item& it : q->pending) {
        if (!it.used || now < eligible_at(q, it)) continue;
        if (!best || it.priority > best->priority ||
            (it.priority == best->priority && it.seq < best->seq))
            best = &it;
    }
    return best;
}

/// Release the next line if the rules allow it. Returns the seq it spoke, or 0.
uint64_t pump(AnnounceQueue* q, uint64_t now)
{
    apply_done(q, now);
    drop_expired(q, now);

    InFlight& f = q->flight;
    if (f.active && now >= f.release_at) {
        if (q->cfg.host_reports_done) {
            q->stats.watchdog++;
            logf(q, "[announce] watchdog: no done report for id=%u; releasing", f.id);
        }
        f.active = false;
    }

    Item* cand = best_eligible(q, now);
    if (!cand) return 0;

    bool interrupt = cand->priority == Priority::High;
    if (f.active) {
#ifndef SABOTAGE_NO_IN_FLIGHT_GATE
        if (!may_interrupt(*cand, f.item)) return 0;
#endif
        interrupt = true;
        q->stats.interrupted++;
        decision_log(q, "interrupted", f.item);
    }
    Item it = *cand;
    cand->used = false;
    emit(q, it, interrupt, now, 1);
    return it.seq;
}

void flush_aggregates(AnnounceQueue* q, uint64_t now)
{
    if (now - q->window_start < kAggregateWindowMs) return;
    for (int i = 0; i < q->group_count; i++) {
        Group& g = q->groups[i];
        uint32_t total = g.win_dup + g.win_replaced + g.win_expired + g.win_other;
        if (total && !q->cfg.diag_verbose)
            logf(q, "[announce] suppressed=%u in last %llus group=%s (dup %u, replaced %u, "
                    "expired %u, other %u)",
                 total, (unsigned long long)((now - q->window_start) / 1000), g.name,
                 g.win_dup, g.win_replaced, g.win_expired, g.win_other);
        g.win_dup = g.win_replaced = g.win_expired = g.win_other = 0;
    }
    q->window_start = now;
}

} // namespace

AnnounceQueue* announce_create(const AnnounceSink& sink, const AnnounceConfig& cfg)
{
    AnnounceQueue* q = new AnnounceQueue();
    q->sink = sink;
    q->cfg  = cfg;
    q->done_slot.store(0);
    q->published_id.store(0);
    return q;
}

void announce_destroy(AnnounceQueue* q) { delete q; }

Decision announce(AnnounceQueue* q, const Announcement& a, uint64_t now_ms)
{
    if (!q) return Decision::RejectedInvalid;
    if (!a.text || !*a.text || !a.group || !*a.group) {
        q->stats.rejected++;
        logf(q, "[announce] decision=rejected reason=empty text or group");
        return Decision::RejectedInvalid;
    }
    int gi = find_or_add_group(q, a.group);
    if (gi < 0) {
        q->stats.rejected++;
        logf(q, "[announce] decision=rejected reason=group table full group=%s", a.group);
        return Decision::RejectedGroupTable;
    }
    Group& g = q->groups[gi];

    Item it{};
    it.used        = true;
    copy_utf8(it.text, kAnnounceTextMax, a.text);
    it.has_key     = a.dedup_key && *a.dedup_key;
    if (it.has_key) copy_utf8(it.key, kAnnounceKeyMax, a.dedup_key);
    it.priority    = a.priority;
    it.group       = gi;
    it.player      = a.player;
    it.expiry_ms   = a.expiry_ms;
    it.interval_ms = a.min_interval_ms;
    it.enqueued_at = now_ms;
    it.seq         = ++q->next_seq;

    if (q->silenced) {
        if (a.priority != Priority::High) {
            q->stats.silenced++;
            g.win_other++;
            decision_log(q, "silenced", it);
            return Decision::SuppressedSilenced;
        }
        q->silenced = false;
    }

    if (a.priority != Priority::High && it.has_key && g.has_last_key && keys_equal(it.key, g.last_key)) {
        q->stats.duplicate++;
        g.win_dup++;
        decision_log(q, "duplicate", it);
        return Decision::SuppressedDuplicate;
    }

    bool replaced = false;
    for (Item& p : q->pending) {
        if (!p.used || p.group != gi) continue;
        if (p.priority > it.priority) {
            q->stats.lower++;
            g.win_other++;
            decision_log(q, "lower-than-pending", it);
            return Decision::SuppressedLower;
        }
        q->stats.replaced++;
        g.win_replaced++;
        decision_log(q, "replaced", p);
        p.used = false;
        replaced = true;
    }

    Item* slot = nullptr;
    for (Item& p : q->pending)
        if (!p.used) { slot = &p; break; }
    if (!slot) {
        // Full: evict the lowest-priority, oldest item — never the newest High.
        Item* victim = nullptr;
        for (Item& p : q->pending) {
#ifdef SABOTAGE_EVICT_NEWEST
            if (!victim || p.seq > victim->seq) victim = &p;
#else
            if (!victim || p.priority < victim->priority ||
                (p.priority == victim->priority && p.seq < victim->seq))
                victim = &p;
#endif
        }
        if (victim->priority > it.priority) {
            q->stats.rejected++;
            g.win_other++;
            decision_log(q, "full", it);
            return Decision::RejectedFull;
        }
        q->stats.evicted++;
        q->groups[victim->group].win_other++;
        decision_log(q, "evicted", *victim);
        slot = victim;
    }
    *slot = it;

    uint64_t spoke = pump(q, now_ms);
    if (spoke == it.seq) return Decision::SpokeNow;
    decision_log(q, replaced ? "replaced-pending" : "queued", it);
    return replaced ? Decision::Replaced : Decision::Queued;
}

void announce_tick(AnnounceQueue* q, uint64_t now_ms)
{
    if (!q) return;
    pump(q, now_ms);
    flush_aggregates(q, now_ms);
}

void announce_speech_done(AnnounceQueue* q, uint32_t utterance_id, bool success)
{
    if (!q) return;
#ifndef SABOTAGE_STALE_DONE
    // Cheap early filter; apply_done() re-checks on the frame thread.
    if (q->published_id.load(std::memory_order_acquire) != utterance_id) return;
#endif
    uint64_t v = (1ull << 63) | ((success ? 1ull : 0ull) << 32) | utterance_id;
    q->done_slot.store(v, std::memory_order_release);
}

void announce_stop(AnnounceQueue* q, uint64_t now_ms)
{
    if (!q) return;
    (void)now_ms;
    for (Item& p : q->pending)
        if (p.used && p.priority != Priority::High) p.used = false;
    q->flight.active = false;
    q->silenced = true;
    q->done_slot.store(0);
    if (q->cfg.diag_verbose) logf(q, "[announce] stop: pending cleared, silenced until next request");
}

void announce_set_diag_verbose(AnnounceQueue* q, bool on)
{
    if (q) q->cfg.diag_verbose = on;
}

AnnounceStats announce_stats(const AnnounceQueue* q)
{
    return q ? q->stats : AnnounceStats{};
}

int announce_pending_count(const AnnounceQueue* q)
{
    int n = 0;
    if (q)
        for (const Item& p : q->pending) n += p.used ? 1 : 0;
    return n;
}

bool announce_in_flight(const AnnounceQueue* q) { return q && q->flight.active; }

const char* decision_name(Decision d)
{
    switch (d) {
    case Decision::SpokeNow: return "spoke";
    case Decision::Queued: return "queued";
    case Decision::Replaced: return "replaced";
    case Decision::SuppressedDuplicate: return "duplicate";
    case Decision::SuppressedSilenced: return "silenced";
    case Decision::SuppressedLower: return "lower";
    case Decision::RejectedInvalid: return "invalid";
    case Decision::RejectedGroupTable: return "group-table-full";
    case Decision::RejectedFull: return "full";
    }
    return "?";
}

} // namespace oga
