/*
 * alttp_adapter_test.cpp — host test for the A Link to the Past adapter.
 *
 * Synthetic 128 KiB work RAM at 0x7E0000, a stub Host, and the adapter's own
 * logic under test: validation, silence while booting, and the debug dump.
 * No ROM, no boot.
 *
 * ⛔ THE MOCK MAPS ABSOLUTE ADDRESSES. Indexing the buffer with a 0x7E… address
 * overflows; subtract the base first. A crash here reads as an adapter bug when
 * it is a test bug.
 */
#include "adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t RAM[0x20000];
static const uint32_t RAMBASE = 0x7E0000u;
static inline size_t OFF(uint32_t a) { return (size_t) (a - RAMBASE); }

static char SPOKEN[32][192]; static int NSPOKEN = 0;
static char LOGGED[32][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 32) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 32) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

namespace oga {
extern const Adapter kZeldaLinkToThePast;
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
}

static void put8(uint32_t a, uint8_t v) { RAM[OFF(a)] = v; }
static void put16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }

static const uint32_t CAP = 0x7EF36Cu, CUR = 0x7EF36Du, MAGIC = 0x7EF36Eu;
static const uint32_t AREA = 0x7E040Au, SCREEN = 0x7E008Au, ROOM = 0x7E00A0u;
static const uint32_t X = 0x7E0022u, Y = 0x7E0020u, INDOORS = 0x7E001Bu;
static const uint32_t RUPEES = 0x7EF362u;

int main()
{
    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;

    int pass = 0, fail = 0;
    #define CHECK(c, m) do { if (c) { pass++; printf("  ok   %s\n", m); } else { fail++; printf("  FAIL %s\n", m); } } while (0)

    // Boot: RAM empty. attach wires the host, but nothing is readable yet.
    oga::kZeldaLinkToThePast.attach(&H);
    CHECK(!oga::kZeldaLinkToThePast.ready(), "not ready on empty RAM");

    NSPOKEN = 0;
    oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 0, "silent while the game is still booting");

    // A playable state: three hearts, health full, in Link's house.
    put8(CAP, 0x18);
    put8(CUR, 0x18);
    put16(X, 0x0800);
    put16(Y, 0x0800);
    put16(AREA, 3);
    put16(SCREEN, 4);
    put8(INDOORS, 0);
    put8(MAGIC, 0x80);
    put16(RUPEES, 242);
    CHECK(oga::kZeldaLinkToThePast.ready(), "ready on a sane playable state");

    // DumpState logs and says nothing out loud.
    NSPOKEN = 0; NLOGGED = 0;
    oga::kZeldaLinkToThePast.command(oga::Command::DumpState);
    CHECK(NSPOKEN == 0, "dump speaks nothing");
    bool logged = false;
    for (int i = 0; i < NLOGGED; i++)
        if (strstr(LOGGED[i], "health=") && strstr(LOGGED[i], "area=")) logged = true;
    CHECK(logged, "dump logs health and area");

    // Health above capacity is not a playable reading.
    put8(CUR, 0x30);
    CHECK(!oga::kZeldaLinkToThePast.ready(), "health above capacity is refused");
    put8(CUR, 0x18);

    // Coordinates at the origin mean no game is running (title, file select).
    put16(X, 0);
    put16(Y, 0);
    CHECK(!oga::kZeldaLinkToThePast.ready(), "origin coordinates are refused");
    put16(X, 0x0800);
    put16(Y, 0x0800);

    // Capacity outside the game's own range is a wrong read, not a small Link.
    put8(CAP, 0x08);
    CHECK(!oga::kZeldaLinkToThePast.ready(), "impossible capacity is refused");
    put8(CAP, 0x18);

    // Detach is honest about being gone.
    oga::kZeldaLinkToThePast.detach();
    CHECK(!oga::kZeldaLinkToThePast.ready(), "not ready after detach");

    printf("\n%d passed, %d failed\n", pass, fail);
    if (fail == 0) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}

/* Registry stubs for the adapters this focused test does not link. Declaring
 * them (not defining them) is what the real registry needs; the real
 * implementations live in their own files. */
namespace oga {
static bool s_att(const Host*) { return false; }
static void s_frm() {}
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}

const Adapter kFireEmblemShadowDragon = {"fe11", "FE", "YFEE", s_att, s_frm, s_cmd, s_rdy, s_det};
const Adapter kGameBoyAdvance = {"gba", "GBA", "", s_att, s_frm, s_cmd, s_rdy, s_det};
}
