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
#include "alttp_text.h"

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

    // ⛔ THE MEASURED BOOT GARBAGE (frame 0): capacity 0x7F, health 0x56, non-zero
    // coordinates. A naive range check accepts it; the game's heart-container
    // step (8) does not.
    put8(CAP, 0x7F);
    put8(CUR, 0x56);
    CHECK(!oga::kZeldaLinkToThePast.ready(), "boot garbage (capacity 0x7F) is refused");
    put8(CAP, 0x18);
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

    // Where am I: outdoors speaks area and screen, indoors speaks the room.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 3); put16(SCREEN, 4); put8(INDOORS, 0); put8(0x7E0010u, 9);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 3, screen 4.") == 0,
          "outdoors: area and screen");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 1); put16(ROOM, 12); put8(0x7E0010u, 7);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Indoors, room 12.") == 0,
          "indoors: dungeon room");

    // Status: hearts, magic and rupees, in the player's units.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 0);
    put8(CAP, 0x18); put8(CUR, 0x14); put8(MAGIC, 0x40); put16(RUPEES, 242);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Two and a half hearts. Magic half. Two hundred forty-two rupees.") == 0,
          "status: hearts, magic and rupees");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(CUR, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Zero hearts.") == 0,
          "status: zero hearts is a real answer, not silence");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(CUR, 0x18); put8(MAGIC, 0); put16(RUPEES, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Three hearts. One rupee.") == 0,
          "status: no magic meter yet, one rupee");

    // ---- module-aware where am I (the decomp's module table) ----
    // module 14 is the interface layer; saved_module_for_menu holds the real one.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 14);          // main_module_index = Interface
    put8(0x7E010Cu, 7);           // saved_module_for_menu = Dungeon (indoors)
    put8(INDOORS, 1); put16(ROOM, 12);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Indoors, room 12.") == 0,
          "interface layer resolves to the indoor module");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E010Cu, 9);           // Overworld
    put8(INDOORS, 0); put16(AREA, 3); put16(SCREEN, 4);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 3, screen 4.") == 0,
          "interface layer resolves to the overworld module");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E010Cu, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "File select.") == 0,
          "the file select is named");

    // ---- names: measured and sourced, numbers for the rest ----
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Link's House.") == 0,
          "the measured save's own area is named");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x1B); put16(SCREEN, 0x1B);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Hyrule Castle.") == 0,
          "a sourced area is named");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x05); put16(SCREEN, 0x05);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 5, screen 5.") == 0,
          "an unknown area speaks its number, never a guessed name");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x42); put16(SCREEN, 0x02);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside in the dark world, Death Mountain.") == 0,
          "the dark world is said out loud");

    // ---- dialogue: the measured message 388 ----
    static const unsigned char kMsg[] = {
        0x7a,0x00,0x44,0x12,0x2d,0x1a,0x2b,0x2d,0x59,0x05,0x2b,0x28,0x26,0x59,
        0x0b,0x22,0x27,0x24,0x51,0x2c,0x59,0x07,0x28,0x2e,0x2c,0x1e,0x75,0x59,
        0x59,0x12,0x2d,0x1a,0x2b,0x2d,0x59,0x05,0x2b,0x28,0x26,0x59,0x12,0x1a,
        0x27,0x1c,0x2d,0x2e,0x1a,0x2b,0x32,0x72,0x7f
    };
    char decoded[192];
    int dn = oga::AlttpDecodeText(kMsg, (int) sizeof(kMsg), decoded, (int) sizeof(decoded));
    CHECK(dn > 0 && strcmp(decoded, "Start From Link's House, Start From Sanctuary.") == 0,
          "the measured message decodes");

    // The adapter speaks a new message once, not once per frame.
    memcpy(&RAM[OFF(0x7F1200u)], kMsg, sizeof(kMsg));
    put16(0x7E1CF0u, 388);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], decoded) == 0,
          "a new message speaks once and is not repeated per frame");

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
