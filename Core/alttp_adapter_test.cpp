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
static uint8_t SRAM[0x2000];
static const uint32_t RAMBASE = 0x7E0000u;
static const uint32_t SRAMBASE = 0x700000u;
static inline size_t OFF(uint32_t a) { return (size_t) (a - RAMBASE); }
static inline bool IS_SRAM(uint32_t a) { return a >= SRAMBASE && a < SRAMBASE + sizeof(SRAM); }

static char SPOKEN[32][192]; static int NSPOKEN = 0;
static char LOGGED[32][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return IS_SRAM(a) ? SRAM[a - SRAMBASE] : RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) {
    uint16_t v;
    if (IS_SRAM(a)) memcpy(&v, &SRAM[a - SRAMBASE], 2);
    else memcpy(&v, &RAM[OFF(a)], 2);
    return v;
}
static uint32_t r32(void*, uint32_t a) {
    uint32_t v;
    if (IS_SRAM(a)) memcpy(&v, &SRAM[a - SRAMBASE], 4);
    else memcpy(&v, &RAM[OFF(a)], 4);
    return v;
}
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
static void putS8(uint32_t a, uint8_t v) { SRAM[a - SRAMBASE] = v; }
static void putS16(uint32_t a, uint16_t v) { memcpy(&SRAM[a - SRAMBASE], &v, 2); }

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
    put16(AREA, 4); put16(SCREEN, 5); put8(INDOORS, 0); put8(0x7E0010u, 9);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 4, screen 5.") == 0,
          "outdoors: area and screen");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 1); put16(ROOM, 12); put8(0x7E0010u, 7); put8(0x7E040Cu, 0xFF);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Indoors, room 12.") == 0,
          "indoors: dungeon room");

    // A dungeon id names the palace. 0 is the sewers, not "no dungeon".
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E040Cu, 0x04);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Inside Eastern Palace, room 12.") == 0,
          "a dungeon id names the palace");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E040Cu, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Inside Sewer Passage, room 12.") == 0,
          "dungeon id 0 is the sewers, not an empty room");
    put8(0x7E040Cu, 0xFF);

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

    // Status on the file select must not read the last file's leftovers.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 1);            // file select
    put8(0x7E0011u, 4);            // and its interactive submodule
    put8(CAP, 0x20); put8(CUR, 0x20); put16(X, 0x0800); put16(Y, 0x0800);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "File one, empty.") == 0,
          "status: the file select speaks its slot, not yesterday's hearts");
    put8(0x7E0010u, 7);

    // ---- module-aware where am I (the decomp's module table) ----
    // module 14 is the interface layer; saved_module_for_menu holds the real one.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 14);          // main_module_index = Interface
    put8(0x7E010Cu, 7);           // saved_module_for_menu = Dungeon (indoors)
    put8(INDOORS, 1); put16(ROOM, 12); put8(0x7E040Cu, 0xFF);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Indoors, room 12.") == 0,
          "interface layer resolves to the indoor module");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E010Cu, 9);           // Overworld
    put8(INDOORS, 0); put16(AREA, 4); put16(SCREEN, 5);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 4, screen 5.") == 0,
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
    put16(AREA, 0x01); put16(SCREEN, 0x01);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, area 1, screen 1.") == 0,
          "an unknown area speaks its number, never a guessed name");

    // The old table called 0x21 Kakariko and 0x39 the desert; the live area
    // index (and the tracker table) says 0x18 and 0x30.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x18); put16(SCREEN, 0x18);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Kakariko Village.") == 0,
          "Kakariko is at its live area id 0x18");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x30); put16(SCREEN, 0x30);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Desert of Mystery.") == 0,
          "the desert is at its live area id 0x30");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x40); put16(SCREEN, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside in the dark world, Skull Woods.") == 0,
          "the dark world is said out loud");

    // ---- area entry: walking into a new area says where you are ----
    put8(0x7E0010u, 9); put8(INDOORS, 0); put16(AREA, 0x40); put16(SCREEN, 0x00);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside in the dark world, Skull Woods.") == 0,
          "area entry: the first outdoor look says where you are");

    put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Link's House.") == 0,
          "area entry: a new area is spoken as you arrive");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 0, "area entry: standing still does not repeat it");

    // Stepping outside after any indoor time announces the area again.
    put8(INDOORS, 1); put8(0x7E040Cu, 0xFF); put16(ROOM, 260);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 0, "area entry: indoors says nothing");

    put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Outside, Link's House.") == 0,
          "area entry: stepping outside announces the area");

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

    // ---- item menu: the measured cursor and the equipment box ----
    // MEASURED 2026-10-17 with the public save: START gives module 14 submodule 1,
    // overworld_map_state 4, and hud_cur_item moves 2 -> 11 on RIGHT and back on
    // LEFT while the item bytes stay still. The cursor is hud_cur_item (0x7E0202).
    put8(0x7E0010u, 14); put8(0x7E0011u, 1); put8(0x7E010Cu, 7);
    put8(0x7E0202u, 1);            // hud_cur_item = 1, the bow's slot
    put8(0x7EF340u, 1);            // link_item_bow
    put8(0x7EF359u, 2); put8(0x7EF35Au, 1); put8(0x7EF35Bu, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Item menu. Bow, row one, column one. Master Sword, Fighter's Shield, Green Mail.") == 0,
          "item menu: opening names the item and the equipment");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF34Au, 1);            // lamp, as on the measured save
    put8(0x7E0202u, 11);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Lamp, row three, column three.") == 0,
          "item menu: a cursor move speaks the item, row and column");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF341u, 2); put8(0x7E0202u, 2);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Magical boomerang, row one, column two.") == 0,
          "item menu: a levelled item keeps its real name");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0202u, 30);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Item 30.") == 0,
          "item menu: an unnamed slot speaks its number, never a guess");

    // Where am I on the item menu must speak the menu, not the room beneath it.
    put8(0x7E0202u, 2);
    put8(INDOORS, 1); put16(ROOM, 260);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Item menu. Magical boomerang, row one, column two.") == 0,
          "where am I on the item menu names the menu, not the room");

    // ---- the bottle slot's submenu ----
    // MEASURED model: hud_cur_item stays 16 while link_item_bottle_index (1-4)
    // moves and link_bottle_info holds the contents. Names are the game's own,
    // from kHudBottlesItemText.
    put8(0x7E0202u, 16); put8(0x7E0200u, 7);
    put8(0x7EF34Fu, 1); put8(0x7EF35Cu, 3);       // bottle 1 = life
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Bottle menu. Bottle one, life.") == 0,
          "bottle submenu: opening names the selected bottle and its contents");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF34Fu, 2); put8(0x7EF35Du, 5);       // bottle 2 = magic
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Bottle two, magic.") == 0,
          "bottle submenu: moving the bottle speaks the new contents");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0200u, 4); put8(0x7E0202u, 11);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Lamp, row three, column three.") == 0,
          "bottle submenu: leaving it returns to the item cursor");

    // ---- save menu: module 14 submodule 11, a two-choice text box ----
    // MEASURED: SELECT gives submodule 11 and message 390, whose decoded text is
    // "Continue Game, Save and Quit."; choice 0 is continue, 1 is save and quit
    // (the decomp's Module0E_0B_SaveMenu saves only when the choice is non-zero).
    put8(0x7E0010u, 14); put8(0x7E0011u, 11); put8(0x7E1CE8u, 0);
    put16(0x7E1CF0u, 390);
    static const unsigned char kSaveMenu[] = {
        0x7a,0x00,0x44,0x59,0x02,0x28,0x27,0x2d,0x22,0x27,0x2e,0x1e,0x59,0x06,0x1a,0x26,
        0x1e,0x75,0x59,0x59,0x59,0x12,0x1a,0x2f,0x1e,0x59,0x1a,0x27,0x1d,0x59,0x10,0x2e,
        0x22,0x2d,0x72,0x7f
    };
    memcpy(&RAM[OFF(0x7F1200u)], kSaveMenu, sizeof(kSaveMenu));
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Save menu. Continue game, or save and quit. Continue game selected.") == 0,
          "save menu: opening reads the options and speaks the box text once, not twice");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E1CE8u, 1); put16(0x7E1CF0u, 10);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Save and quit selected.") == 0,
          "save menu: moving the choice speaks the new option");

    // Closing the box must not replay the cursor glyphs (message 9) as prose.
    put8(0x7E0010u, 7); put8(0x7E0011u, 0);
    static const unsigned char kCursorGlyphs[] = {
        0x7a,0x00,0x74,0x44,0x75,0x59,0x59,0x72,0x7f
    };
    memcpy(&RAM[OFF(0x7F1200u)], kCursorGlyphs, sizeof(kCursorGlyphs));
    put16(0x7E1CF0u, 9);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 0, "closing the save menu does not speak the cursor glyphs");

    // ---- file select: slots live in the cartridge's save RAM ----
    // The fade-in (submodule 1) is not the screen: 0x7E00C8 holds garbage then.
    put8(0x7E0010u, 1); put8(0x7E0011u, 1); put8(0x7E00C8u, 216);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 0, "file select: the fade-in speaks nothing, even on a garbage cursor");

    // MEASURED 2026-10-17: module 1, cursor selectfile_R16 (0x7E00C8) walking
    // 0 -> 1 -> 2 with DOWN; slot 0 exists (0x55AA at +0x3E5) with the name
    // glyphs 0x0B, 0xC0, 0x47, 0x44 = "Link", capacity 32, sword 1, shield 1.
    put8(0x7E0010u, 1); put8(0x7E0011u, 4); put8(0x7E00C8u, 0);
    putS16(0x700000u + 0x3E5u, 0x55AA);
    putS16(0x700000u + 0x3D9u, 0x000B);
    putS16(0x700000u + 0x3DBu, 0x00C0);
    putS16(0x700000u + 0x3DDu, 0x0047);
    putS16(0x700000u + 0x3DFu, 0x0044);
    putS16(0x700000u + 0x3E1u, 0xA9);
    putS16(0x700000u + 0x3E3u, 0xA9);
    putS8(0x700000u + 0x36Cu, 32);
    putS8(0x700000u + 0x359u, 2);
    putS8(0x700000u + 0x35Au, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "File select. File one, Link. Four hearts. Master Sword, Fighter's Shield.") == 0,
          "file select: the used slot names its file, hearts and equipment");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E00C8u, 1);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "File two, empty.") == 0,
          "file select: an empty slot says so");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E00C8u, 3);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Copy file.") == 0,
          "file select: the copy position is named");

    // An unreadable name is dropped, never guessed.
    putS16(0x700A00u + 0x3E5u, 0x55AA);
    putS16(0x700A00u + 0x3D9u, 0x00FF);
    putS8(0x700A00u + 0x36Cu, 24);
    putS8(0x700A00u + 0x359u, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E00C8u, 2);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "File three. Three hearts. Fighter's Sword.") == 0,
          "file select: an unreadable name is left out, not invented");

    // Status on the file select reads the slot, not stale gameplay values.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E00C8u, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "File one, Link. Four hearts. Master Sword, Fighter's Shield.") == 0,
          "status on the file select speaks the slot");

    // Guidance: a bearing to the next named landmark, never a walkable route.
    // Coordinates are the public save's own, standing at Link's House (0x2C).
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);
    put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    put16(X, 2232); put16(Y, 2792);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Link's House. You are here.") == 0,
          "guide: the first landmark is the place you are standing in");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Sanctuary, north, about three screens away.") == 0,
          "guide: the Sanctuary is north and three screens away");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Hyrule Castle, north-west, about two screens away.") == 0,
          "guide: the castle is north-west and two screens away");

    // The list wraps: five more presses bring it back to the first landmark.
    for (int i = 0; i < 5; i++) oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Link's House. You are here.") == 0,
          "guide: the landmark list wraps");

    // Indoors, in a menu, and in the dark world the answer is a refusal.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 1); put8(0x7E0010u, 7); put8(0x7E0011u, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guidance works outside.") == 0,
          "guide refuses indoors");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 0); put8(0x7E0010u, 14); put8(0x7E0011u, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Close the menu first.") == 0,
          "guide refuses while a menu owns the frame");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0); put16(AREA, 0x43);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guidance covers the light world only.") == 0,
          "guide refuses in the dark world rather than guess a name");

    // Objective: the game's own progress bytes pick the story stage. The save
    // measured flags 0x15 (sword given, Zelda at the Sanctuary, uncle gone),
    // no pendants, Fighter's Sword.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);
    put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    put16(X, 2232); put16(Y, 2792);
    put8(0x7EF3C6u, 0x15); put8(0x7EF374u, 0); put8(0x7EF359u, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Collect the three pendants. Sahasrahla in Kakariko knows where to start. Kakariko Village is north-west, about five screens away.") == 0,
          "objective: after the Sanctuary, the pendants are the goal");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Find your uncle in Hyrule Castle. Hyrule Castle is north-west, about two screens away.") == 0,
          "objective: before the sword, find the uncle");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x11);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Rescue Zelda in Hyrule Castle. Hyrule Castle is north-west, about two screens away.") == 0,
          "objective: with the sword, Zelda is the goal");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x15); put8(0x7EF374u, 7);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Claim the Master Sword in the Lost Woods. Lost Woods is north-west, about seven screens away.") == 0,
          "objective: three pendants, the Master Sword is the goal");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF374u, 0); put8(INDOORS, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Collect the three pendants. Sahasrahla in Kakariko knows where to start.") == 0,
          "objective: indoors names the goal without a bogus bearing");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 0); put8(0x7E0010u, 14); put8(0x7E0011u, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Close the menu first.") == 0,
          "objective refuses while a menu owns the frame");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0); put16(AREA, 0x43);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: nothing mapped for the dark world yet.") == 0,
          "objective refuses in the dark world rather than guess the goal");

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
