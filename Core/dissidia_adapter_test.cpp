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

static const oga::Host HOST = { r8, r16, r32, spk, lg, btn, nullptr };

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
    NSPOKEN = 0; NLOGGED = 0;
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
    NSPOKEN = 0;
    put32(W + 0x3Cu, 0u);
    put32(W + 0x240u, 4u);
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 1 of 4.") == 0,
          "WhereAmI discovers pause widget");
    CHECK(oga::dissidia::WidgetRoot() == W, "discovery pins the widget");

    // 3. pinned root, idx=1 count=4 -> "Row 2 of 4."
    oga::dissidia::SetWidgetRoot(W);
    put32(W + 0x3Cu, 1u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 2 of 4.") == 0, "Row 2 of 4");

    // 4. sentinel idx=-1 -> "No selection."
    put32(W + 0x3Cu, 0xFFFFFFFFu);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "sentinel -1");

    // 5. count=0 -> "No selection."
    put32(W + 0x3Cu, 0u);
    put32(W + 0x240u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "count 0");

    // 6. count above cap (9 > 7) -> "No selection."
    put32(W + 0x240u, 9u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "count above cap");

    // 7. idx == count (out of range) -> "No selection."
    put32(W + 0x3Cu, 4u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "No selection.") == 0, "idx == count");

    // 8. manager invalid -> not ready, refuses
    put32(0x08B9B770u, 0u);
    CHECK(!a->ready(), "not ready with null manager");
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Game not ready yet.") == 0, "refuses when not ready");

    // 9. DumpState logs (does not speak)
    reset();
    oga::dissidia::SetWidgetRoot(W);
    put32(W + 0x3Cu, 2u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0; NLOGGED = 0;
    a->command(oga::Command::DumpState);
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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Menu not tracked yet.") == 0,
          "closed pause refuses discovery");

    // 12. corrupt holder -> discovery refuses
    reset();
    put32(BATTLE_HOLDER, 0u);
    put32(W + 0x3Cu, 1u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Menu not tracked yet.") == 0,
          "corrupt holder refuses discovery");

    // 13. live pause tags observed on hardware (Return/Quicksave/Quit/Help)
    reset();
    put32(W + 0x3Cu, 2u);
    put32(W + 0x240u, 4u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Row 3 of 4.") == 0, "Row 3 of 4");

    // ---- board surfaces (synthetic M/P/B/D + progress chain, live-observed values)
    // The 2 MB mock maps absolute addresses through a mask, applied identically on
    // every access -- so the REAL chain math (chapter stride etc.) runs unmodified
    // with small stand-in bases. Live game validated the real offsets.
    const uint32_t BM = 0x08C168F0u, BP = 0x08C17000u, BB = 0x08C17100u, BD = 0x08C17200u;
    const uint32_t G2 = 0x08900000u;
    const uint32_t C2 = G2 + 0x1AE80u + 0u * 0xF74u;   // chapter 0
    const uint32_t R2 = C2 + 0u * 0x314u + 8u;         // slot 0

    // 14. board WhereAmI: DP 1, cursor home (1,2) -> "DP 1. Cursor home at 1, 2."
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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "DP 1. Cursor home at 1, 2.") == 0,
          "board DP + cursor home");

    // 15. cursor away from origin -> "DP 1. Cursor 2, 2. Origin 1, 2."
    put8(BD + 0x194u, 2u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "DP 1. Cursor 2, 2. Origin 1, 2.") == 0,
          "board cursor away");

    // 16. broken chain (B null) -> refuses
    put32(BP + 0x04u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Menu not tracked yet.") == 0,
          "broken board chain refuses");

    // 17. DP unreadable (progress holder null) -> cursor-only speech
    put32(BP + 0x04u, BB);
    put32(0x08B99338u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Cursor 2, 2. Origin 1, 2.") == 0,
          "board DP unreadable");

    if (failures == 0) printf("\nALL DISSIDIA ADAPTER TESTS PASSED\n");
    else printf("\n%d FAILURES\n", failures);
    return failures != 0;
}
