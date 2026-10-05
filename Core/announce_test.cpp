/*
 * announce_test.cpp — host test for the announcement queue. No ROM, no emulator, no platform.
 *
 * The sink is a recorder: it keeps every speak() and log() call, and it never reports "done"
 * on its own — each test says exactly when the platform finished a line, which is the whole
 * point of the one-in-flight rule. Time is a plain integer the test advances by hand.
 *
 * scripts/announce-test.sh also rebuilds this with each SABOTAGE_* switch in announce.cpp and
 * REQUIRES a failure, so every rule below is proven to be guarded, not just exercised.
 *
 * Build:  g++ -std=c++17 -ICore -o /tmp/announce_test Core/announce_test.cpp Core/announce.cpp
 */
#include "announce.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace oga;

namespace {

struct Spoken {
    std::string text;
    bool        interrupt;
    uint32_t    id;
};

struct Rec {
    std::vector<Spoken>      spoken;
    std::vector<std::string> logs;
};

void rec_speak(void* ctx, const char* t, bool interrupt, uint32_t id)
{
    static_cast<Rec*>(ctx)->spoken.push_back({t, interrupt, id});
}
void rec_log(void* ctx, const char* t) { static_cast<Rec*>(ctx)->logs.push_back(t); }

struct Fixture {
    Rec            rec;
    AnnounceQueue* q = nullptr;
    uint64_t       now = 1000;

    explicit Fixture(bool reports_done = true, bool verbose = true)
    {
        AnnounceSink sink{rec_speak, rec_log, &rec};
        AnnounceConfig cfg{};
        cfg.host_reports_done = reports_done;
        cfg.diag_verbose      = verbose;
        q = announce_create(sink, cfg);
    }
    ~Fixture() { announce_destroy(q); }

    Decision say(const char* text, Priority p, const char* group, const char* key = nullptr,
                 uint32_t expiry = kAnnounceDefault, uint32_t interval = kAnnounceDefault)
    {
        Announcement a{text, p, group, key, expiry, interval, -1};
        return announce(q, a, now);
    }
    /// The platform finished the most recent line.
    void finish(bool success = true)
    {
        if (!rec.spoken.empty()) announce_speech_done(q, rec.spoken.back().id, success);
        announce_tick(q, now);
    }
    void advance(uint64_t ms) { now += ms; announce_tick(q, now); }
    int  count(const char* text) const
    {
        int n = 0;
        for (auto& s : rec.spoken) n += s.text == text;
        return n;
    }
    std::string last() const { return rec.spoken.empty() ? "" : rec.spoken.back().text; }
};

int g_fail = 0, g_pass = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) { g_pass++; printf("ok: %s\n", msg); }                   \
        else { g_fail++; printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

// ── Priority and interruption ──────────────────────────────────────────────────────────

void test_high_speaks_now_and_interrupts()
{
    Fixture f;
    Decision d = f.say("Cursor 1, 20.", Priority::High, "whereami");
    CHECK(d == Decision::SpokeNow, "high speaks immediately");
    CHECK(f.rec.spoken.size() == 1 && f.rec.spoken[0].interrupt, "high is handed over with interrupt");
}

void test_one_in_flight()
{
    // The platform queues a line itself once it has it, and we can never take it back. So a
    // second line must stay HERE until the first one is reported finished.
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    Decision d = f.say("Dialogue: Welcome.", Priority::Normal, "dialogue");
    CHECK(d == Decision::Queued, "second normal line waits while the first is in flight");
    CHECK(f.rec.spoken.size() == 1, "only one line handed to the platform");
    f.finish();
    CHECK(f.rec.spoken.size() == 2 && f.last() == "Dialogue: Welcome.", "done report releases the next line");
    CHECK(!f.rec.spoken[1].interrupt, "a normal line released in turn does not interrupt");
}

void test_stale_done_ignored()
{
    // WhereAmI pressed twice: the second answer cuts off the first. A late "finished" for the
    // FIRST must not release the queue while the second is still being spoken.
    Fixture f;
    f.say("Cursor 1, 20.", Priority::High, "whereami");
    uint32_t first = f.rec.spoken.back().id;
    f.say("Cursor 2, 20.", Priority::High, "whereami");
    CHECK(f.rec.spoken.size() == 2 && f.rec.spoken[1].interrupt, "same-group high re-request interrupts");
    f.say("Enemy phase.", Priority::Normal, "battle");
    announce_speech_done(f.q, first, true);
    announce_tick(f.q, f.now);
    CHECK(f.rec.spoken.size() == 2, "late done for the interrupted line is ignored");
    f.finish();
    CHECK(f.last() == "Enemy phase.", "done for the current line releases the queue");
}

void test_high_other_group_waits()
{
    // WhereAmI then NextEnemy: the player wants both answers, so the second waits its turn.
    Fixture f;
    f.say("Cursor 1, 20.", Priority::High, "whereami");
    Decision d = f.say("Enemy: Soldier, 12 HP.", Priority::High, "enemy");
    CHECK(d == Decision::Queued, "high in a different group queues behind a speaking high");
    f.finish();
    CHECK(f.last() == "Enemy: Soldier, 12 HP." && f.rec.spoken.back().interrupt,
          "then speaks, still as an interrupting (requested) line");
}

void test_normal_interrupts_low_not_reverse()
{
    Fixture f;
    f.say("Position 4, 7.", Priority::Low, "position");
    Decision d = f.say("Battle start.", Priority::Normal, "battle");
    CHECK(d == Decision::SpokeNow && f.rec.spoken.back().interrupt, "event interrupts ambient");

    Fixture g;
    g.say("Battle start.", Priority::Normal, "battle");
    d = g.say("Position 4, 7.", Priority::Low, "position");
    CHECK(d == Decision::Queued && g.rec.spoken.size() == 1, "ambient never interrupts an event");
}

// ── Replacement ────────────────────────────────────────────────────────────────────────

void test_replacement_same_group()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.say("HP 42.", Priority::Normal, "hud");
    Decision d = f.say("HP 30.", Priority::Normal, "hud");
    CHECK(d == Decision::Replaced, "newer pending line in the same group replaces the older");
    f.finish();
    f.finish();
    CHECK(f.count("HP 42.") == 0 && f.count("HP 30.") == 1, "only the newest reading is spoken");
}

void test_cross_group_not_replaced()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.say("HP 42.", Priority::Normal, "hud");
    f.say("Ally Marth moved.", Priority::Normal, "ally");
    f.finish();
    f.finish();
    CHECK(f.count("HP 42.") == 1 && f.count("Ally Marth moved.") == 1,
          "different groups never replace each other");
}

void test_lower_does_not_replace_higher()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.say("Level up!", Priority::Normal, "status");
    Decision d = f.say("Status ok.", Priority::Low, "status");
    CHECK(d == Decision::SuppressedLower, "ambient cannot replace a pending event in its group");
}

// ── Dedup ──────────────────────────────────────────────────────────────────────────────

void test_dedup()
{
    Fixture f;
    f.say("Marth, 18 HP.", Priority::Normal, "ally", "ally:1:hp:18");
    f.finish();
    Decision d = f.say("Marth, 18 HP.", Priority::Normal, "ally", "ally:1:hp:18");
    CHECK(d == Decision::SuppressedDuplicate, "identical key in the same group is suppressed");
    d = f.say("Marth, 18 HP.", Priority::High, "ally", "ally:1:hp:18");
    CHECK(d == Decision::SpokeNow, "a player request bypasses dedup");
}

void test_dedup_key_includes_value()
{
    Fixture f;
    f.say("Ally 2, 41 HP.", Priority::Normal, "ally", "ally:2:hp:41");
    f.finish();
    Decision d = f.say("Ally 2, 40 HP.", Priority::Normal, "ally", "ally:2:hp:40");
    CHECK(d == Decision::SpokeNow, "a changed value under the same label is not a duplicate");
}

// ── Expiry and rate limiting ───────────────────────────────────────────────────────────

void test_expiry()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.say("HP 42.", Priority::Normal, "hud", nullptr, 100);
    f.advance(200);
    CHECK(announce_pending_count(f.q) == 0 && announce_stats(f.q).expired == 1, "stale line expires");
    f.finish();
    CHECK(f.count("HP 42.") == 0, "an expired line is never spoken as if current");
}

void test_expiry_counts_from_eligibility()
{
    // The rate limit must not kill the settled final value: an item waiting for its window
    // is not stale yet.
    Fixture f;
    f.say("HP 42.", Priority::Low, "hp");
    f.finish();
    f.advance(100);
    f.say("HP 30.", Priority::Low, "hp");     // eligible at +2000, default expiry 1500
    f.advance(1800);
    CHECK(f.count("HP 30.") == 0 && announce_pending_count(f.q) == 1, "waits for the rate window");
    f.advance(200);
    CHECK(f.count("HP 30.") == 1, "final value speaks when the window opens");
}

void test_rate_limit_always_changing()
{
    // CFC2: a guard asking "is this different from last time" is no guard against a value
    // that differs every time. 2741 gauge lines in 5 minutes. Rate-limit by TIME.
    Fixture f;
    const char* hp[] = {"HP 42.", "HP 41.", "HP 40.", "HP 39.", "HP 38."};
    for (const char* t : hp) {
        f.say(t, Priority::Low, "hp", t);     // key = text: every value is distinct
        f.finish();                           // the platform is instantly done each time
        f.advance(16);
    }
    CHECK(f.rec.spoken.size() == 1, "five per-frame changes speak once inside the window");
    f.advance(2000);
    CHECK(f.rec.spoken.size() == 2 && f.last() == "HP 38.", "then only the latest value");
}

// ── Silence, diagnostics, bounds ───────────────────────────────────────────────────────

void test_stop_sticks_until_request()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.say("Dialogue.", Priority::Normal, "dialogue");
    announce_stop(f.q, f.now);
    CHECK(announce_pending_count(f.q) == 0 && !announce_in_flight(f.q), "stop clears pending and in-flight");
    CHECK(f.say("HP 30.", Priority::Normal, "hud") == Decision::SuppressedSilenced, "events stay silent after stop");
    CHECK(f.say("Cursor 1, 20.", Priority::High, "whereami") == Decision::SpokeNow, "a request speaks and lifts silence");
    f.finish();
    CHECK(f.say("HP 30.", Priority::Normal, "hud") == Decision::SpokeNow, "events speak again after a request");
}

void test_diag_off_never_silences_play()
{
    Fixture f(true, /*verbose=*/false);
    f.say("Cursor 1, 20.", Priority::High, "whereami");
    CHECK(f.count("Cursor 1, 20.") == 1, "with diagnostics off, a requested line still speaks");
    f.finish();
    f.say("Position 4, 7.", Priority::Low, "position");
    CHECK(f.count("Position 4, 7.") == 1, "with diagnostics off, ambient still speaks");
}

void test_aggregate_log_reports_rate()
{
    Fixture f(true, false);
    for (int i = 0; i < 4; i++) {
        char t[32];
        snprintf(t, sizeof t, "HP %d.", 40 - i);
        f.say(t, Priority::Low, "hp", t);
        f.advance(10);
    }
    f.advance(10000);
    bool found = false;
    for (auto& l : f.rec.logs) found |= l.find("group=hp") != std::string::npos && l.find("suppressed=") != std::string::npos;
    CHECK(found, "suppressions are reported as a rate per group");
}

void test_bounded_and_evicts_lowest_oldest()
{
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");        // in flight
    char g[32], t[32];
    for (int i = 0; i < kAnnounceMaxPending; i++) {
        snprintf(g, sizeof g, "amb%d", i);
        snprintf(t, sizeof t, "Ambient %d.", i);
        f.say(t, Priority::Low, g);
    }
    CHECK(announce_pending_count(f.q) == kAnnounceMaxPending, "queue fills to its cap");
    Decision d = f.say("Where am I answer.", Priority::High, "whereami");
    CHECK(d == Decision::SpokeNow, "a new request is always accepted when full");
    f.say("Level up!", Priority::Normal, "status");
    CHECK(announce_pending_count(f.q) == kAnnounceMaxPending, "count never exceeds the cap");
    CHECK(announce_stats(f.q).evicted == 1, "one ambient item evicted to make room");
    for (int i = 0; i < 12; i++) f.finish();
    CHECK(f.count("Level up!") == 1, "the newest event survives eviction");
    CHECK(f.count("Ambient 0.") == 0, "the oldest ambient item was the one evicted");
}

void test_full_rejects_lower()
{
    Fixture f;
    f.say("Cursor.", Priority::High, "whereami");               // in flight
    char g[32];
    for (int i = 0; i < kAnnounceMaxPending; i++) {
        snprintf(g, sizeof g, "req%d", i);
        f.say("Answer.", Priority::High, g);
    }
    CHECK(f.say("Position.", Priority::Low, "position") == Decision::RejectedFull,
          "ambient is refused, not queued, when requests fill the queue");
}

// ── Platform realities ─────────────────────────────────────────────────────────────────

void test_retry_dropped_request_once()
{
    // VoiceOver drops an announcement posted while it reads a focused element, and reports
    // that through announcementWasSuccessfulUserInfoKey.
    Fixture f;
    f.say("Cursor 1, 20.", Priority::High, "whereami");
    f.finish(false);
    CHECK(f.count("Cursor 1, 20.") == 2, "a dropped request is retried once");
    f.finish(false);
    CHECK(f.count("Cursor 1, 20.") == 2 && !announce_in_flight(f.q), "and not a second time");

    Fixture g;
    g.say("Battle start.", Priority::Normal, "battle");
    g.finish(false);
    CHECK(g.count("Battle start.") == 1, "a dropped event is not retried");
}

void test_estimate_when_host_cannot_report()
{
    // TalkBack live regions give no completion signal.
    Fixture f(/*reports_done=*/false);
    f.say("Battle start.", Priority::Normal, "battle");        // ~300 + 55*13 = 1015 ms
    f.say("Dialogue.", Priority::Normal, "dialogue");
    f.advance(500);
    CHECK(f.rec.spoken.size() == 1, "next line waits for the estimated duration");
    f.advance(600);
    CHECK(f.last() == "Dialogue.", "then is released without a done report");
}

void test_watchdog()
{
    // Lines queued behind a wedged utterance go stale and expire (correctly); what matters is
    // that the queue itself recovers.
    Fixture f;
    f.say("Battle start.", Priority::Normal, "battle");
    f.advance(10000);
    CHECK(!announce_in_flight(f.q) && announce_stats(f.q).watchdog == 1,
          "a lost done report cannot wedge the queue");
    CHECK(f.say("Dialogue.", Priority::Normal, "dialogue") == Decision::SpokeNow,
          "the next line speaks after the watchdog");
}

void test_utf8_truncation()
{
    Fixture f;
    std::string s;
    while (s.size() < 700) s += "é";                             // 2 bytes each
    f.say(s.c_str(), Priority::High, "long");
    const std::string& out = f.last();
    bool ok = out.size() < (size_t)kAnnounceTextMax && out.size() % 2 == 0;
    CHECK(ok, "long text is cut on a UTF-8 boundary under the cap");
    CHECK(f.say("", Priority::High, "x") == Decision::RejectedInvalid, "empty text is rejected");
}

} // namespace

// ── Spoken history (repeat / previous) ────────────────────────────────────────────────

void test_history_records_only_what_was_spoken()
{
    // ⛔ THE RULE THAT MATTERS MOST. A duplicate suppressed by dedup never reached the
    // platform, so repeating must not surface it. Record at emit(), not at announce().
    Fixture f;
    f.say("Enemy: Soldier, 12 HP.", Priority::Normal, "enemy", "enemy:1");
    f.finish();
    f.say("Enemy: Soldier, 12 HP.", Priority::Normal, "enemy", "enemy:1");  // suppressed
    f.finish();
    CHECK(announce_history_count(f.q) == 1, "a deduped line never enters the history");
    CHECK(announce_repeat_newest(f.q, f.now), "the one real line is repeatable");
    CHECK(f.last() == "Enemy: Soldier, 12 HP.", "repeat speaks the line that was heard");
}

void test_history_walk_back_and_clamp()
{
    Fixture f;
    f.say("One.", Priority::Normal, "a"); f.finish();
    f.say("Two.", Priority::Normal, "a"); f.finish();
    f.say("Three.", Priority::Normal, "a"); f.finish();
    CHECK(announce_history_count(f.q) == 3, "three spoken lines recorded");

    CHECK(announce_repeat_older(f.q, f.now), "step back");
    CHECK(f.last() == "Two.", "older walks back one line");
    CHECK(announce_repeat_older(f.q, f.now), "step back again");
    CHECK(f.last() == "One.", "older walks to the oldest");
    CHECK(!announce_repeat_older(f.q, f.now), "older refuses at the oldest (no wrap)");
    CHECK(f.last() == "One.", "a refused step speaks nothing");

    CHECK(announce_repeat_newest(f.q, f.now), "newest resets the walk");
    CHECK(f.last() == "Three.", "newest returns to the most recent line");
}

void test_repeat_does_not_reenter_history()
{
    // Otherwise walking back would slide the ring toward itself and the same line would
    // repeat forever instead of advancing.
    Fixture f;
    f.say("One.", Priority::Normal, "a"); f.finish();
    f.say("Two.", Priority::Normal, "a"); f.finish();
    int before = announce_history_count(f.q);
    announce_repeat_newest(f.q, f.now);
    announce_repeat_older(f.q, f.now);
    announce_repeat_older(f.q, f.now);
    CHECK(announce_history_count(f.q) == before, "repeats do not grow the history");
    CHECK(f.last() == "One.", "and the walk still reached the oldest line");
}

void test_repeat_is_a_player_action()
{
    // Repeating is the player asking, so it must behave like every other High query:
    // it cuts off what is speaking, and it lifts a stop.
    Fixture f;
    f.say("One.", Priority::Normal, "a"); f.finish();
    f.say("Two.", Priority::Normal, "a"); f.finish();
    f.say("Ambient chatter.", Priority::Low, "chatter");   // now speaking
    CHECK(announce_repeat_older(f.q, f.now), "step back past the ambient line");
    CHECK(f.last() == "Two." && f.rec.spoken.back().interrupt,
          "walking back cuts off the ambient line and speaks interrupting");

    Fixture g;
    g.say("One.", Priority::Normal, "a"); g.finish();
    announce_stop(g.q, g.now);
    Decision silenced = g.say("Ignored while silenced.", Priority::Normal, "a");
    CHECK(silenced == Decision::SuppressedSilenced, "silence holds automatic lines");
    CHECK(announce_repeat_newest(g.q, g.now), "a repeat works while silenced");
    CHECK(g.last() == "One.", "and it lifts the silence, like any player request");
}

void test_history_empty_is_honest()
{
    Fixture f;
    CHECK(announce_history_count(f.q) == 0, "nothing recorded yet");
    CHECK(!announce_repeat_newest(f.q, f.now), "newest refuses with an empty history");
    CHECK(!announce_repeat_older(f.q, f.now), "older refuses with an empty history");
    CHECK(f.rec.spoken.empty(), "and neither speaks a placeholder");
}

void test_history_ring_is_bounded()
{
    // The ring must not grow without bound, and past its capacity it keeps the NEWEST.
    Fixture f;
    char line[32];
    for (int i = 0; i < kAnnounceHistoryMax + 4; i++) {
        snprintf(line, sizeof(line), "Line %d.", i);
        f.say(line, Priority::Normal, "a");
        f.finish();
    }
    CHECK(announce_history_count(f.q) == kAnnounceHistoryMax, "the ring is capped");
    CHECK(announce_repeat_newest(f.q, f.now), "newest is the last line spoken");
    snprintf(line, sizeof(line), "Line %d.", kAnnounceHistoryMax + 3);
    CHECK(f.last() == line, "the newest line survives the wrap");
}

void test_history_groups_are_reused()
{
    // A repeat re-speaks under the ORIGINAL group, so it cannot burn one of the 16
    // group-table slots. (16 distinct groups is the documented hard cap.)
    Fixture f;
    for (int i = 0; i < kAnnounceMaxGroups; i++) {
        char g[16];
        snprintf(g, sizeof(g), "grp%d", i);
        f.say("Line.", Priority::Normal, g);
        f.finish();
    }
    Decision d = f.say("One more.", Priority::Normal, "grp0");
    CHECK(d != Decision::RejectedGroupTable, "the group table is exactly full, not over");
    announce_repeat_newest(f.q, f.now);
    CHECK(f.last() == "One more.", "repeat still works with a full group table");
}

int main()
{
    test_high_speaks_now_and_interrupts();
    test_one_in_flight();
    test_stale_done_ignored();
    test_high_other_group_waits();
    test_normal_interrupts_low_not_reverse();
    test_replacement_same_group();
    test_cross_group_not_replaced();
    test_lower_does_not_replace_higher();
    test_dedup();
    test_dedup_key_includes_value();
    test_expiry();
    test_expiry_counts_from_eligibility();
    test_rate_limit_always_changing();
    test_stop_sticks_until_request();
    test_diag_off_never_silences_play();
    test_aggregate_log_reports_rate();
    test_bounded_and_evicts_lowest_oldest();
    test_full_rejects_lower();
    test_retry_dropped_request_once();
    test_estimate_when_host_cannot_report();
    test_watchdog();
    test_utf8_truncation();
    test_history_records_only_what_was_spoken();
    test_history_walk_back_and_clamp();
    test_repeat_does_not_reenter_history();
    test_repeat_is_a_player_action();
    test_history_empty_is_honest();
    test_history_ring_is_bounded();
    test_history_groups_are_reused();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) return 1;
    printf("ALL ANNOUNCE QUEUE TESTS PASSED\n");
    return 0;
}
