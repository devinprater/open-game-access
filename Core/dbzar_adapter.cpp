/*
 * dbzar_adapter.cpp — Dragon Ball Z: Shin Budokai — Another Road (PSP, ULUS10234).
 *
 * WHAT THIS IS FOR: Another Road's STORY MODE ("Another Road") is a FIELD MODE, not a
 * menu: the player flies a wide landscape and has to protect the cities on it. Each city
 * has a health gauge that an enemy lowers simply by loitering near it, and the player
 * repairs a city by staying over it. There is also a mission layer ("Don't let any city's
 * health fall below 40%!"), enemy fighters the player flies into to start a battle, and
 * Senzu Beans that decide how many times an enemy can be beaten. None of that is visible
 * to a blind player and none of it is speech-shaped on its own — it is continuous state.
 *
 * So this adapter answers the few questions a blind player actually has in field mode, and
 * announces the one live event worth hearing: a city crossing a damage band.
 *
 * VERIFIED MAP (decompile + LIVE read; the live read is what made this possible):
 *   ⛔ EBOOT.dec is RELOCATABLE. The static bytes of an `addiu r, r, 0x1780` are NOT the
 *      runtime address — .rel.text patches the lui/addiu pair on load. Every address below
 *      was therefore read from LIVE memory (`scripts/dbzar-live-addresses.mjs`), not from the
 *      file. The two field-mode arrays sit ~2 MiB above their static values: the static
 *      0x08805780 is really 0x08A852D0.
 *
 *   ENTITY(i)  = 0x08A852D0 + i * 0xF0        i = 0..0x24 (37 slots)
 *       +0x00 float x, +0x04 float y, +0x08 float z
 *       +0x30 i32  team:  0/1 = player side, 2/3 = enemy side, -1 = slot unused
 *                  (the 0/1-vs-2/3 split is the project's earlier finding taken from the
 *                   decompile's two index ranges, not re-derived here)
 *       +0xD1 byte "visible this frame", set by the city module from a 50.0 check
 *   CITY(i)    = 0x08A876B0 + i * 0x70        i = 0..4 (5 cities; the module walks exactly 5)
 *       +0x00 u16  city id; 0 = EMPTY SLOT
 *       +0x10 float x, +0x18 float z (the module's proximity test uses 0x10 and 0x18)
 *       +0x20 i32  current health
 *       +0x24 i32  max health          <- percent = +0x20 / +0x24, the game's own arithmetic
 *       +0x28 float radius (its square is the proximity test's bound)
 *       +0x34 i32  a render/task handle array (3 entries), -1 when none
 *   AR MODE FLAG   = 0x089B51D4  (EBOOT .data; NOT relocated — verified live, reads 1
 *                  exactly when Another Road is up)
 *   CHAPTER INDEX  = 0x089B51D5
 *   CITY COUNT the game walks: 5. Entity slots walked: 0x25 = 37.
 *
 * ⛔ THE DAMAGE THRESHOLD IS NOT INVENTED. FUN_0001a90c computes
 *      ratio = [+0x20] / [+0x24]
 *   and swaps up to three task handles when that ratio crosses 0.8 / 0.5 / 0.3 — i.e. the
 *   GAME'S OWN damage bands are 80% / 50% / 30%. The cue below announces exactly those
 *   crossings, so every number a player hears is one the game itself acts on.
 *
 * ⛔ WHAT THIS DOES NOT DO, stated so nobody hunts for it: it does not track WHICH enemy is
 *   attacking a city (the city module knows which entity is in range but does not store
 *   which one), it does not read the mission text or its completion state, it does not
 *   count Senzu Beans, and it has no audio beacon (field mode is not battle, and no cue
 *   contract has been agreed for it). Those are open, not done.
 *
 * READ-ONLY. This adapter reads memory and speaks. It never writes RAM and never moves the
 * player; the proximity and team checks it uses are the game's own.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

namespace oga {
namespace dbzar {

// ---- the verified field-mode layout ----------------------------------------
constexpr uint32_t ENT_BASE   = 0x08A852D0u;   // live; static 0x08805780 + relocation
constexpr uint32_t ENT_STRIDE = 0xF0u;
constexpr int      ENT_COUNT  = 0x25;         // 37

constexpr uint32_t CITY_BASE   = 0x08A876B0u; // live; static 0x08803B60 + relocation
constexpr uint32_t CITY_STRIDE = 0x70u;
constexpr int      CITY_COUNT  = 5;          // the module walks exactly 5

constexpr uint32_t ENT_TEAM   = 0x30u;        // i32; -1 = unused slot
constexpr uint32_t ENT_ACTIVE = 0xD1u;        // byte

constexpr uint32_t CITY_ID     = 0x00u;       // u16
constexpr uint32_t CITY_X      = 0x10u;       // float
constexpr uint32_t CITY_Z      = 0x18u;       // float
constexpr uint32_t CITY_CUR    = 0x20u;       // i32 current health
constexpr uint32_t CITY_MAX    = 0x24u;       // i32 max health
constexpr uint32_t CITY_RADIUS = 0x28u;       // float

constexpr uint32_t AR_MODE    = 0x089B51D4u;  // EBOOT .data, static
constexpr uint32_t AR_CHAPTER = 0x089B51D5u;

constexpr uint32_t RAM_LO = 0x08800000u;      // PSP user RAM
constexpr uint32_t RAM_HI = 0x0A000000u;

/// The game's OWN damage bands, from FUN_0001a90c's three ratio comparisons.
constexpr float kBandHealthy  = 0.80f;
constexpr float kBandHurt     = 0.50f;
constexpr float kBandCritical = 0.30f;

static const Host* g_host = nullptr;
static float g_lastRatio[CITY_COUNT] = {0};
static bool  g_haveRatio[CITY_COUNT] = {false};

// ---------------------------------------------------------------------------
// reads
// ---------------------------------------------------------------------------
static uint8_t  R8 (uint32_t a) { return g_host && g_host->read8  ? g_host->read8 (g_host->ctx, a) : 0; }
static uint16_t R16(uint32_t a) { return g_host && g_host->read16 ? g_host->read16(g_host->ctx, a) : 0; }
static uint32_t R32(uint32_t a) { return g_host && g_host->read32 ? g_host->read32(g_host->ctx, a) : 0; }
static int32_t  RI32(uint32_t a) { return (int32_t) R32(a); }
static float    RF32(uint32_t a) {
    uint32_t w = R32(a);
    float f;
    memcpy(&f, &w, 4);
    return f;
}
static bool InRam(uint32_t a, uint32_t n) { return a >= RAM_LO && (a + n) <= RAM_HI; }

/// The mode flag is what tells us Another Road is up at all. Everything else here is
/// meaningless outside it, so this is the gate every command and the frame hook use.
static bool InAnotherRoad(void) { return R8(AR_MODE) == 1; }

/// The chapter the game is in. NOT a browse cursor: the decompile and a live test both
/// showed this value does not move while the Chapter Select map is browsed.
static int ChapterIndex(void) { return (int) R8(AR_CHAPTER); }

// ---------------------------------------------------------------------------
// speech helpers
// ---------------------------------------------------------------------------
static void SayRaw(const char* text, Priority prio, const char* group, const char* key)
{
    if (!text || !*text) return;
    if (g_host->announce_q) {
        Announcement a;
        a.text = text;
        a.priority = prio;
        a.group = group;
        a.dedup_key = key;
        a.expiry_ms = kAnnounceDefault;
        a.min_interval_ms = kAnnounceDefault;
        a.player = -1;
        announce(g_host->announce_q, a, g_host->now_ms);
    } else if (g_host->speak) {
        // Host-test stubs (and hosts that predate the queue) speak synchronously.
        g_host->speak(g_host->ctx, text, prio == Priority::High);
    }
}

/// Percentages are rounded down to the nearest 5. WHY: the gauge moves continuously while
/// an enemy is on a city, and "37 percent" becoming "36 percent" is noise a player cannot
/// act on. The game's own bands are 80/50/30, so 5 points is already finer than anything it
/// distinguishes.
static int BucketPct(int pct) { return (pct / 5) * 5; }

// ---------------------------------------------------------------------------
// city reads
// ---------------------------------------------------------------------------
struct CityInfo {
    int   slot;        // 0..4
    bool  present;     // id != 0
    float x, z;
    int   cur, max, pct;
};

static bool ReadCity(int i, CityInfo* out)
{
    if (i < 0 || i >= CITY_COUNT) return false;
    uint32_t b = CITY_BASE + (uint32_t) i * CITY_STRIDE;
    if (!InRam(b, CITY_STRIDE)) return false;
    out->slot = i;
    out->present = (R16(b + CITY_ID) != 0);
    out->x = RF32(b + CITY_X);
    out->z = RF32(b + CITY_Z);
    out->cur = RI32(b + CITY_CUR);
    out->max = RI32(b + CITY_MAX);
    out->pct = (out->max > 0) ? (int) ((((float) out->cur) / (float) out->max) * 100.0f + 0.5f) : -1;
    return true;
}

// ---------------------------------------------------------------------------
// entity reads (the player's own row included)
// ---------------------------------------------------------------------------
static bool EntityAt(int i, float* x, float* y, float* z, int* team)
{
    if (i < 0 || i >= ENT_COUNT) return false;
    uint32_t b = ENT_BASE + (uint32_t) i * ENT_STRIDE;
    if (!InRam(b, ENT_STRIDE)) return false;
    int t = RI32(b + ENT_TEAM);
    if (team) *team = t;
    if (t == -1) return false;
    if (x) *x = RF32(b + 0x00);
    if (y) *y = RF32(b + 0x04);
    if (z) *z = RF32(b + 0x08);
    return true;
}

/// The player is whichever live entity sits in a player-side slot (team 0 or 1).
/// ⛔ WHICH OF 0/1 IS 1P IS NOT PINNED HERE. The city module distinguishes "player team"
/// (indices 0..2) from "enemy team" (3..0x24) when it checks who is over a city, and that
/// split is what this uses; picking slot 0 specifically would be a guess.
static bool PlayerPos(float* x, float* z)
{
    for (int i = 0; i < ENT_COUNT; i++) {
        int t = 0;
        float ex, ez;
        if (!EntityAt(i, &ex, nullptr, &ez, &t)) continue;
        if (t == 0 || t == 1) { if (x) *x = ex; if (z) *z = ez; return true; }
    }
    return false;
}

/// Count live entities on each side. -1 slots are skipped, so an empty map reads 0/0
/// rather than 37.
static void CountSides(int* players, int* enemies)
{
    int p = 0, e = 0;
    for (int i = 0; i < ENT_COUNT; i++) {
        int t = 0;
        if (!EntityAt(i, nullptr, nullptr, nullptr, &t)) continue;
        if (t >= 2) e++; else p++;
    }
    if (players) *players = p;
    if (enemies) *enemies = e;
}

/// Cities that exist, collected in slot order.
static int CollectCities(CityInfo* out)
{
    int n = 0;
    for (int i = 0; i < CITY_COUNT; i++) if (ReadCity(i, &out[n])) n++;
    return n;
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------
static void CmdWhereAmI(void)
{
    if (!InAnotherRoad()) { SayRaw("Not in story mode.", Priority::High, "dbzar", nullptr); return; }

    CityInfo cities[CITY_COUNT];
    int n = CollectCities(cities);

    // Cities that still exist, worst health first — the order a player needs them in.
    int order[CITY_COUNT]; int m = 0;
    for (int i = 0; i < n; i++) if (cities[i].present) order[m++] = i;
    for (int i = 1; i < m; i++) {
        int k = order[i], j = i - 1;
        while (j >= 0 && cities[order[j]].pct > cities[k].pct) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }

    char line[256];
    if (m == 0) {
        // No cities is a real state (the field has not loaded), and saying so beats silence.
        SayRaw("Story mode. No cities yet.", Priority::High, "dbzar", nullptr);
        return;
    }
    const CityInfo* worst = &cities[order[0]];
    if (m == 1) snprintf(line, sizeof line, "One city left, %d percent.", BucketPct(worst->pct));
    else        snprintf(line, sizeof line, "%d cities, lowest %d percent.", m, BucketPct(worst->pct));
    SayRaw(line, Priority::High, "dbzar", nullptr);

    int hurt = 0, critical = 0;
    for (int i = 0; i < m; i++) {
        int mx = cities[order[i]].max ? cities[order[i]].max : 1;
        float r = (float) cities[order[i]].cur / (float) mx;
        if (r < kBandCritical) critical++;
        else if (r < kBandHurt) hurt++;
    }
    if (critical > 0)  snprintf(line, sizeof line, "%d critical.", critical);
    else if (hurt > 0) snprintf(line, sizeof line, "%d below half.", hurt);
    else               snprintf(line, sizeof line, "All above half.");
    SayRaw(line, Priority::High, "dbzar", nullptr);

    int players = 0, enemies = 0;
    CountSides(&players, &enemies);
    snprintf(line, sizeof line, "%d enemies.", enemies);
    SayRaw(line, Priority::High, "dbzar", nullptr);
}

/// Every city with its health, worst first. This is the plan, where WhereAmI is the
/// in-play one-liner.
static void CmdCityList(void)
{
    if (!InAnotherRoad()) { SayRaw("Not in story mode.", Priority::High, "dbzar", nullptr); return; }
    CityInfo cities[CITY_COUNT];
    int n = CollectCities(cities);
    int m = 0;
    for (int i = 0; i < n; i++) if (cities[i].present) m++;
    if (m == 0) { SayRaw("No cities yet.", Priority::High, "dbzar", nullptr); return; }

    // Insertion sort, worst health first, so the player hears the emergencies first.
    int order[CITY_COUNT]; int k = 0;
    for (int i = 0; i < n; i++) if (cities[i].present) order[k++] = i;
    for (int i = 1; i < k; i++) {
        int v = order[i], j = i - 1;
        while (j >= 0 && cities[order[j]].pct > cities[v].pct) { order[j + 1] = order[j]; j--; }
        order[j + 1] = v;
    }
    char line[224];
    snprintf(line, sizeof line, "%d cities.", m);
    SayRaw(line, Priority::High, "dbzar", nullptr);
    for (int i = 0; i < k; i++) {
        const CityInfo* c = &cities[order[i]];
        snprintf(line, sizeof line, "City %d, %d percent.", c->slot + 1, BucketPct(c->pct));
        SayRaw(line, Priority::High, "dbzar", nullptr);
    }
}

/// The nearest city. Distance is reported in bands, never as a raw world unit: a number
/// like 1723.4 carries no meaning, and the game's own map is roughly 1500 units across.
static void CmdNearestCity(void)
{
    if (!InAnotherRoad()) { SayRaw("Not in story mode.", Priority::High, "dbzar", nullptr); return; }
    float px = 0, pz = 0;
    if (!PlayerPos(&px, &pz)) { SayRaw("Field not tracked yet.", Priority::High, "dbzar", nullptr); return; }

    CityInfo cities[CITY_COUNT];
    int n = CollectCities(cities);
    int bestSlot = -1; float best = 0;
    for (int i = 0; i < n; i++) {
        if (!cities[i].present) continue;
        float dx = cities[i].x - px, dz = cities[i].z - pz;
        float d = sqrtf(dx * dx + dz * dz);
        if (bestSlot < 0 || d < best) { best = d; bestSlot = cities[i].slot; }
    }
    if (bestSlot < 0) { SayRaw("No cities yet.", Priority::High, "dbzar", nullptr); return; }

    const char* band = best < 150.f ? "right here" : best < 600.f ? "close"
                     : best < 1500.f ? "far" : "across the map";
    char line[224];
    snprintf(line, sizeof line, "City %d is %s, %d percent.", bestSlot + 1, band, BucketPct(cities[bestSlot].pct));
    SayRaw(line, Priority::High, "dbzar", nullptr);
}

static void CmdStatus(void)
{
    if (!InAnotherRoad()) { SayRaw("Not in story mode.", Priority::High, "dbzar", nullptr); return; }
    CityInfo cities[CITY_COUNT];
    int n = CollectCities(cities);
    int present = 0, total = 0;
    for (int i = 0; i < n; i++) if (cities[i].present) { present++; total += cities[i].pct; }
    int players = 0, enemies = 0;
    CountSides(&players, &enemies);
    char line[224];
    if (present)
        snprintf(line, sizeof line, "Chapter %d. %d cities, average %d percent. %d enemies.",
                 ChapterIndex() + 1, present, present ? total / present : 0, enemies);
    else
        snprintf(line, sizeof line, "Chapter %d. No cities yet. %d enemies.", ChapterIndex() + 1, enemies);
    SayRaw(line, Priority::High, "dbzar", nullptr);
}

static void CmdDump(void)
{
    // Diagnostics go to the LOG channel, never to speech: this is for a bug report, and a
    // blind player should not have to hear it.
    if (!g_host || !g_host->log) return;
    char line[256];
    snprintf(line, sizeof line, "DBZAR mode=%d chapter=%d", (int) R8(AR_MODE), ChapterIndex());
    g_host->log(g_host->ctx, line);
    for (int i = 0; i < CITY_COUNT; i++) {
        CityInfo c;
        if (!ReadCity(i, &c)) continue;
        snprintf(line, sizeof line, "DBZAR city[%d] id=%d cur=%d max=%d pct=%d present=%d",
                 i, (int) R16(CITY_BASE + i * CITY_STRIDE), c.cur, c.max, c.pct, c.present ? 1 : 0);
        g_host->log(g_host->ctx, line);
    }
    for (int i = 0; i < ENT_COUNT; i++) {
        float x, y, z; int t;
        if (!EntityAt(i, &x, &y, &z, &t)) continue;
        snprintf(line, sizeof line, "DBZAR ent[%d] team=%d x=%.1f y=%.1f z=%.1f", i, t, x, y, z);
        g_host->log(g_host->ctx, line);
    }
}

// ---------------------------------------------------------------------------
// the frame hook: the ONE event worth hearing
// ---------------------------------------------------------------------------
/// A city crossing a damage band. The bands are the game's own (80/50/30), and this fires
/// on the crossing edge only, so a city hovering at exactly 50% does not repeat itself.
/// This is the field-mode equivalent of "the enemy is at the door".
static void OnFrame(void)
{
    if (!g_host) return;
    if (!InAnotherRoad()) {
        // Leaving story mode clears the per-city memory so the next entry announces from
        // the real state instead of comparing against a stale one.
        for (int i = 0; i < CITY_COUNT; i++) g_haveRatio[i] = false;
        return;
    }
    char line[192];
    for (int i = 0; i < CITY_COUNT; i++) {
        CityInfo c;
        if (!ReadCity(i, &c) || !c.present || c.max <= 0) { g_haveRatio[i] = false; continue; }
        float r = (float) c.cur / (float) c.max;
        if (!g_haveRatio[i]) { g_lastRatio[i] = r; g_haveRatio[i] = true; continue; }
        float was = g_lastRatio[i];
        g_lastRatio[i] = r;
        if (r >= was) continue;                 // repair is the player's own doing, not an event
        const char* word = nullptr;
        if      (was >= kBandHealthy  && r < kBandHealthy)  word = "below 80 percent";
        else if (was >= kBandHurt     && r < kBandHurt)     word = "below half";
        else if (was >= kBandCritical && r < kBandCritical) word = "critical";
        if (!word) continue;
        snprintf(line, sizeof line, "City %d %s.", c.slot + 1, word);
        // Normal, not Low: it is a discrete state change, but it must not interrupt a
        // decision the way a requested query does. Dedup by slot+band so one crossing
        // never speaks twice.
        char key[48];
        snprintf(key, sizeof key, "dbzar:city%d:%s", c.slot, word);
        SayRaw(line, Priority::Normal, "dbzar-city", key);
    }
}

// ---------------------------------------------------------------------------
// adapter plumbing
// ---------------------------------------------------------------------------
static bool Attach(const Host* host)
{
    g_host = host;
    for (int i = 0; i < CITY_COUNT; i++) g_haveRatio[i] = false;
    return true;   // the game-id check already happened in the registry
}

static void Detach(void)
{
    g_host = nullptr;
    for (int i = 0; i < CITY_COUNT; i++) g_haveRatio[i] = false;
}

/// "Ready" means story mode is up AND the city array is readable. Nothing here speaks a
/// guess, so when this is false the UI shows no reader rather than a stream of zeroes.
static bool Ready(void)
{
    if (!g_host || !InAnotherRoad()) return false;
    CityInfo c;
    return ReadCity(0, &c);
}

static void Command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();    break;
        case Command::DumpState:       CmdDump();        break;
        // The project's command set is menu/battle shaped; field mode needs its own
        // questions, and this adapter answers them through the same dispatcher rather than
        // inventing new commands for one game:
        //   MenuNext / MenuPrev  -> the city list, worst first (a list the player walks)
        //   NextUnactedAlly      -> the nearest city (the "where do I fly" answer)
        //   NextEnemy            -> the chapter/city/enemy summary
        case Command::MenuNext:        CmdCityList();    break;
        case Command::MenuPrev:        CmdCityList();    break;
        case Command::NextUnactedAlly: CmdNearestCity(); break;
        case Command::NextEnemy:       CmdStatus();      break;
        default: break;   // every other command is silently not ours
    }
}

} // namespace dbzar

// NOTE: the instance lives at oga scope (like kDragonBallZSaiyans and
// kDissidiaFinalFantasy) — otherwise the registry's `oga::kDragonBallZAnotherRoad`
// declaration will not link.
extern const Adapter kDragonBallZAnotherRoad;
const Adapter kDragonBallZAnotherRoad = {
    "dbzar",
    "Dragon Ball Z: Shin Budokai - Another Road",
    "ULUS10234",            // PSP game id for Another Road (USA), from PARAM.SFO
    dbzar::Attach,
    dbzar::OnFrame,
    dbzar::Command,
    dbzar::Ready,
    dbzar::Detach,
    nullptr,                // no cue snapshot: field mode has no agreed beacon contract yet
};

} // namespace oga
