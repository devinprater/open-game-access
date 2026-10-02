/*
 * dissidia_adapter_test.cpp — host test for the Dissidia adapter, against SYNTHETIC memory.
 *
 * WHY SYNTHETIC (same rule as dbz_adapter_test.cpp): the adapter's job is to turn bytes
 * into speech, and the bytes that matter are already known exactly (decompile + live RAM).
 * Feeding it a fake RAM image built from that verified layout tests the LOGIC without
 * booting a PSP title, and it runs in milliseconds.
 *
 * ⛔ THE MOCK MUST MAP ABSOLUTE ADDRESSES. The adapter reads ABSOLUTE PSP RAM addresses
 * (0x08xxxxxx); the mock translates them to a buffer offset. Subtract the base, then mask.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t RAM[0x200000];
static const uint32_t RAMBASE = 0x08800000;
static inline size_t OFF(uint32_t a) { return (size_t)((a - RAMBASE) & 0x1FFFFF); }

static char SPOKEN[64][192]; static int NSPOKEN = 0;
static char LOGGED[64][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 64) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 64) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

namespace oga {
extern const Adapter kDissidiaFinalFantasy;
namespace dissidia {
void SetWidgetRoot(uint32_t p);
uint32_t WidgetRoot(void);
const char* DissidiaStoryRunFor(const char* id);
const char* DissidiaStoryTitleFor(const char* id);
}
// Focused-test stubs: the registry TU references the sibling adapters, but this
// binary tests ONLY the Dissidia adapter, so the siblings are null shells that
// are never attached or commanded.
static bool NoAttach(const Host*) { return false; }
static void NoFrame(void) {}
static void NoCommand(Command) {}
static bool NoReady(void) { return false; }
static void NoDetach(void) {}
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
extern const Adapter kDragonBallZSaiyans;
const Adapter kFireEmblemShadowDragon = { "fe11", "stub", "B2FE", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kGameBoyAdvance = { "gba", "stub", "AGB", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
const Adapter kDragonBallZSaiyans = { "dbz", "stub", "BRPE", NoAttach, NoFrame, NoCommand, NoReady, NoDetach };
}

static oga::AnnounceQueue* Q = nullptr;  // recreated by reset(): test isolation
static uint64_t NOW = 0;                  // fake clock, stamped + advanced by hand
static void qspk(void*, const char* t, bool, uint32_t) { spk(nullptr, t, false); }
// Tiny estimates (10 ms + 1 ms/byte) so sync() flushes fast. The queue RULES
// are covered by announce_test.cpp; here we assert adapter logic: the right
// text, groups, and priorities reach the sink.
static oga::Host HOST = { r8, r16, r32, spk, 0, nullptr, lg, btn, nullptr };
static void sync(void)
{
    for (int n = 0; n < 400; n++) {
        if (!oga::announce_in_flight(Q) && oga::announce_pending_count(Q) == 0) break;
        NOW += 25;
        HOST.now_ms = NOW;
        oga::announce_tick(Q, NOW);
    }
}
#define CMD(c) do { a->command(c); sync(); } while (0)
#define FRAME() do { a->on_frame(); sync(); } while (0)

static void put32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void put8(uint32_t a, uint8_t v) { RAM[OFF(a)] = v; }
static void put16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }

// Synthetic layout: manager at 0x08C08EB0 (as observed live); battle root holder
// at 0x08B98940 -> fake root 0x08C168F0; pause widget W = root + 0x234.
static const uint32_t MGR = 0x08C08EB0u;
static const uint32_t BATTLE_HOLDER = 0x08B98940u;
static const uint32_t BROOT = 0x08C168F0u;
static const uint32_t W = BROOT + 0x234u;   // the widget itself

static void reset(void)
{
    memset(RAM, 0, sizeof(RAM));
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); NLOGGED = 0;
    if (Q) oga::announce_destroy(Q);
    oga::AnnounceConfig cfg{};
    cfg.host_reports_done = false;
    cfg.diag_verbose = false;
    cfg.est_base_ms = 10;
    cfg.est_ms_per_byte = 1;
    oga::AnnounceSink sink{};
    sink.speak = qspk;
    sink.log = nullptr;
    sink.ctx = nullptr;
    Q = oga::announce_create(sink, cfg);
    NOW = 0;
    HOST.now_ms = 0;
    HOST.announce_q = Q;
    put32(0x08B9B770u, MGR);   // MGR_SLOT -> manager
    put32(MGR + 0x18u, 3u);    // render count (derived; nonzero = drawing)
    put32(BATTLE_HOLDER, BROOT);
    oga::dissidia::SetWidgetRoot(0);
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

#define SPOKE(i) (i < NSPOKEN ? SPOKEN[i] : "<nothing>")

// ---- Universal OSK reader tests: synthetic OskParams chain (verified layout:
// params AT 0x09B3FAE4 size=64 fc=1 fields=0x09B3FB24; OskData intext/outtext
// 0x09B3FC00, outtextlen 13). Mock maps absolute PSP addresses by masking.
static const uint32_t OSK_P = 0x09B3FAE4u, OSK_F = 0x09B3FB24u, OSK_B = 0x09B3FC00u;
static void putU16str(uint32_t a, const char* s)
{
    for (; *s; s++, a += 2) put16(a, (uint16_t)(unsigned char) *s);
    put16(a, 0);
}
static void oskSetup(const char* seed)
{
    put32(OSK_P, 64u);
    put32(OSK_P + 48u, 1u);
    put32(OSK_P + 52u, OSK_F);
    put32(OSK_F + 32u, OSK_B);
    put32(OSK_F + 36u, 13u);
    put32(OSK_F + 40u, OSK_B);
    putU16str(OSK_B, seed);
}

int main(void)
{
    const oga::Adapter* a = &oga::kDissidiaFinalFantasy;

    CHECK(strcmp(a->id, "dissidia") == 0, "adapter id");
    CHECK(strcmp(a->game_code, "ULUS10437") == 0, "game code ULUS10437");
    CHECK(oga::find_by_game_code("ULUS10437") == a, "registry resolves ULUS10437");

    // 1. attach with valid manager -> ready
    reset();
    CHECK(a->attach(&HOST), "attach returns true");
    CHECK(a->ready(), "ready with valid manager");

    // 2. no widget pinned -> auto-discovers the pause widget from its holder
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    put32(W + 0x3Cu, 0u);
    put32(W + 0x240u, 4u);
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 1 of 4.") == 0,
          "WhereAmI discovers pause widget");
    CHECK(oga::dissidia::WidgetRoot() == W, "discovery pins the widget");

    // 3. pinned root, idx=1 count=4 -> "Row 2 of 4."
    oga::dissidia::SetWidgetRoot(W);
    put32(W + 0x3Cu, 1u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 2 of 4.") == 0, "Row 2 of 4");

    // 4. sentinel idx=-1 -> "No selection."
    put32(W + 0x3Cu, 0xFFFFFFFFu);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "sentinel -1");

    // 5. count=0 -> "No selection."
    put32(W + 0x3Cu, 0u);
    put32(W + 0x240u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "count 0");

    // 6. count above cap (9 > 7) -> "No selection."
    put32(W + 0x240u, 9u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "count above cap");

    // 7. idx == count (out of range) -> "No selection."
    put32(W + 0x3Cu, 4u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "idx == count");

    // 8. manager invalid -> not ready, refuses
    put32(0x08B9B770u, 0u);
    CHECK(!a->ready(), "not ready with null manager");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not ready.") == 0, "refuses when not ready");

    // 9. DumpState logs (does not speak)
    reset();
    oga::dissidia::SetWidgetRoot(W);
    put32(W + 0x3Cu, 2u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); NLOGGED = 0;
    CMD(oga::Command::DumpState);
    CHECK(NSPOKEN == 0, "dump does not speak");
    CHECK(NLOGGED == 2, "dump logs two lines");

    // 10. detach clears the root (never inherit across attach)
    a->detach();
    CHECK(oga::dissidia::WidgetRoot() == 0, "detach clears root");
    CHECK(a->attach(&HOST), "re-attach for remaining tests");

    // 11. closed pause (count 0) -> discovery refuses, says not tracked
    reset();
    put32(W + 0x3Cu, 0xFFFFFFFFu);
    put32(W + 0x240u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not tracked yet.") == 0,
          "closed pause refuses discovery");

    // 12. corrupt holder -> discovery refuses
    reset();
    put32(BATTLE_HOLDER, 0u);
    put32(W + 0x3Cu, 1u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not tracked yet.") == 0,
          "corrupt holder refuses discovery");

    // 13. live pause tags observed on hardware (Return/Quicksave/Quit/Help)
    reset();
    put32(W + 0x3Cu, 2u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 3 of 4.") == 0, "Row 3 of 4");

    // ---- board surfaces (synthetic M/P/B/D + progress chain, live-observed values)
    // The 2 MB mock maps absolute addresses through a mask, applied identically on
    // every access -- so the REAL chain math (chapter stride etc.) runs unmodified
    // with small stand-in bases. Live game validated the real offsets.
    const uint32_t BM = 0x08C168F0u, BP = 0x08C17000u, BB = 0x08C17100u, BD = 0x08C17200u;
    const uint32_t G2 = 0x08900000u;
    const uint32_t C2 = G2 + 0x1AE80u + 0u * 0xF74u;   // chapter 0
    const uint32_t R2 = C2 + 0u * 0x314u + 8u;         // slot 0

    // 14. board WhereAmI: DP 1, cursor home (1,2) -> "DP 1. Home 1, 2."
    reset();
    put32(0x08B9B770u, BM);
    put32(0x08B98940u, BM);
    put32(BM + 0x118u, BP);
    put32(BP + 0x04u, BB);
    put32(BB + 0x10u, BD);
    put32(0x08B99338u, G2);
    put8(BM + 0x120u, 0u);
    put8(C2 + 2u, 0u);
    put16(R2 + 6u, 1u);
    put8(BD + 0x194u, 1u); put8(BD + 0x195u, 2u);
    put32(BD + 0x38u, BB);
    put8(BB + 0x02u, 1u); put8(BB + 0x03u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "DP 1. Home 1, 2.") == 0,
          "board DP + cursor home");

    // 15. cursor away from origin -> "DP 1. 2, 2. Origin 1, 2."
    put8(BD + 0x194u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "DP 1. 2, 2. Origin 1, 2.") == 0,
          "board cursor away");

    // 16. broken chain (B null) -> refuses
    put32(BP + 0x04u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not tracked yet.") == 0,
          "broken board chain refuses");

    // 17. DP unreadable (progress holder null) -> cursor-only speech
    put32(BP + 0x04u, BB);
    put32(0x08B99338u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "2, 2. Origin 1, 2.") == 0,
          "board DP unreadable");

    // ---- grid + markers (live prologue 8x5 bytes, cursor (1,2))
    const uint32_t BG2 = 0x08902000u, BA2 = 0x08902100u, BC2 = 0x08902200u;
    const uint8_t ROW0[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t ROW1[8] = {0, 1, 1, 1, 1, 1, 0, 0};
    const uint8_t ROW2[8] = {0, 1, 1, 1, 1, 1, 0x11, 0};
    const uint8_t ROW3[8] = {0, 1, 1, 1, 1, 1, 0, 0};
    const uint8_t ROW4[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    reset();
    put32(0x08B9B770u, BM);
    put32(0x08B98940u, BM);
    put32(BM + 0x118u, BP);
    put32(BP + 0x04u, BB);
    put32(BB + 0x10u, BD);
    put32(BB + 0x08u, BG2);
    put32(BG2 + 0u, BA2);
    put32(BG2 + 4u, BC2);
    put8(BA2 + 0u, 8u); put8(BA2 + 1u, 5u);
    memcpy(&RAM[OFF(BC2) + 0 * 8], ROW0, 8);
    memcpy(&RAM[OFF(BC2) + 1 * 8], ROW1, 8);
    memcpy(&RAM[OFF(BC2) + 2 * 8], ROW2, 8);
    memcpy(&RAM[OFF(BC2) + 3 * 8], ROW3, 8);
    memcpy(&RAM[OFF(BC2) + 4 * 8], ROW4, 8);
    put8(BD + 0x194u, 1u); put8(BD + 0x195u, 2u);

    // 18. available directions from (1,2)
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Open: east, north, south. Blocked: west.") == 0,
          "directions from (1,2)");

    // 19. marker report with real catalog: slot0 (6,2) key 0 -> type 0 -> enemy
    // NOTE: T/catalog kept clear of the 32-slot span BN..BN+0x1FF.
    const uint32_t BN = 0x08902300u;
    const uint32_t BT = 0x08902800u;
    const uint32_t BCAT = 0x08902900u, BCTAB = 0x08902A00u, BO = 0x08902B00u;
    put32(BB + 0x0Cu, BT);
    put32(BT + 4u, BN);
    put32(BT + 0u, BCAT);
    put32(BCAT + 4u, BCTAB);
    put32(BCAT + 8u, BO);
    put32(BCTAB + 0u, 0u);          // key 0 -> offset 0 -> O
    put16(BO + 4u, 0u);             // type 0 = enemy
    put8(BN + 0u, 0u); put8(BN + 2u, 6u); put8(BN + 3u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "enemy east 5 away at 6, 2.") == 0,
          "marker enemy east");
    // 19b. known species keys speak their live-verified names; unknown keys
    // keep "enemy". O = BO here (key 0 -> offset 0), species key = s16[O+10].
    put16(BO + 10u, 0x30u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "False Hero east 5 away at 6, 2.") == 0,
          "marker False Hero");
    put16(BO + 10u, 0x137u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Delusory Knight east 5 away at 6, 2.") == 0,
          "marker Delusory Knight");
    put16(BO + 10u, 0x138u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Transient Lion east 5 away at 6, 2.") == 0,
          "marker Transient Lion");
    put16(BO + 10u, 0x139u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Imaginary Soldier east 5 away at 6, 2.") == 0,
          "marker Imaginary Soldier");
    put16(BO + 10u, 0x13Au);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Capricious Thief east 5 away at 6, 2.") == 0,
          "marker Capricious Thief");
    put16(BO + 10u, 0x177u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Phantasmal Girl east 5 away at 6, 2.") == 0,
          "marker Phantasmal Girl");
    put16(BO + 10u, 0x178u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Delusory Knight east 5 away at 6, 2.") == 0,
          "marker Delusory Knight Terra");
    put16(BO + 10u, 0x31u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "enemy east 5 away at 6, 2.") == 0,
          "marker unknown species key");
    put16(BO + 10u, 0u);

    // 20. potion + stigma + unknown names
    put32(BCTAB + 4u, 0x10u); put16(BO + 0x14u, 4u);   // key 1 -> type 4
    put32(BCTAB + 8u, 0x20u); put16(BO + 0x24u, 5u);   // key 2 -> type 5
    put32(BCTAB + 12u, 0x30u); put16(BO + 0x34u, 9u);  // key 3 -> type 9 (unknown)
    put8(BN + 0u, 1u); put8(BN + 2u, 5u); put8(BN + 3u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "potion east 4 away at 5, 2.") == 0,
          "marker potion here");
    put8(BN + 0u, 2u); put8(BN + 2u, 6u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Stigma of Chaos east 5 away at 6, 2.") == 0,
          "marker stigma east");
    put8(BN + 0u, 3u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "unknown 9 east 5 away at 6, 2.") == 0,
          "marker unknown type");

    // 21. unreadable catalog -> honest fallback
    put32(BCAT + 8u, 0u);
    put8(BN + 0u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "tile east 5 away at 6, 2.") == 0,
          "marker catalog unreadable");

    // 22. broken grid (G null) -> refuses honestly
    put32(BB + 0x08u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Board unreadable.") == 0,
          "broken grid refuses");

    // ---- battle mode (live retry values: WoL vs False Hero) ----
    // NOTE: battle-holder 0x08B955A0 maps through the mock mask like all addrs.
    const uint32_t BH = 0x08B955A0u;
    const uint32_t BM2 = 0x08903000u, BP0 = 0x08903100u, BP1 = 0x08903200u;
    const uint32_t BS0 = 0x08903300u, BS1 = 0x08903400u;
    reset();
    put32(0x08B9B770u, BM);
    put32(BH, BM2);
    put32(BM2 + 0x14u, BP0);
    put32(BP0 + 0x51Cu, BS0);
    put32(BP0 + 0x2F0u, BP1);
    put32(BP0 + 0x4EA8u, BP1);
    put32(BP1 + 0x51Cu, BS1);
    auto putf = [](uint32_t ad, float v) { memcpy(&RAM[OFF(ad)], &v, 4); };
    put16(BS0 + 0x08u, 1000u); put16(BS0 + 0x02u, 94u);    // HP 906/1000
    put16(BS0 + 0x0Eu, 41u); put16(BS0 + 0x10u, 95u);      // BRV 41/95
    putf(BS0 + 0x14u, 0.0f);
    putf(BP0 + 0x80u, -7.5f); putf(BP0 + 0x84u, 18.4f); putf(BP0 + 0x88u, 41.3f);
    put16(BS1 + 0x08u, 338u); put16(BS1 + 0x02u, 0u);      // HP 338/338
    put16(BS1 + 0x0Eu, 530u); put16(BS1 + 0x10u, 49u);     // BRV 530/49
    putf(BS1 + 0x14u, 912.0f);
    putf(BP1 + 0x80u, -7.5f); putf(BP1 + 0x84u, 2.6f); putf(BP1 + 0x88u, 41.3f);

    // 23. battle self speech
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "HP 906 of 1000. Bravery 41. EX 0.") == 0,
          "battle self");

    // 24. battle foe speech: dy=2.6-18.4=-15.8 -> below; dist=sqrt(15.8^2)=15 (int)
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Enemy HP 338 of 338. Bravery 530. 15 away, below you.") == 0,
          "battle foe");


    // 25. lock states (P0+0x2EC; live: target==enemy at round start)
    // enemy lock: tgt == P1 (paired); dist P0->P1 = 15.8 -> 15
    put32(BP0 + 0x2ECu, BP1);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Locked. 15 away.") == 0,
          "lock enemy");
    // lock off
    put32(BP0 + 0x2ECu, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Lock off.") == 0,
          "lock off");
    // EX-core lock: alternate target in the generic list
    const uint32_t BO1 = 0x08903500u, BO2 = 0x08903600u;
    put32(BP0 + 0x2ECu, BO2);
    put32(BM2 + 0x0Cu, BO1);
    put32(BO1 + 0x490u, BO2);
    put32(BO2 + 0x490u, 0u);
    putf(BO2 + 0x80u, -7.5f); putf(BO2 + 0x84u, 10.0f); putf(BO2 + 0x88u, 41.3f);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "EX core. 8 away.") == 0,
          "lock core");
    // retired target (not in list) -> lost
    put32(BO1 + 0x490u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Lock lost.") == 0,
          "lock lost");

    // 26. down state (dmg >= max)
    put16(BS0 + 0x02u, 1000u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Down. Retry or flee.") == 0,
          "battle down");

    // 27. no battle (holder null) -> board path still works (proves no regression)
    put32(BH, 0u);
    put32(0x08B98940u, BM);
    put32(BM + 0x118u, BP);
    put32(BP + 0x04u, BB);
    put32(BB + 0x10u, BD);
    put32(BB + 0x08u, BG2);
    put32(BG2 + 0u, BA2);
    put32(BG2 + 4u, BC2);
    put8(BA2 + 0u, 8u); put8(BA2 + 1u, 5u);
    memcpy(&RAM[OFF(BC2) + 0 * 8], ROW0, 8);
    memcpy(&RAM[OFF(BC2) + 1 * 8], ROW1, 8);
    memcpy(&RAM[OFF(BC2) + 2 * 8], ROW2, 8);
    memcpy(&RAM[OFF(BC2) + 3 * 8], ROW3, 8);
    memcpy(&RAM[OFF(BC2) + 4 * 8], ROW4, 8);
    put8(BD + 0x194u, 1u); put8(BD + 0x195u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Open: east, north, south. Blocked: west.") == 0,
          "board after battle");

    // ---- Quickmove marker edge (battle only; detector lives app-side) ----
    // Self-contained: re-establishes the battle pointers torn down above.
    reset();
    put32(0x08B9B770u, BM);
    put32(BH, BM2);
    put32(BM2 + 0x14u, BP0);
    put32(BP0 + 0x51Cu, BS0);
    put32(BP0 + 0x2F0u, BP1);
    put32(BP0 + 0x4EA8u, BP1);
    put32(BP1 + 0x51Cu, BS1);
    put16(BS0 + 0x08u, 1000u); put16(BS0 + 0x02u, 94u);
    put16(BS1 + 0x08u, 338u); put16(BS1 + 0x02u, 0u);
    // Q1-Q3. battles stay silent except QTE prompts: edges update the latch only
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::QuickOn);
    CHECK(NSPOKEN == 0, "quickmove silent in battle");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::QuickOn);
    CHECK(NSPOKEN == 0, "quickmove repeat silent");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::QuickOff);
    CHECK(NSPOKEN == 0, "quickoff silent");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::QuickOn);
    CHECK(NSPOKEN == 0, "quickmove re-arm silent");
    // Q4. outside battle QuickOn stays silent (battle gate, not once-rule:
    // an intervening WhereAmI breaks the identical-line chain first).
    // The dialogs block below starts with reset(), so leaving RAM clear
    // here is safe.
    reset();
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1, "nonbattle whereami speaks first");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::QuickOn);
    CHECK(NSPOKEN == 0, "quickmove silent outside battle");

    // ---- EX Mode / EX Burst reader (battle only, detector + input-echo) ----
    reset();
    put32(0x08B9B770u, BM);
    put32(BH, BM2);
    put32(BM2 + 0x14u, BP0);
    put32(BP0 + 0x51Cu, BS0);
    put32(BP0 + 0x2F0u, BP1);
    put32(BP0 + 0x4EA8u, BP1);
    put32(BP1 + 0x51Cu, BS1);
    put16(BS0 + 0x08u, 1000u); put16(BS0 + 0x02u, 94u);
    put16(BS1 + 0x08u, 338u); put16(BS1 + 0x02u, 0u);
    // E1. gauge fills: silent (battles speak QTE only)
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExReady);
    CHECK(NSPOKEN == 0, "ex ready silent");
    // E2. still full: no repeat
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExReady);
    CHECK(NSPOKEN == 0, "ex ready repeat silent");
    // E3. spent then full again: still silent
    CMD(oga::Command::ExSpent);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExReady);
    CHECK(NSPOKEN == 0, "ex re-arm silent");
    // E4. player enters EX Mode (app input-echo): silent
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExActive);
    CHECK(NSPOKEN == 0, "ex active silent");
    // E5. Burst popup live after HP attack: terse prompt
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExBurstGo);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Square!") == 0, "ex burst go");
    // E6. QTE prompts speak raw: identical twice still speaks twice
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExQteUp);
    CMD(oga::Command::ExQteUp);
    CMD(oga::Command::ExQteLeft);
    CMD(oga::Command::ExQteCircle);
    CHECK(NSPOKEN == 4 && strcmp(SPOKE(0), "Up!") == 0 &&
          strcmp(SPOKE(1), "Up!") == 0 && strcmp(SPOKE(2), "Left!") == 0 &&
          strcmp(SPOKE(3), "Circle!") == 0, "ex qte raw repeat");
    // E7. EX over speaks and disarms ready latch
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExEnded);
    CHECK(NSPOKEN == 0, "ex ended silent");
    // E8. EX silent outside battle (gate, not once-rule)
    reset();
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CMD(oga::Command::ExReady);
    CMD(oga::Command::ExQteUp);
    CHECK(NSPOKEN == 1, "ex silent outside battle");

    // E9. Mash-type Burst: go prompt speaks, level-ups never dedup
    reset();
    put32(0x08B9B770u, BM);
    put32(BH, BM2);
    put32(BM2 + 0x14u, BP0);
    put32(BP0 + 0x51Cu, BS0);
    put32(BP0 + 0x2F0u, BP1);
    put32(BP0 + 0x4EA8u, BP1);
    put32(BP1 + 0x51Cu, BS1);
    put16(BS0 + 0x08u, 1000u); put16(BS0 + 0x02u, 94u);
    put16(BS1 + 0x08u, 338u); put16(BS1 + 0x02u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::ExBurstGoMash);
    CMD(oga::Command::ExBurstLevel);
    CMD(oga::Command::ExBurstLevel);
    CHECK(NSPOKEN == 3, "mash burst speaks");
    CHECK(strcmp(SPOKE(0), "Mash Circle!") == 0, "mash go text");
    CHECK(strcmp(SPOKE(1), "Power up!") == 0 &&
          strcmp(SPOKE(2), "Power up!") == 0, "level raw repeat");
    // E10. Mash prompts silent outside battle
    reset();
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CMD(oga::Command::ExBurstGoMash);
    CMD(oga::Command::ExBurstLevel);
    CHECK(NSPOKEN == 1, "mash silent outside battle");


    // ---- YES/NO dialogs (confirm + cancel) + title/setup path to first battle ----
    // The question text is glyph-rendered (no ASCII in RAM), so v1 speaks the
    // selection only. Confirm = Cross on YES, cancel = Cross/Circle on NO: both
    // branches must speak before the player commits, or the choice is a guess.
    const uint32_t DLG_SLOT = 0x08C0CC3Cu, DLG_X = 0x08C0CCB0u;
    const uint32_t SDLG_A = 0x08C0B508u, SDLG_B = 0x08C0B65Cu;

    // 28. battle-slot dialog, glove on YES -> confirm branch speaks
    reset();
    put32(DLG_SLOT, 1u);
    putf(DLG_X, 119.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog YES speaks");

    // 29. dialog nav re-reads RAM (no tracking to desync on a dropped D-pad)
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog nav re-reads YES");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuLeft);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog left re-reads YES");

    // 30. glove moves to NO -> cancel branch speaks
    putf(DLG_X, 266.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "dialog NO speaks");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "dialog nav re-reads NO");

    // 31. story dialog (chapter detail) YES
    reset();
    put32(SDLG_A, 5u); put32(SDLG_B, 1u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "story dialog YES speaks");

    // 32. story dialog NO
    put32(SDLG_A, 4u); put32(SDLG_B, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "story dialog NO speaks");

    // 33. chapter detail WITHOUT dialog (8,13) falls through, never a dialog read
    put32(SDLG_A, 8u); put32(SDLG_B, 13u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not tracked yet.") == 0,
          "no dialog falls through");

    // 34. title menu: fingerprints + cursor -> New Game / Data Install
    reset();
    const uint32_t TT = 0x09DA8D80u, TW = 0x08C18000u;
    put32(TT + 0x0Cu, 4u);
    put32(TT + 0x20u, 1u);
    put32(TT + 0x24u, 0x00190012u);
    put32(0x09A3F0C8u, 1u);
    put32(0x09D8E900u, TW);
    put32(TW, 0x09D90728u);
    put32(TW + 0x0Cu, 4u);
    put32(0x09D90728u + 4u, TW);
    put32(0x09A3F0CCu, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "New Game. Row 1 of 3.") == 0,
          "title New Game");
    put32(0x09A3F0CCu, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Data Install. Row 3 of 3.") == 0,
          "title Data Install");

    // 35. Data Setup > Play Plan after NEW GAME
    reset();
    put32(0x09B3FA30u, 0u); put32(0x09B3FA38u, 2u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Casual. Row 1 of 3.") == 0,
          "play plan Casual");
    put32(0x09B3FA30u, 1u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Average. Row 2 of 3.") == 0,
          "play plan Average");

    // 36. Data Setup > Bonus Day
    reset();
    put32(0x09B3FA30u, 5u); put32(0x09B3FA38u, 6u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Bonus Day. Sat. Row 6 of 7.") == 0,
          "bonus day Sat");

    // 37. first accessible battle screen: self + foe read clean
    reset();
    const uint32_t BH2 = 0x08B955A0u;
    const uint32_t BM3 = 0x08903000u, BP3 = 0x08903100u, BP4 = 0x08903200u;
    const uint32_t BS3 = 0x08903300u, BS4 = 0x08903400u;
    put32(0x08B9B770u, BM);
    put32(BH2, BM3);
    put32(BM3 + 0x14u, BP3);
    put32(BP3 + 0x51Cu, BS3);
    put32(BP3 + 0x2F0u, BP4);
    put32(BP3 + 0x4EA8u, BP4);
    put32(BP4 + 0x51Cu, BS4);
    put16(BS3 + 0x08u, 1000u); put16(BS3 + 0x02u, 94u);
    put16(BS3 + 0x0Eu, 41u); put16(BS3 + 0x10u, 95u);
    putf(BS3 + 0x14u, 0.0f);
    putf(BP3 + 0x80u, -7.5f); putf(BP3 + 0x84u, 18.4f); putf(BP3 + 0x88u, 41.3f);
    put16(BS4 + 0x08u, 338u); put16(BS4 + 0x02u, 0u);
    put16(BS4 + 0x0Eu, 530u); put16(BS4 + 0x10u, 49u);
    putf(BS4 + 0x14u, 912.0f);
    putf(BP4 + 0x80u, -7.5f); putf(BP4 + 0x84u, 2.6f); putf(BP4 + 0x88u, 41.3f);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "HP 906 of 1000. Bravery 41. EX 0.") == 0,
          "first battle self");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Enemy HP 338 of 338. Bravery 530. 15 away, below you.") == 0,
          "first battle foe");

    // ---- Customize tracker (story-map Triangle; host-RE mx-map4) ----
    // No RAM gate (glyph-rendered rows, scans identical): player-in-the-loop
    // toggle. Entry = Abilities row 1; 1:1 moves; wraps both ways (live-verified).
    reset();
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::CustToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Abilities. Row 1 of 9.") == 0,
          "customize toggle on speaks row 1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Abilities. Row 1 of 9.") == 0,
          "customize whereami row 1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Equipment. Row 2 of 9.") == 0,
          "customize next to equipment");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Abilities. Row 1 of 9.") == 0,
          "customize prev back to abilities");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Options. Row 9 of 9.") == 0,
          "customize wrap to options");
    // Modal Help Manual (YES/NO slot) keeps priority over the tracker.
    put32(DLG_SLOT, 1u);
    putf(DLG_X, 119.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "customize dialog keeps priority");
    // Tracker survives dialog close with its row (no RAM to resync from).
    put32(DLG_SLOT, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Options. Row 9 of 9.") == 0,
          "customize row survives dialog");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::CustToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Closed.") == 0,
          "customize toggle off");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "customize nav silent after exit");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    {
        bool clean = true;
        for (int i = 0; i < NSPOKEN; i++)
            if (strstr(SPOKEN[i], "of 9.") != nullptr) clean = false;
        CHECK(clean, "customize rows gone after exit");
    }

    // ---- Character-select tracker (main-menu Triangle; mx-charsel1-4) ----
    // 10 Cosmos heroes I-X, 1:1, wraps 10->1, entry resets to row 1.
    reset();
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::CharToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Warrior of Light. Row 1 of 10.") == 0,
          "charsel toggle on speaks row 1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Firion. Row 2 of 10.") == 0,
          "charsel next to firion");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Onion Knight. Row 3 of 10.") == 0,
          "charsel next to onion knight");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Firion. Row 2 of 10.") == 0,
          "charsel prev back to firion");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Warrior of Light. Row 1 of 10.") == 0,
          "charsel prev back to row 1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Tidus. Row 10 of 10.") == 0,
          "charsel wrap up to tidus");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Warrior of Light. Row 1 of 10.") == 0,
          "charsel wrap down to row 1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::CharToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Closed.") == 0,
          "charsel toggle off");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "charsel nav silent after exit");

    // ---- Title/setup WITHOUT the manager (on-device title gate; s-title-diag) ----
    // On-device the manager slot is not live on the pre-game title, which left
    // the New/Load title silent behind "Game state is not ready yet." The
    // title/setup screens are fingerprinted readers: ready + speaking there
    // must not depend on the manager.
    reset();
    put32(0x08B9B770u, 0u);   // manager slot dead
    {
        const uint32_t TT = 0x09DA8D80u, TW = 0x08C18000u;
        put32(TT + 0x0Cu, 4u);
        put32(TT + 0x20u, 1u);
        put32(TT + 0x24u, 0x00190012u);
        put32(0x09A3F0C8u, 1u);
        put32(0x09D8E900u, TW);
        put32(TW, 0x09D90728u);
        put32(TW + 0x0Cu, 4u);
        put32(0x09D90728u + 4u, TW);
        put32(0x09A3F0CCu, 0u);
    }
    CHECK(a->ready(), "title ready without manager");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "New Game. Row 1 of 3.") == 0,
          "title speaks without manager");
    // D-pad nav re-reads the title cursor (RAM, no tracking to desync).
    put32(0x09A3F0CCu, 1u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Load Game. Row 2 of 3.") == 0,
          "title MenuNext re-reads row");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Load Game. Row 2 of 3.") == 0,
          "title MenuPrev re-reads row");
    // Play Plan without manager: same gate.
    reset();
    put32(0x08B9B770u, 0u);   // manager slot dead
    put32(0x09B3FA30u, 2u); put32(0x09B3FA38u, 2u);
    CHECK(a->ready(), "play plan ready without manager");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Hardcore. Row 3 of 3.") == 0,
          "play plan speaks without manager");
    // Neither title nor manager: genuinely not ready.
    reset();
    put32(0x08B9B770u, 0u);   // manager slot dead, no title either
    CHECK(!a->ready(), "not ready with no manager and no title");

    // 38. OSK toggle refused when the params chain is absent.
    reset();
    put32(0x08B9B770u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Not open.") == 0,
          "osk toggle refused with no chain");

    // 39. OSK toggle on seeds the prefill; WhereAmI reads name + cursor.
    reset();
    oskSetup("PPSSPP");
    CHECK(a->ready(), "osk ready via params chain");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Type your name.") == 0,
          "osk toggle entry line");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name P P S S P P. On 1.") == 0,
          "osk where with prefill");

    // 40. D-pad echo walks the grid (Right 0->1, Down 1->13, Left 13->12,
    // Up 12->0).
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuRight);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "2. Row 1 of 5. Column 2 of 12.") == 0,
          "osk right to 2");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "w. Row 2 of 5. Column 2 of 12.") == 0,
          "osk down to w");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuLeft);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "q. Row 2 of 5. Column 1 of 12.") == 0,
          "osk left to q");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "1. Row 1 of 5. Column 1 of 12.") == 0,
          "osk up back to 1");

    // 41. Type with the game buffer in agreement speaks the echo line.
    putU16str(OSK_B, "PPSSPP1");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskType);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "1. Name P P S S P P 1.") == 0,
          "osk type grounded");

    // 42. Type with a stale buffer resyncs to RAM and says so.
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskDelete);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name P P S S P P 1.") == 0,
          "osk delete mismatch resyncs to RAM");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name P P S S P P 1. On 1.") == 0,
          "osk mirror follows RAM after resync");

    // 43. Finish announces the final name and stops tracking.
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskFinish);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name P P S S P P 1.") == 0,
          "osk finish announces name");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskType);
    CHECK(NSPOKEN == 0, "osk type silent after finish");

    // 44. Play Plan validating mid-track auto-exits with the final name.
    reset();
    oskSetup("PPSSPP");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskToggle);
    put32(0x09B3FA30u, 0u); put32(0x09B3FA38u, 2u);  // Play Plan Casual
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name P P S S P P.") == 0,
          "osk auto-exit announces name on Play Plan");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Casual. Row 1 of 3.") == 0,
          "play plan speaks after osk exit");

    // 45. Toggle off announces the final name.
    reset();
    oskSetup("AB");
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskToggle);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
    CMD(oga::Command::OskToggle);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Name A B.") == 0,
          "osk toggle off announces name");

    // ---- Frame-polled auto-speech watch (menus speak without a tap) ----
    // on_frame runs every emulated frame via poke_frame. Two consecutive
    // validations before speaking (flicker guard); silence on steady rows.
    // 46. title entry speaks after two frames, then stays silent
    reset();
    CHECK(a->attach(&HOST), "watch attach");
    put32(TT + 0x0Cu, 4u);
    put32(TT + 0x20u, 1u);
    put32(TT + 0x24u, 0x00190012u);
    put32(0x09A3F0C8u, 1u);
    put32(0x09D8E900u, TW);
    put32(TW, 0x09D90728u);
    put32(TW + 0x0Cu, 4u);
    put32(0x09D90728u + 4u, TW);
    put32(0x09A3F0CCu, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME();
    CHECK(NSPOKEN == 0, "watch silent on first sighting");
    FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "New Game. Row 1 of 3.") == 0,
          "watch speaks title entry");

    // 47. steady row stays silent; cursor move speaks after two frames
    FRAME();
    CHECK(NSPOKEN == 1, "watch silent on steady row");
    put32(0x09A3F0CCu, 1u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME();
    CHECK(NSPOKEN == 0, "watch silent on first changed frame");
    FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Load Game. Row 2 of 3.") == 0,
          "watch speaks cursor move");

    // 48. broken fingerprints silence the watch; restore re-announces
    put32(0x09A3F0C8u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 0, "watch silent when fingerprints break");
    put32(0x09A3F0C8u, 1u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Load Game. Row 2 of 3.") == 0,
          "watch re-announces after restore");

    // 48b. story-dialogue portrait names the speaker (verified IDs only)
    reset();
    CHECK(a->attach(&HOST), "watch attach story");
    put8(0x08BB376Au, 0x9Au);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Garland.") == 0,
          "story portrait names Garland");
    put8(0x08BB376Au, 0x9Eu);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Chaos.") == 0,
          "story portrait change names Chaos");
    put8(0x08BB376Au, 0x0Cu);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 0, "unverified portrait stays silent");
    put8(0x08BB376Au, 0x46u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Chaos.") == 0,
          "second Chaos variant names Chaos");

    // 48c. story scene table: DO 3 (throne scene) run + DO 5 (WoL/Garland)
    {
        const char* r3 = oga::dissidia::DissidiaStoryRunFor("DO 3");
        CHECK(r3 && strstr(r3, "Chaos|Garland") != nullptr,
              "DO 3 run carries Chaos/Garland");
        const char* r5 = oga::dissidia::DissidiaStoryRunFor("DO 5");
        CHECK(r5 && strcmp(r5, "Warrior of Light|Garland|Warrior of Light|Garland|Warrior of Light|Garland") == 0,
              "DO 5 run alternates WoL/Garland");
        CHECK(oga::dissidia::DissidiaStoryRunFor("DO 999") == nullptr, "unknown scene id is null");
        const char* t3 = oga::dissidia::DissidiaStoryTitleFor("DO 3");
        CHECK(t3 && strcmp(t3, "Prologue") == 0, "DO 3 title is Prologue");
    }

    // 49. main-menu entry announces row 1 (TITLE_CURSOR 8 + live A10 chain)
    reset();
    CHECK(a->attach(&HOST), "watch attach main");
    put32(0x09A3F0CCu, 8u);
    put32(0x09B3FA10u, 0x08C18100u);
    put32(0x08C18100u, 0x08C18200u);
    put32(0x08C18208u, 0x08C18300u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Story Mode. Row 1 of 8.") == 0,
          "watch announces main-menu entry");

    // 50. a live board suspends the watch (cursor could be stale menu pins)
    put32(BROOT + 0x118u, 0x08C18400u);
    put32(0x08C18400u + 0x04u, 0x08C18500u);
    put32(0x08C18500u + 0x10u, 0x08C18600u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 0, "watch silent on live board");
    put32(BROOT + 0x118u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Story Mode. Row 1 of 8.") == 0,
          "watch resumes after board clears");

    // 51. a live battle suspends the watch even with title pins set
    reset();
    CHECK(a->attach(&HOST), "watch attach battle");
    put32(TT + 0x0Cu, 4u);
    put32(TT + 0x20u, 1u);
    put32(TT + 0x24u, 0x00190012u);
    put32(0x09A3F0C8u, 1u);
    put32(0x09D8E900u, TW);
    put32(TW, 0x09D90728u);
    put32(TW + 0x0Cu, 4u);
    put32(0x09D90728u + 4u, TW);
    put32(0x09A3F0CCu, 0u);
    put32(0x08B955A0u, 0x08C19000u);
    put32(0x08C19000u + 0x14u, 0x08C19100u);
    put32(0x08C19100u + 0x51Cu, 0x08C19200u);
    put16(0x08C19200u + 0x08u, 5000u);
    put16(0x08C19200u + 0x02u, 0u);
    put16(0x08C19200u + 0x0Eu, 100u);
    put16(0x08C19200u + 0x10u, 100u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 0, "watch silent in battle");
    put32(0x08B955A0u, 0u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "New Game. Row 1 of 3.") == 0,
          "watch resumes after battle clears");

    // 52. YES/NO dialog appearance auto-speaks the selection
    reset();
    CHECK(a->attach(&HOST), "watch attach dialog");
    put32(DLG_SLOT, 1u);
    putf(DLG_X, 119.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "watch announces dialog entry");
    putf(DLG_X, 266.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "watch announces dialog move");

    // 53. options-menu entry announces row 1
    reset();
    CHECK(a->attach(&HOST), "watch attach options");
    put32(0x09B3FCA4u, 6u); put32(0x09B3FCACu, 8u); put32(0x09B43468u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Battle Tutorials. Row 1 of 23.") == 0,
          "watch announces options entry");

    // ---- Board arrival watch (54-56): cursor reaching a marker speaks it ----
    // Synthetic board bundle + marker array + catalog (verified layout: T at
    // B+0x0C holds C=[T+0] and N=[T+4]; K=[C+4], base=[C+8], type=s16[base+off+4]).
    reset();
    CHECK(a->attach(&HOST), "watch attach board");
    const uint32_t WBP = 0x08C18400u, WBB = 0x08C18500u, WBD = 0x08C18600u;
    const uint32_t WBT = 0x08C18700u, WBC = 0x08C18800u, WBN = 0x08C18900u;
    const uint32_t WBK = 0x08C19A00u, WBASE = 0x08C19B00u;
    put32(BROOT + 0x118u, WBP);
    put32(WBP + 0x04u, WBB);
    put32(WBB + 0x10u, WBD);
    put32(WBB + 0x0Cu, WBT);
    put32(WBT, WBC); put32(WBT + 4u, WBN);
    put32(WBC + 4u, WBK); put32(WBC + 8u, WBASE);
    put32(WBK + 12u, 0x20u); put16(WBASE + 0x20u + 4u, 5u);   // key3 -> stigma
    put32(WBK + 16u, 0x30u); put16(WBASE + 0x30u + 4u, 4u);   // key4 -> potion
    put32(WBK + 0u, 0x40u); put16(WBASE + 0x40u + 4u, 0u);    // key0 -> enemy
    put8(WBN + 0, 3); put8(WBN + 2, 6); put8(WBN + 3, 2);       // stigma at 6,2
    put8(WBN + 0x10 + 0, 4); put8(WBN + 0x10 + 2, 1); put8(WBN + 0x10 + 3, 1);  // potion at 1,1
    put8(WBN + 0x20 + 0, 0); put8(WBN + 0x20 + 2, 2); put8(WBN + 0x20 + 3, 3);  // enemy at 2,3
    put8(WBN + 0x20 + 0x0C, 1);                               // nonzero flag: key 0 is real
    // 54. arriving on the stigma speaks it, then steadies to silence
    put8(WBD + 0x194u, 6); put8(WBD + 0x195u, 2);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Stigma of Chaos. Engaging this piece finishes the level.") == 0,
          "watch announces stigma arrival");
    FRAME();
    CHECK(NSPOKEN == 1, "watch silent standing on stigma");
    // 55. empty cells stay silent; potion arrival speaks
    put8(WBD + 0x194u, 0); put8(WBD + 0x195u, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 0, "watch silent on empty cell");
    put8(WBD + 0x194u, 1); put8(WBD + 0x195u, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Potion. Restores HP and EX Gauge to 100%.") == 0,
          "watch announces potion arrival");
    // 56. enemy arrival (key 0 is a real marker, not absent); unknown types
    // speak as unknown with the number, never a guess
    put8(WBD + 0x194u, 2); put8(WBD + 0x195u, 3);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 2 && strcmp(SPOKE(0), "enemy here.") == 0 &&
          strcmp(SPOKE(1), "Press X to engage.") == 0,
          "watch announces enemy arrival plus engage hint");
    put16(WBASE + 0x30u + 4u, 9u);
    put8(WBD + 0x194u, 1); put8(WBD + 0x195u, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Unknown 9 here.") == 0,
          "watch announces unknown type with number");
    // 56b. DP-zero alert speaks once per emptying (progress chain mirrors
    // test 14: G2/C2/R2 with slot 0); enemy arrivals keep the engage hint.
    put32(0x08B98940u, BM);
    put32(0x08B99338u, G2);
    put8(BM + 0x120u, 0u);
    put8(C2 + 2u, 0u);
    put16(R2 + 6u, 0u);
    put8(WBD + 0x194u, 2); put8(WBD + 0x195u, 3);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 3 && strcmp(SPOKE(0), "enemy here.") == 0 &&
          strcmp(SPOKE(1), "Press X to engage.") == 0 &&
          strcmp(SPOKE(2), "You're out of Destiny Points!") == 0,
          "dp-zero alert on first empty arrival");
    put8(WBD + 0x194u, 6); put8(WBD + 0x195u, 2);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 1,
          "dp-zero alert not repeated on next arrival");
    put16(R2 + 6u, 1u);
    put8(WBD + 0x194u, 2); put8(WBD + 0x195u, 3);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    FRAME(); FRAME();
    CHECK(NSPOKEN == 2 && strcmp(SPOKE(0), "enemy here.") == 0 &&
          strcmp(SPOKE(1), "Press X to engage.") == 0,
          "no dp alert once DP refills");

    // ---- Queue mapping: interruption, pacing, replacement (fake clock) ----
    // Raw a->command bypasses the CMD() auto-sync so lines stay queued.
    // Q1. Two rapid WhereAmI answers (High, same "whereami" group): the second
    // interrupts the first; both speak, in order.
    reset();
    CHECK(a->attach(&HOST), "q attach");
    oga::dissidia::SetWidgetRoot(W);
    put32(W + 0x3Cu, 0u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    put32(W + 0x3Cu, 1u);
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 2 && strcmp(SPOKE(0), "Row 1 of 4.") == 0 &&
          strcmp(SPOKE(1), "Row 2 of 4.") == 0, "rapid answers both speak in order");
    CHECK(oga::announce_stats(Q).interrupted == 1, "second answer interrupted first");
    // Q2. A Normal "qte" line queued behind an in-flight High waits for the
    // estimate; it must not speak early.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sync();
    put32(W + 0x3Cu, 2u);
    a->command(oga::Command::WhereAmI);
    put32(0x08B955A0u, 0x08903000u);
    put32(0x08903000u + 0x14u, 0x08903100u);
    put32(0x08903100u + 0x51Cu, 0x08903300u); put16(0x08903300u + 0x08u, 1000u);
    a->command(oga::Command::ExQteUp);
    CHECK(NSPOKEN == 1, "qte waits while answer in flight");
    sync();
    CHECK(NSPOKEN == 2 && strcmp(SPOKE(1), "Up!") == 0, "qte speaks after estimate");
    // Q3. Three rapid QTE prompts (Normal, same "qte" group): the middle one is
    // replaced while pending; only first and last speak.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sync();
    a->command(oga::Command::ExQteUp);
    a->command(oga::Command::ExQteDown);
    a->command(oga::Command::ExQteLeft);
    sync();
    CHECK(NSPOKEN == 2 && strcmp(SPOKE(0), "Up!") == 0 &&
          strcmp(SPOKE(1), "Left!") == 0, "middle qte replaced");
    CHECK(oga::announce_stats(Q).replaced == 1, "replacement counted");

    if (failures == 0) printf("\nALL DISSIDIA ADAPTER TESTS PASSED\n");
    else printf("\n%d FAILURES\n", failures);
    return failures != 0;
}
