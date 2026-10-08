/*
 * dbzar_adapter_test.cpp — host test for the Another Road (DBZ Shin Budokai 2) adapter,
 * against SYNTHETIC memory built from the live-verified field-mode layout.
 *
 * WHY SYNTHETIC (same rule as dbz_adapter_test.cpp and dissidia_adapter_test.cpp): the
 * adapter's job is to turn bytes into speech, and the bytes that matter are already known
 * exactly (decompile + a live read of the relocated bases). Feeding it a fake RAM image
 * tests the LOGIC without booting a PSP title, and it runs in milliseconds.
 *
 * ⛔ THE MOCK MUST MAP ABSOLUTE ADDRESSES. The adapter reads ABSOLUTE PSP RAM addresses
 * (0x08xxxxxx); the mock translates them to a buffer offset. Subtract the base, then mask.
 *
 * The test asserts the CONTRACT, not the implementation:
 *   1. outside Another Road nothing speaks (the mode gate holds);
 *   2. WhereAmI names the LOWEST city first and counts the damage bands using the GAME's
 *      own 80/50/30 thresholds;
 *   3. a city crossing a band speaks once, and re-crossing the same band does not;
 *   4. repair (health rising) is not an event;
 *   5. empty slots (id 0) and unused entities (team -1) are never spoken about;
 *   6. Ready() is false with the mode flag clear.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t RAM[0x200000];
static const uint32_t RAMBASE = 0x08800000;
static inline size_t OFF(uint32_t a) { return (size_t)((a - RAMBASE) & 0x1FFFFF); }

static char SPOKEN[96][224]; static int NSPOKEN = 0;
static char LOGGED[96][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 96) snprintf(SPOKEN[NSPOKEN++], 224, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 96) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

namespace oga {
extern const Adapter kDragonBallZAnotherRoad;
// The adapter's entry points are static inside its own TU (as every adapter here keeps
// them), so the test drives the REGISTERED struct -- which is the real contract anyway.
inline bool AT(const Host* h)                 { return kDragonBallZAnotherRoad.attach(h); }
inline bool RD(void)                          { return kDragonBallZAnotherRoad.ready(); }
inline void CM(Command c)                     { kDragonBallZAnotherRoad.command(c); }
inline void FR(void)                          { kDragonBallZAnotherRoad.on_frame(); }
inline void DT(void)                          { kDragonBallZAnotherRoad.detach(); }
inline bool CUE(oga::CueSnapshot* s)         { return kDragonBallZAnotherRoad.cue_snapshot
                                                  ? kDragonBallZAnotherRoad.cue_snapshot(s) : false; }
}
// Focused-test stubs: the registry TU references sibling adapters, but this binary tests
// ONLY the Another Road adapter, so the siblings are null shells never attached/commanded.
// They live INSIDE namespace oga, exactly as the real registry defines them.
namespace oga {
static bool NoAttach(const Host*) { return false; }
static void NoFrame(void) {}
static void NoCommand(Command) {}
static bool NoReady(void) { return false; }
static void NoDetach(void) {}
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
extern const Adapter kDragonBallZSaiyans;
extern const Adapter kDissidiaFinalFantasy;
extern const Adapter kDragonQuestIX;
extern const Adapter kNintendoEntertainmentSystem;
extern const Adapter kNintendo64;
const Adapter kFireEmblemShadowDragon = { "fe11", "stub", "B2FE", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kGameBoyAdvance = { "gba", "stub", "AGB", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kDragonBallZSaiyans = { "dbz", "stub", "BRPE", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kDissidiaFinalFantasy = { "dissidia", "stub", "ULUS10437", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kDragonQuestIX = { "dq9", "stub", "YDQE", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kNintendoEntertainmentSystem = { "nes", "stub", "", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kNintendo64 = { "n64", "stub", "", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
}

// ---- the live-verified constants, repeated here on purpose -------------------------
// The test must fail if the ADAPTER's constants drift, so it writes through its own copy.
static const uint32_t ENT_BASE = 0x08A852D0u, ENT_STRIDE = 0xF0u;
static const uint32_t CITY_BASE = 0x08A876B0u, CITY_STRIDE = 0x70u;
static const uint32_t AR_MODE = 0x089B51D4u, AR_CHAPTER = 0x089B51D5u;

static void w8 (uint32_t a, uint8_t v)  { RAM[OFF(a)] = v; }
static void w16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }
static void w32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void wf (uint32_t a, float v)    { memcpy(&RAM[OFF(a)], &v, 4); }

static void CitySet(int slot, uint16_t id, int cur, int max, float x, float z)
{
    uint32_t b = CITY_BASE + slot * CITY_STRIDE;
    w16(b + 0x00, id);
    w32(b + 0x10, 0); wf(b + 0x10, x);
    wf(b + 0x18, z);
    w32(b + 0x20, (uint32_t) cur);
    w32(b + 0x24, (uint32_t) max);
    wf(b + 0x28, 10.0f);
}
static void EntitySet(int slot, int team, float x, float z)
{
    uint32_t b = ENT_BASE + slot * ENT_STRIDE;
    wf(b + 0x00, x);
    wf(b + 0x04, 0.0f);
    wf(b + 0x08, z);
    w32(b + 0x30, (uint32_t) team);
}

/// Did the most recently spoken line contain this text?
static bool SPOKEN_LAST_HAS(const char* n) {
    return NSPOKEN > 0 && strstr(SPOKEN[NSPOKEN - 1], n) != nullptr;
}

/// Walk the target ring onto the CITY kind, so a read is about a city rather than an enemy.
static void g_select_city(void)
{
    oga::CM(oga::Command::MenuNext);
    for (int i = 0; i < 6; i++) {
        if (SPOKEN_LAST_HAS("City")) return;
        oga::CM(oga::Command::MenuNext);
    }
}

static int NCHECK = 0, NFAIL = 0;
static void ok(const char* what, bool cond) {
    NCHECK++;
    if (!cond) { NFAIL++; printf("  FAIL %s\n", what); } else { printf("  ok   %s\n", what); }
}
static bool said(const char* needle) {
    for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], needle)) return true;
    return false;
}
/// ORDER MATTERS for the city list ("worst first"), and `said()` cannot express that:
/// it asks only whether a line was spoken anywhere, so a list sorted best-first passed.
/// This returns the index of the first line containing the needle, or -1.
static int saidAt(const char* needle) {
    for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], needle)) return i;
    return -1;
}
static void clear() { NSPOKEN = 0; NLOGGED = 0; }

static oga::Host H;
static void setup(void)
{
    memset(RAM, 0, sizeof RAM);
    // every entity slot starts unused: team -1
    for (int i = 0; i < 0x25; i++) w32(ENT_BASE + i * ENT_STRIDE + 0x30, (uint32_t) -1);
    memset(&H, 0, sizeof H);
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;
    H.announce_q = nullptr;   // direct-wire path, so assertions are synchronous
    H.now_ms = 1000;
    oga::AdapterSpeechReset();
    clear();
}

int main(void)
{
    printf("Another Road (DBZ Shin Budokai 2) adapter tests\n");

    // ---------------------------------------------------------------- 1. the mode gate
    setup();
    ok("attach succeeds (game id already matched in the registry)", oga::AT(&H));
    ok("Ready() is false while the mode flag is clear", !oga::RD());
    clear();
    oga::CM(oga::Command::WhereAmI);
    ok("outside Another Road, WhereAmI speaks a plain refusal", NSPOKEN == 1 && said("Not in story mode"));

    // ---------------------------------------------------------------- 2. in story mode
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1); w8(AR_CHAPTER, 0);
    ok("Ready() is true once the mode flag is set and the city array is mapped", oga::RD());

    // No cities yet must be said, not silently skipped.
    clear();
    oga::CM(oga::Command::WhereAmI);
    ok("with no city ids set, WhereAmI says there are no cities yet",
       NSPOKEN == 1 && said("No cities yet"));

    // Three cities, deliberately out of order: slot 2 worst, slot 0 best.
    CitySet(0, 0x0001, 900, 1000, 0.f,   0.f);     // 90%  -> healthy
    CitySet(1, 0x0002, 400, 1000, 100.f, 0.f);     // 40%  -> hurt (<50)
    CitySet(2, 0x0003, 250, 1000, 200.f, 0.f);     // 25%  -> critical (<30)
    EntitySet(0, 0, 0.f, 0.f);                     // player side
    EntitySet(5, 2, 300.f, 0.f);                   // enemy side
    EntitySet(6, 2, 300.f, 0.f);                   // enemy side

    clear();
    oga::CM(oga::Command::WhereAmI);
    ok("WhereAmI leads with the LOWEST city health, not slot 0",
       NSPOKEN >= 1 && said("lowest 25 percent"));
    ok("WhereAmI counts the game's own critical band (under 30%)",
       said("1 critical"));
    ok("WhereAmI counts live enemies from the entity array", said("2 enemies"));

    // The city list walks every city, worst first, one line each. It lives on NextEnemy;
    // MenuNext/MenuPrev are the target ring, which is a different question.
    clear();
    oga::CM(oga::Command::NextEnemy);
    ok("city list announces the count", said("3 cities"));
    {
        int crit = saidAt("City 3, 25 percent");
        int hurt = saidAt("City 2, 40 percent");
        int fine = saidAt("City 1, 90 percent");
        ok("city list names the critical city FIRST (order, not just presence)",
           crit >= 0 && hurt >= 0 && fine >= 0 && crit < hurt && hurt < fine);
    }

    // Empty slots must never be spoken about: slot 3 and 4 are id 0.
    ok("empty city slots (id 0) are never listed",
       !said("City 4,") && !said("City 5,"));

    // ---------------------------------------------------------------- 3. the band cue
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    CitySet(0, 0x0001, 900, 1000, 0.f, 0.f);
    oga::FR();                       // seeds the ratio at 90%
    clear();
    CitySet(0, 0x0001, 780, 1000, 0.f, 0.f);     // 90% -> 78%: crosses 80
    oga::FR();
    ok("a city falling through 80% speaks once", NSPOKEN == 1 && said("City 1 below 80 percent"));

    clear();
    CitySet(0, 0x0001, 770, 1000, 0.f, 0.f);     // still under 80: no new band
    oga::FR();
    ok("staying inside the same band does not repeat", NSPOKEN == 0);

    clear();
    CitySet(0, 0x0001, 480, 1000, 0.f, 0.f);     // -> 48%: crosses 50
    oga::FR();
    ok("crossing 50% speaks 'below half'", NSPOKEN == 1 && said("below half"));

    clear();
    CitySet(0, 0x0001, 280, 1000, 0.f, 0.f);     // -> 28%: crosses 30
    oga::FR();
    ok("crossing 30% speaks 'critical'", NSPOKEN == 1 && said("critical"));

    // ⛔ THE BOUNDARY IS THE THING UNDER TEST. Mutation testing showed that asserting only
    // the WORDS let a wrong threshold (90/60/40) pass the suite: the message was still
    // "below 80 percent", it just fired at 90. So probe each band's edge from BOTH sides.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    CitySet(0, 0x0001, 850, 1000, 0.f, 0.f);
    oga::FR();                                   // seed at 85%
    clear();
    CitySet(0, 0x0001, 810, 1000, 0.f, 0.f);      // 81%: still ABOVE the 80 band
    oga::FR();
    ok("81 percent does NOT trip the 80 band (the edge is exactly 0.8)", NSPOKEN == 0);
    clear();
    CitySet(0, 0x0001, 795, 1000, 0.f, 0.f);      // 79.5%: just BELOW it
    oga::FR();
    ok("79.5 percent DOES trip it, so the threshold is 0.8 and not 0.9",
       NSPOKEN == 1 && said("below 80 percent"));

    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    CitySet(0, 0x0001, 550, 1000, 0.f, 0.f);
    oga::FR();                                   // seed at 55%
    clear();
    CitySet(0, 0x0001, 510, 1000, 0.f, 0.f);      // 51%: above the 50 band
    oga::FR();
    ok("51 percent does NOT trip the 50 band", NSPOKEN == 0);
    clear();
    CitySet(0, 0x0001, 495, 1000, 0.f, 0.f);      // 49.5%: below it
    oga::FR();
    ok("49.5 percent DOES trip it, so that threshold is 0.5 and not 0.6",
       NSPOKEN == 1 && said("below half"));

    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    CitySet(0, 0x0001, 350, 1000, 0.f, 0.f);
    oga::FR();                                   // seed at 35%
    clear();
    CitySet(0, 0x0001, 310, 1000, 0.f, 0.f);      // 31%: above the 30 band
    oga::FR();
    ok("31 percent does NOT trip the critical band", NSPOKEN == 0);
    clear();
    CitySet(0, 0x0001, 295, 1000, 0.f, 0.f);      // 29.5%: below it
    oga::FR();
    ok("29.5 percent DOES trip it, so that threshold is 0.3 and not 0.4",
       NSPOKEN == 1 && said("critical"));

    // 4. repair is not an event
    clear();
    CitySet(0, 0x0001, 700, 1000, 0.f, 0.f);     // health RISING: the player is repairing
    oga::FR();
    ok("repair (health rising) never speaks", NSPOKEN == 0);

    // 5. unused entities are not counted as enemies
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    CitySet(0, 0x0001, 1000, 1000, 0.f, 0.f);
    // every slot is team -1 from setup(): a full array of unused slots
    for (int i = 0; i < 0x25; i++) EntitySet(i, -1, 0.f, 0.f);
    clear();
    oga::CM(oga::Command::PrevEnemy);          // status; NextEnemy is now the city list
    ok("37 unused entity slots report 0 enemies, not 37", said("0 enemies"));

    // 6. readiness follows the mode flag, not the array alone
    w8(AR_MODE, 0);
    ok("Ready() goes false when story mode is left", !oga::RD());
    clear();
    oga::CM(oga::Command::WhereAmI);
    ok("and commands refuse again rather than reading a stale field",
       NSPOKEN == 1 && said("Not in story mode"));

    // 7. the distance bands, through the ring's read. Same maths, same bands.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    // Walk the ring to a CITY so the reading is about a city.
    CitySet(0, 0x0001, 1000, 1000, 500.f, 500.f);
    g_select_city();
    clear();
    // (500,500) is 707 units away: the 600..1500 band, which is "far".
    oga::CM(oga::Command::NextUnactedAlly);
    ok("a 707-unit city reads as 'far'", said("City 1") && said("far"));

    CitySet(0, 0x0001, 400, 1000, 300.f, 0.f);
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("a 300-unit city reads as 'close', so the bands are distinct",
       said("City 1") && said("close") && said("40 percent"));

    // ---------------------------------------------------------------- 8. the target ring
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    // two enemies, two cities, one ally
    EntitySet(0, 0, 0.f, 0.f);          // the player
    EntitySet(1, 2, 400.f, 0.f);        // enemy A, east
    EntitySet(2, 2, 0.f, 400.f);        // enemy B, south
    CitySet(0, 0x0001, 900, 1000, 100.f, 0.f);
    CitySet(1, 0x0002, 500, 1000, 200.f, 0.f);

    clear();
    oga::CM(oga::Command::NextUnactedAlly);        // read the ring's starting target
    ok("the ring starts on an enemy (killing them ends the stage)",
       NSPOKEN >= 1 && said("Enemy"));

    clear();
    oga::CM(oga::Command::MenuNext);
    ok("next steps to the second enemy", said("Enemy 2"));

    clear();
    oga::CM(oga::Command::MenuNext);
    ok("stepping past the last enemy enters the CITY kind, worst health first",
       said("City 2, 50 percent"));

    clear();
    oga::CM(oga::Command::MenuNext);
    ok("the next step is the healthier city", said("City 1, 90 percent"));

    clear();
    oga::CM(oga::Command::MenuNext);
    ok("then the ally kind", said("Ally"));

    clear();
    oga::CM(oga::Command::MenuNext);
    ok("and the ring WRAPS back to the first enemy rather than stopping", said("Enemy 1"));

    clear();
    oga::CM(oga::Command::MenuPrev);
    ok("stepping back from the first enemy wraps to the ally", said("Ally"));

    clear();
    oga::CM(oga::Command::MenuPrev);
    ok("stepping back again lands on the last city", said("City 1, 90 percent"));

    // ---------------------------------------------------------------- 9. bearing + heading
    // Standing still with a fresh attach: there is no heading yet, so the answer must say
    // WHERE it is but refuse to name a clock direction.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    EntitySet(1, 2, 400.f, 0.f);
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("with no heading yet, the target is named but NO clock direction is given",
       said("Enemy 1") && !said("o'clock") && said("Move to get a direction"));

    // The map's axes: x is EAST, and z DECREASES to the north. The player flies from (0,0) to
    // (0,-200), which is due north, so the heading is 0 degrees.
    EntitySet(0, 0, 0.f, 0.f);
    clear();
    oga::CM(oga::Command::NextUnactedAlly);        // poll 1 seeds the previous position
    EntitySet(0, 0, 0.f, -200.f);                  // poll 2 sees 200 units of northward travel
    clear();
    oga::CM(oga::Command::NextUnactedAlly);

    // ⛔ THE TARGET IS PLACED RELATIVE TO WHERE THE PLAYER NOW IS (0,-200), NOT THE ORIGIN.
    // A target left at (400,0) would be SOUTH-EAST of them, and a test asserting "due east"
    // would then be testing the wrong thing while passing for the wrong reason.
    EntitySet(1, 2, 0.f, -600.f);                  // due NORTH of the player
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("after flying north, a target due north reads straight ahead",
       said("straight ahead") && said("Enemy 1"));

    EntitySet(1, 2, 400.f, -200.f);                // due EAST of the player
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("a target due east while flying north reads 3 o'clock", said("3 o'clock"));

    EntitySet(1, 2, -400.f, -200.f);               // due WEST of the player
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("a target due west reads 9 o'clock", said("9 o'clock"));

    // ---------------------------------------------------------------- 10. the cue snapshot
    oga::CueSnapshot cs{};
    ok("the cue reports a field with a bearing while the field is live",
       oga::CUE(&cs) && cs.field && cs.kind == 1 && cs.heading_live);

    // ⛔ THE STALE-HEADING RULE NEEDS A CLOCK. heading_live only means anything ACROSS TIME: it
    // goes false when the player stops moving and enough time passes. A check that never moves
    // the fake clock cannot see that difference, which is exactly how a mutation pinning
    // heading_live to true survived the first mutation pass.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    EntitySet(1, 2, 400.f, 0.f);
    H.now_ms = 10000;
    oga::CM(oga::Command::NextUnactedAlly);           // seed the previous position
    EntitySet(0, 0, 0.f, -200.f);                     // move north: a heading now exists
    H.now_ms = 10500;
    oga::CueSnapshot csFresh{};
    ok("a heading just after moving is live", oga::CUE(&csFresh) && csFresh.heading_live);
    H.now_ms = 10500 + 3000;                         // past kHeadingStaleMs (2500)
    oga::CueSnapshot csStale{};
    ok("after standing still past the window the heading is NOT live",
       oga::CUE(&csStale) && !csStale.heading_live);

    // ⛔ With no player row the cue must REFUSE rather than invent a direction.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(1, 2, 400.f, 0.f);                   // an enemy, but no player-side entity
    oga::CueSnapshot cs2{};
    ok("with no player row the cue refuses instead of pointing", !oga::CUE(&cs2));

    // Outside Another Road it must also refuse.
    w8(AR_MODE, 0);
    ok("outside story mode the cue refuses", !oga::CUE(&cs2));

    // With no target selected, the cue falls back to the NEAREST ENEMY (the player's call).
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    EntitySet(3, 2, 2000.f, 0.f);                  // far enemy
    EntitySet(4, 2, 300.f, 0.f);                   // near enemy
    oga::CueSnapshot cs3{};
    ok("with nothing selected the cue falls back to the nearest enemy",
       oga::CUE(&cs3) && cs3.kind == 1 && cs3.dist > 299.f && cs3.dist < 301.f);

    // ⛔ AND THE FALLBACK NEEDS TWO ENEMIES AT DIFFERENT DISTANCES. With one target, "nearest"
    // and "farthest" are the same answer, so a mutation reversing the comparison survived.
    // The ring's first entry must also be that same enemy, or the default reading and the
    // fallback would point at two different fighters.
    {
        // A fresh attach leaves the ring on enemy index 0, and the kinds are ordered NEAREST
        // FIRST -- so the ring's default read and the no-selection fallback must agree. The
        // check reads the ring WITHOUT stepping it; stepping first would test the step, not
        // the default.
        clear();
        oga::CM(oga::Command::NextUnactedAlly);
        oga::CueSnapshot cs4{};
        bool have = oga::CUE(&cs4);
        ok("the ring's first enemy is the one the fallback points at",
           have && cs4.dist > 299.f && cs4.dist < 301.f);
    }

    // ⛔ THE FALLBACK'S REAL CASE: the ring is sitting on the CITY kind when every city is
    // destroyed and enemies are still on the map. CurrentTarget() then has nothing, and the
    // cue must fall back to the NEAREST ENEMY rather than going silent -- silence would read
    // as "nothing left to do" at exactly the moment the stage is still live.
    // This is the only path that reaches the fallback, and without it a mutation reversing the
    // fallback's distance comparison survived the whole suite.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    EntitySet(3, 2, 2000.f, 0.f);                    // far enemy
    EntitySet(4, 2, 300.f, 0.f);                     // near enemy
    CitySet(0, 0x0001, 900, 1000, 100.f, 0.f);
    CitySet(1, 0x0002, 400, 1000, 200.f, 0.f);
    clear();
    oga::CM(oga::Command::NextUnactedAlly);          // start on the nearest enemy
    // Step until the ring actually reaches the city kind. TWO enemies sit ahead of it, so a
    // single step lands on enemy 2 -- the walk has to be driven, not assumed.
    bool onCity = false;
    for (int step = 0; step < 5 && !onCity; step++) {
        clear();
        oga::CM(oga::Command::MenuNext);
        onCity = said("City");
    }
    ok("the ring reaches the city kind within the two enemies that precede it", onCity);

    // Every city is destroyed: the ids go to 0.
    CitySet(0, 0x0000, 0, 0, 0.f, 0.f);
    CitySet(1, 0x0000, 0, 0, 0.f, 0.f);
    {
        oga::CueSnapshot cs6{};
        bool have = oga::CUE(&cs6);
        ok("with the selected kind EMPTY but enemies alive, the cue falls back to the NEAREST enemy",
           have && cs6.kind == 1 && cs6.dist > 299.f && cs6.dist < 301.f);
    }
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("and the spoken answer refuses the empty kind instead of guessing a city",
       said("No cities"));

    // An EMPTY KIND must be skipped, not stopped on: with one enemy, one ally and NO cities,
    // six ring steps must never land on the city kind, and every step must name a target.
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);                       // the player, who is also the only ally
    EntitySet(1, 2, 500.f, 0.f);                     // one enemy
    {
        bool sawEmptyCity = false, everyStepNamed = true;
        for (int step = 0; step < 6; step++) {
            clear();
            oga::CM(oga::Command::MenuNext);
            if (said("No cities")) sawEmptyCity = true;
            if (!(said("Enemy") || said("Ally") || said("City"))) everyStepNamed = false;
        }
        ok("an empty kind is SKIPPED: six ring steps never land on the empty city kind",
           !sawEmptyCity);
        ok("and every one of those steps names a real target", everyStepNamed);
    }

    // A kind with no members must say so rather than going silent in a way that reads as
    // "nothing here at all".
    setup();
    oga::AT(&H);
    w8(AR_MODE, 1);
    EntitySet(0, 0, 0.f, 0.f);
    clear();
    oga::CM(oga::Command::NextUnactedAlly);
    ok("with no enemies at all, the ring says so", said("No enemies"));

    oga::DT();

    printf("\n%d checks, %d failed\n", NCHECK, NFAIL);
    return NFAIL == 0 ? 0 : 1;
}
