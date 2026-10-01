/*
 * dq9_adapter_test.cpp — host test for the Dragon Quest IX adapter.
 *
 * WHY SYNTHETIC: the adapter turns the mod's documented addresses into
 * speech, and the layout is known exactly (party names at 0x020F3888 with
 * stride 0x964, map code at 0x020FB3FC, player pointer at 0x020F33E0). A fake
 * RAM image built from that layout tests the adapter's LOGIC — validation,
 * bounds, wording, refusal — in milliseconds, with no ROM.
 *
 * ⛔ THE MOCK MAPS ABSOLUTE ADDRESSES. The adapter reads absolute RAM
 * addresses; the mock subtracts the base first. Indexing the buffer with the
 * absolute address overflows, and that crash reads as an adapter bug when it
 * is a test bug.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t RAM[0x400000];
static const uint32_t RAMBASE = 0x02000000;
static inline size_t OFF(uint32_t a) { return (size_t)((a - RAMBASE) & 0x3FFFFF); }

static char SPOKEN[64][192]; static int NSPOKEN = 0;
static char LOGGED[64][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 64) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 64) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

// ⛔ DECLARE IT INSIDE THE NAMESPACE. `extern const oga::Adapter name;` at
// global scope declares a DIFFERENT variable and the link error reads as a
// missing adapter rather than a wrong declaration.
namespace oga {
extern const Adapter kDragonQuestIX;
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
}

static void put8(uint32_t a, uint8_t v) { RAM[OFF(a)] = v; }
static void put32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void putbytes(uint32_t a, const char* s, size_t n) { memcpy(&RAM[OFF(a)], s, n); }

static const uint32_t PARTY0 = 0x020F3888u, PSTRIDE = 0x964u;
static const uint32_t MAPCODE = 0x020FB3FCu, PPTR = 0x020F33E0u;
static const uint32_t GOLD = 0x020F6D48u, BATTLE = 0x020EF0E8u;
static const uint32_t CAMERA = 0x0210A134u;
static const uint32_t OBJTABLE = 0x02107600u, OBJCOUNT = 0x02107680u;
static const uint32_t MENU = 0x02118FFCu, PHASE = 0x02109DA6u, CHOICE = 0x021153A4u;

int main()
{
    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;

    int pass = 0, fail = 0;
    #define CHECK(c,m) do { if (c) { pass++; } else { fail++; printf("  FAIL: %s\n", m); } } while (0)

    oga::kDragonQuestIX.attach(&H);

    // Empty RAM: nothing allocated yet — refuse, do not speak garbage.
    CHECK(!oga::kDragonQuestIX.ready(), "not ready on empty RAM");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "empty party says so instead of reading zeros");

    // A live title snapshot (dq9-t0.ram, Oct 2026): slot 0 holds printable junk
    // ("NineRZ") with no map code, battle flag SET, empty menu buffer. Every
    // command must refuse; nothing may speak the junk.
    putbytes(PARTY0, "NineRZ", 7);
    put8(BATTLE, 1);
    CHECK(!oga::kDragonQuestIX.ready(), "title junk without a map is not ready");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "title junk never cycles as a party member");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No menu"),
          "title with set battle flag but empty buffer reads as no menu");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Position unknown"),
          "title with no player pointer has no nearby");
    memset(&RAM[OFF(PARTY0)], 0, 32);
    put8(BATTLE, 0);
    // Build the world the mod documents: map code, player placed, two named
    // members, some gold, exploring (not battle).
    putbytes(MAPCODE, "M01M0100", 8);
    put32(PPTR, 0x02100000u);
    put32(0x02100044u, (uint32_t)(10 * 4096));
    put32(0x0210004Cu, (uint32_t)(20 * 4096));
    putbytes(PARTY0, "Hero", 5);
    putbytes(PARTY0 + PSTRIDE, "Biscuit", 8);
    put32(GOLD, 1234);
    put8(BATTLE, 0);

    CHECK(oga::kDragonQuestIX.ready(), "ready once slot 0 has a name");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "M01M0100") && strstr(SPOKEN[0], "10")
                       && strstr(SPOKEN[0], "Exploring"),
          "WhereAmI reports map code, position and exploring");

    put8(BATTLE, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "In battle"),
          "WhereAmI reports battle when the flag is set");
    put8(BATTLE, 0);

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Hero") && strstr(SPOKEN[0], "1 of 4"),
          "first NextAlly names slot 0 with position");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Biscuit"),
          "second NextAlly steps to slot 1");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::PrevAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Hero"),
          "PrevAlly steps back");

    // A record whose name is not text — isolated so the walk cannot hide it
    // behind a valid neighbour.
    memset(&RAM[OFF(PARTY0)], 0, 32);
    memset(&RAM[OFF(PARTY0 + PSTRIDE)], 0, 32);
    RAM[OFF(PARTY0)] = 0x01; RAM[OFF(PARTY0) + 1] = 0x02;
    CHECK(!oga::kDragonQuestIX.ready(), "non-text slot 0 is not ready");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a non-text record is rejected, not spoken");
    putbytes(PARTY0, "Hero", 5);
    putbytes(PARTY0 + PSTRIDE, "Biscuit", 8);

    NLOGGED = 0;
    oga::kDragonQuestIX.command(oga::Command::DumpState);
    CHECK(NLOGGED >= 3 && strstr(LOGGED[0], "M01M0100") && strstr(LOGGED[0], "1234"),
          "DumpState logs map code and gold");

    // Nearby scan: player at tile (10,20), camera south of the player so
    // screen-up is world +z. Object 0 is a person 5 tiles up; object 1 is
    // a story model 20 tiles to the side (farther, spoken second).
    put32(CAMERA, (uint32_t)(10 * 4096));
    put32(CAMERA + 8, (uint32_t)(12 * 4096));
    put32(OBJCOUNT, 2);
    put32(OBJTABLE, 0x02110000u);
    put32(OBJTABLE + 4, 0x02111000u);
    put32(0x02110008u, 0x02112000u);
    putbytes(0x02112004u, "n001a", 6);
    put32(0x02110024u, (uint32_t)(10 * 4096));
    put32(0x0211002Cu, (uint32_t)(25 * 4096));
    put32(0x02111008u, 0x02113000u);
    putbytes(0x02113004u, "s025x", 6);
    put32(0x02111064u, (uint32_t)(30 * 4096));
    put32(0x0211106Cu, (uint32_t)(20 * 4096));

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Someone") && strstr(SPOKEN[0], "5 steps up"),
          "NextEnemy names the nearest person with camera-relative direction");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Story character") && strstr(SPOKEN[0], "20 steps left"),
          "second NextEnemy steps to the farther story model");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Someone"),
          "PrevEnemy steps back to the nearest");

    // An over-count is a corrupt table, not a crowd: silence, not 65 labels.
    put32(OBJCOUNT, 99);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Nobody nearby"),
          "over-count object table reads as nobody nearby");
    put32(OBJCOUNT, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Nobody nearby"),
          "empty object table reads as nobody nearby");

    // Menu echo: cursor + items straight from the markup buffer.
    putbytes(MENU, "<CURSOR=1><N=0>Fight</N><N=1>Spells</N>", 39);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Menu. Spells. 2 items"),
          "MenuState echoes the cursor item with the item count");

    // In battle away from the command phase the buffer can hold a pre-built
    // list: say busy, never the stale words.
    put8(BATTLE, 1); put8(PHASE, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Menu. Busy"),
          "battle menu away from the command phase reads as busy");
    put8(BATTLE, 0);

    // Yes/no prompt with no item list: name the choice, never the cursor.
    RAM[OFF(MENU)] = 0;
    put32(CHOICE, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Choose. Yes or no"),
          "event choice names the prompt without guessing the cursor");
    put32(CHOICE, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No menu"),
          "no markup and no prompt reads as no menu");

    // Registry: YDQE must resolve to this adapter.
    CHECK(oga::find_by_game_code("YDQE") == &oga::kDragonQuestIX,
          "registry resolves YDQE to the DQ9 adapter");

    oga::kDragonQuestIX.detach();
    CHECK(!oga::kDragonQuestIX.ready(), "detach clears readiness");

    printf("  dq9 adapter: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

// Stubs so adapters.o links without dragging in the sibling machinery.
namespace oga {
static bool s_att(const Host*) { return false; }
static void s_frm() {}
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11","FE","YFEE",s_att,s_frm,s_cmd,s_rdy,s_det};
const Adapter kGameBoyAdvance = {"gba","GBA","",s_att,s_frm,s_cmd,s_rdy,s_det};
}
