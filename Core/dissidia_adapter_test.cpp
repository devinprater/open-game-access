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
    NSPOKEN = 0;
    a->command(oga::Command::NextAlly);
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
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "enemy east 5 away at 6, 2.") == 0,
          "marker enemy east");

    // 20. potion + stigma + unknown names
    put32(BCTAB + 4u, 0x10u); put16(BO + 0x14u, 4u);   // key 1 -> type 4
    put32(BCTAB + 8u, 0x20u); put16(BO + 0x24u, 5u);   // key 2 -> type 5
    put32(BCTAB + 12u, 0x30u); put16(BO + 0x34u, 9u);  // key 3 -> type 9 (unknown)
    put8(BN + 0u, 1u); put8(BN + 2u, 5u); put8(BN + 3u, 2u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "potion east 4 away at 5, 2.") == 0,
          "marker potion here");
    put8(BN + 0u, 2u); put8(BN + 2u, 6u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Stigma of Chaos east 5 away at 6, 2.") == 0,
          "marker stigma east");
    put8(BN + 0u, 3u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "unknown object type 9 east 5 away at 6, 2.") == 0,
          "marker unknown type");

    // 21. unreadable catalog -> honest fallback
    put32(BCAT + 8u, 0u);
    put8(BN + 0u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "special tile east 5 away at 6, 2.") == 0,
          "marker catalog unreadable");

    // 22. broken grid (G null) -> refuses honestly
    put32(BB + 0x08u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Board map unreadable.") == 0,
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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "HP 906 of 1000. Bravery 41. EX 0 percent.") == 0,
          "battle self");

    // 24. battle foe speech: dy=2.6-18.4=-15.8 -> below; dist=sqrt(15.8^2)=15 (int)
    NSPOKEN = 0;
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Enemy: HP 338 of 338. Bravery 530. 15 away, below you.") == 0,
          "battle foe");

    // 25. lock states (P0+0x2EC; live: target==enemy at round start)
    // enemy lock: tgt == P1 (paired); dist P0->P1 = 15.8 -> 15
    put32(BP0 + 0x2ECu, BP1);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Locked on the enemy. 15 away.") == 0,
          "lock enemy");
    // lock off
    put32(BP0 + 0x2ECu, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Lock off.") == 0,
          "lock off");
    // EX-core lock: alternate target in the generic list
    const uint32_t BO1 = 0x08903500u, BO2 = 0x08903600u;
    put32(BP0 + 0x2ECu, BO2);
    put32(BM2 + 0x0Cu, BO1);
    put32(BO1 + 0x490u, BO2);
    put32(BO2 + 0x490u, 0u);
    putf(BO2 + 0x80u, -7.5f); putf(BO2 + 0x84u, 10.0f); putf(BO2 + 0x88u, 41.3f);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Locked on the EX core. 8 away.") == 0,
          "lock core");
    // retired target (not in list) -> lost
    put32(BO1 + 0x490u, 0u);
    NSPOKEN = 0;
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Lock target lost.") == 0,
          "lock lost");

    // 26. down state (dmg >= max)
    put16(BS0 + 0x02u, 1000u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "You are down. Retry or flee.") == 0,
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
    NSPOKEN = 0;
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Open: east, north, south. Blocked: west.") == 0,
          "board after battle");

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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog YES speaks");

    // 29. dialog nav re-reads RAM (no tracking to desync on a dropped D-pad)
    NSPOKEN = 0;
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog nav re-reads YES");
    NSPOKEN = 0;
    a->command(oga::Command::MenuLeft);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "dialog left re-reads YES");

    // 30. glove moves to NO -> cancel branch speaks
    putf(DLG_X, 266.0f);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "dialog NO speaks");
    NSPOKEN = 0;
    a->command(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "dialog nav re-reads NO");

    // 31. story dialog (chapter detail) YES
    reset();
    put32(SDLG_A, 5u); put32(SDLG_B, 1u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "YES. Row 1 of 2.") == 0,
          "story dialog YES speaks");

    // 32. story dialog NO
    put32(SDLG_A, 4u); put32(SDLG_B, 2u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "NO. Row 2 of 2.") == 0,
          "story dialog NO speaks");

    // 33. chapter detail WITHOUT dialog (8,13) falls through, never a dialog read
    put32(SDLG_A, 8u); put32(SDLG_B, 13u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Menu not tracked yet.") == 0,
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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "New Game. Row 1 of 3.") == 0,
          "title New Game");
    put32(0x09A3F0CCu, 2u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Data Install. Row 3 of 3.") == 0,
          "title Data Install");

    // 35. Data Setup > Play Plan after NEW GAME
    reset();
    put32(0x09B3FA30u, 0u); put32(0x09B3FA38u, 2u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Casual. Row 1 of 3.") == 0,
          "play plan Casual");
    put32(0x09B3FA30u, 1u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Play Plan. Average. Row 2 of 3.") == 0,
          "play plan Average");

    // 36. Data Setup > Bonus Day
    reset();
    put32(0x09B3FA30u, 5u); put32(0x09B3FA38u, 6u);
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
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
    NSPOKEN = 0;
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "HP 906 of 1000. Bravery 41. EX 0 percent.") == 0,
          "first battle self");
    NSPOKEN = 0;
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strcmp(SPOKE(0), "Enemy: HP 338 of 338. Bravery 530. 15 away, below you.") == 0,
          "first battle foe");

    if (failures == 0) printf("\nALL DISSIDIA ADAPTER TESTS PASSED\n");
    else printf("\n%d FAILURES\n", failures);
    return failures != 0;
}
