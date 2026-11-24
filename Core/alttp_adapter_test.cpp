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
#include "alttp_graph.h"
#include "alttp_walk.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t RAM[0x20000];
static uint8_t SRAM[0x2000];
static const uint32_t RAMBASE = 0x7E0000u;
static const uint32_t SRAMBASE = 0x700000u;
static inline size_t OFF(uint32_t a) { return (size_t) (a - RAMBASE); }
static inline bool IS_SRAM(uint32_t a) { return a >= SRAMBASE && a < SRAMBASE + sizeof(SRAM); }

static char SPOKEN[32][384]; static int NSPOKEN = 0;
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
static void spk(void*, const char* s, bool) { if (NSPOKEN < 32) snprintf(SPOKEN[NSPOKEN++], 384, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 32) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

/* Auto-walk stubs: a recording set_button (walk code must never call
 * it), a recording set_direction (the walker's only output), a player
 * feed driven by hand, and a combined event log for ordering checks. */
static bool DIRDOWN[4] = { false, false, false, false };
static int DIRPRESS = 0;
static int BTNCALLS = 0;
static uint32_t FEEDHELD = 0, FEEDPRESSED = 0;
static char EVENTS[512][96]; static int NEVENTS = 0;
static void btnrec(void*, int, bool) { BTNCALLS++; }
static void dirset(void*, int dir, bool down) {
    if (dir < 0 || dir > 3) return;
    DIRDOWN[dir] = down;
    if (down) {
        DIRPRESS++;
        if (NEVENTS < 512) snprintf(EVENTS[NEVENTS++], 96, "D %d", dir);
    }
}
static void padfeed(void*, uint32_t* held, uint32_t* pressed) {
    *held = FEEDHELD; *pressed = FEEDPRESSED; FEEDPRESSED = 0;
}
static void spk2(void* c, const char* s, bool i) {
    spk(c, s, i);
    if (NEVENTS < 512) snprintf(EVENTS[NEVENTS++], 96, "S %s", s);
}

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

    // Room notes: derived facts from the room database, only inside a dungeon.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 0); put8(0x7E040Cu, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Inside Sewer Passage, room 0. Kill to open Ganon's door.") == 0,
          "room note: a puzzle note is spoken with the room");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 170); put8(0x7E040Cu, 0x04);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Inside Eastern Palace, room 170. One chest. Switch opens door(Toggle).") == 0,
          "room note: a chest and its puzzle");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 36);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Inside Eastern Palace, room 36. One big chest. Kill enemy to open.") == 0,
          "room note: a big chest is named as one");

    // A house shares the room index space but belongs to no dungeon.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 260); put8(0x7E040Cu, 0xFF);
    oga::kZeldaLinkToThePast.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Indoors, room 260.") == 0,
          "room note: a house does not borrow a dungeon room's chest");
    put16(ROOM, 12);

    // Status: hearts, magic and rupees, in the player's units.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 0);
    put8(CAP, 0x18); put8(CUR, 0x14); put8(MAGIC, 0x40); put16(RUPEES, 242);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Two and a half hearts. Magic half. Two hundred forty-two rupees.") == 0,
          "status: hearts, magic and rupees");

    // Inside a dungeon the pouch joins the answer, keyed on the dungeon id.
    // 0x04 is Eastern Palace (id 2); the three flags are one bit per dungeon.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 1); put8(0x7E040Cu, 0x04);
    put8(0x7EF36Fu, 3);
    put16(0x7EF366u, 1u << 2);
    put16(0x7EF368u, 1u << 2);
    put16(0x7EF364u, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Two and a half hearts. Magic half. Two hundred forty-two rupees. Three keys, big key, map, no compass.") == 0,
          "status in a dungeon adds keys, big key, map and compass");

    // A house is not a dungeon: 0xFF must add nothing.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E040Cu, 0xFF);
    oga::kZeldaLinkToThePast.command(oga::Command::Status);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Two and a half hearts. Magic half. Two hundred forty-two rupees.") == 0,
          "status in a house says nothing about keys");
    put8(INDOORS, 0);

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

    // Word breaks the table does not name still separate words. MEASURED on the
    // intro message: unknown tiles between "Help me" and "Please" decoded the
    // two words together before this rule.
    static const unsigned char kBreaks[] = {
        0x07,0x1e,0x25,0x29,0x59,0x26,0x1e, 0x43, 0x0f,0x25,0x1e,0x1a,0x2c,0x1e, 0x7f
    };
    char joined[64];
    oga::AlttpDecodeText(kBreaks, (int) sizeof(kBreaks), joined, (int) sizeof(joined));
    CHECK(strcmp(joined, "Help me Please") == 0,
          "an unnamed separator tile becomes a word break");

    // A page advance keeps the message index. The content hash is what speaks,
    // so the second page is not swallowed by the index check.
    static const unsigned char kPage2[] = {
        0x19,0x1e,0x25,0x1d,0x1a,0x59,0x30,0x1a,0x22,0x2d,0x2c,0x72,0x7f
    };
    memcpy(&RAM[OFF(0x7F1200u)], kPage2, sizeof(kPage2));
    put16(0x7E1CF0u, 388);          // same message, new page
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.on_frame();
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Zelda waits.") == 0,
          "the next page of the same message is spoken");

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

    // Guidance: a spoken walking route when the generated graph proves one.
    // The synthetic tile-attribute RAM is all tile 0 (walkable), so the house
    // screen flood-fills to its single table region and routes are spoken.
    // Coordinates are the public save's own, standing at Link's House (0x2C).
    // The save's progress flags are 0x15 and it has no pendants, so the first
    // press must aim at Kakariko, the objective, not at Link's House.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);
    put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
    put16(X, 2232); put16(Y, 2792);
    put8(0x7EF3C6u, 0x15); put8(0x7EF374u, 0); put8(0x7EF359u, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Kakariko Village. Route: west one screen, south one screen, west one screen, north one screen, west one screen, then north one screen into Kakariko Village.") == 0,
          "guide: the first press aims at the current objective");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Eastern Palace. Route: east two screens from near the south end, north one screen, then east one screen into Eastern Palace.") == 0,
          "guide: the next press walks the landmark list");

    // A new story stage re-aims the cycle at the new objective.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x11);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Hyrule Castle. Route: north one screen into Hyrule Castle.") == 0,
          "guide: a new objective re-aims the cycle");

    // Standing in the objective's own area is said plainly. A new stage re-aims
    // the cycle, so set the Master Sword stage (Lost Woods) and stand there.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x15); put8(0x7EF374u, 7); put8(0x7EF359u, 1);
    put16(AREA, 0x00); put16(SCREEN, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Lost Woods. You are here.") == 0,
          "guide: standing at the objective says you are there");
    put16(AREA, 0x2C); put16(SCREEN, 0x2C); put8(0x7EF374u, 0);

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

    // The dark world has its own landmark cycle. At the Pyramid with a
    // light-world story stage, the cycle starts at the Pyramid itself; the
    // next press cannot prove a route (the synthetic fill sees one region,
    // the Pyramid's table has eight, so no region can be trusted and the
    // regions disagree), so it falls back to a bearing said as a fallback.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);
    put16(AREA, 0x5B); put16(SCREEN, 0x5B); put16(X, 1792); put16(Y, 1792);
    put8(0x7EF3C5u, 0); put8(0x7EF37Au, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Pyramid of Power. You are here.") == 0,
          "guide: the dark cycle starts at the Pyramid");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Palace of Darkness. No walking route known; it lies east, about three screens away.") == 0,
          "guide: an unprovable dark route falls back to a bearing");

    // On a single-region dark screen the live fill is trustworthy, and the
    // first press aims at the nearest remaining crystal dungeon: the Palace
    // of Darkness is one screen north of screen 0x66.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(AREA, 0x65); put16(SCREEN, 0x66); put16(X, 3328); put16(Y, 2304);
    put8(0x7EF3C5u, 3); put8(0x7EF37Au, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Palace of Darkness. Route: north one screen into Palace of Darkness.") == 0,
          "guide: a proven dark route is spoken as a route");
    put8(0x7EF3C5u, 0); put8(0x7EF37Au, 0);
    put16(AREA, 0x2C); put16(SCREEN, 0x2C); put16(X, 2232); put16(Y, 2792);

    // Indoors guidance: the Eastern Palace room graph. Room 201 is the
    // entrance hall, the big chest is room 169, the boss is room 200. The
    // expected routes were cross-checked against the generated table with
    // an independent router. No small keys, no big key.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 7); put8(0x7E0011u, 0);
    put8(INDOORS, 1); put16(ROOM, 201); put8(0x7E040Cu, 4);
    put8(0x7EF36Fu, 0); put16(0x7EF366u, 0);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Big Chest. Route: north through the shutter door, then north through the door into the big chest room. One room on the way is gated: a switch opens the door.") == 0,
          "guide indoors: the first press routes to the big chest");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Boss. Route: north through the shutter door, north through the door, north through the big key door, take the stairs, west through two shutter doors, then north through the shutter door into the boss room. You do not have the big key. One room on the way is gated: a switch opens the door.") == 0,
          "guide indoors: the boss route names the big key door and the shortfall");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Entrance. You are here.") == 0,
          "guide indoors: standing in the entrance room says so");

    // From the boss room the cycle restarts at the big chest; that route
    // spends a small key the player does not have, and says so.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 200);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Big Chest. Route: south through the shutter door, east through the door, east through the shutter door, take the stairs, then south through the locked door into the big chest room. You have no small key for the locked door. One room on the way is gated: clear it to open the door.") == 0,
          "guide indoors: a locked door with no key is named with its cost");

    // The Ice Palace boss has no door at all: room 222 is reached
    // only by the pit fall from room 206, whose pick-up block hides
    // the game's only hole secret - and the fall is live because that
    // room's pits drop the player (where pits only hurt, the
    // destination byte is never read and proves nothing). Expected
    // routes cross-checked with an independent router over the
    // emitted table. Room 14 is the entrance hall, 158 the big chest.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 14); put8(0x7E040Cu, 0x12);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Big Chest. Route: take the stairs, east through the shutter door, take the stairs, take the stairs, take the stairs, west through the shutter door, then drop down the pit into the big chest room. One room on the way is gated: a switch opens the door.") == 0,
          "guide indoors: the Ice Palace big chest route drops through a pit");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Boss. Route: take the stairs, east through the shutter door, take the stairs, take the stairs, take the stairs, west through the shutter door, drop down the pit, take the stairs, south through the shutter door, then drop down the pit into the boss room. One room on the way is gated: a switch opens the door.") == 0,
          "guide indoors: the Ice Palace boss route ends with the fall from room 206");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guide: Entrance. You are here.") == 0,
          "guide indoors: the Ice Palace entrance room says so");

    // Standing over the hidden hole itself, the boss is one fall away.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 206);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Big Chest. Route: north through the shutter door, then take the stairs into the big chest room. One room on the way is gated: a switch opens the door.") == 0,
          "guide indoors: from room 206 the cycle restarts at the big chest");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Boss. Route: drop down the pit into the boss room. One room on the way is gated: clear it to open the door.") == 0,
          "guide indoors: room 206 falls straight into the boss room");

    // The same fall rule connects Mothula: Skull Woods' boss room 41
    // has no paired door either, and the pit in room 57 drops into
    // it. From the Boss Lair entrance (room 89) the first press skips
    // the big chest this part of the dungeon does not hold.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put16(ROOM, 89); put8(0x7E040Cu, 0x10);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Guide: Boss. Route: north through the locked door, north through the door, then drop down the pit into the boss room. You have no small key for the locked door. One room on the way is gated: clear it to open the door.") == 0,
          "guide indoors: the Mothula route falls through the pit in room 57");

    // A house is indoors but no dungeon: the refusal stands.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E040Cu, 0xFF);
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Guidance works outside.") == 0,
          "guide indoors: a house is not a dungeon");
    put8(INDOORS, 0);

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
          "Objective: Collect the three pendants. Sahasrahla in Kakariko knows where to start. Route: west one screen, south one screen, west one screen, north one screen, west one screen, then north one screen into Kakariko Village.") == 0,
          "objective: after the Sanctuary, the pendants are the goal");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x00);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Find your uncle in Hyrule Castle. Route: north one screen into Hyrule Castle.") == 0,
          "objective: before the sword, find the uncle");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x11);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Rescue Zelda in Hyrule Castle. Route: north one screen into Hyrule Castle.") == 0,
          "objective: with the sword, Zelda is the goal");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7EF3C6u, 0x15); put8(0x7EF374u, 7);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Claim the Master Sword in the Lost Woods. Route: north one screen, west one screen, north one screen, west one screen, north one screen, west one screen, then north one screen into Lost Woods.") == 0,
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

    // The dark-world stage: progress indicator 3 (Agahnim down) and three
    // crystal bits set (Palace of Darkness, Swamp Palace, Skull Woods). At
    // the Pyramid no region can be trusted, so the nearest remaining dungeon
    // is the first in gating order and its direction is a bearing.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);
    put16(AREA, 0x5B); put16(SCREEN, 0x5B); put16(X, 1792); put16(Y, 1792);
    put8(0x7EF3C5u, 3); put8(0x7EF37Au, 0x52);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Rescue the seven maidens. Three of seven crystals held. Remaining: Thieves' Town, Ice Palace, Misery Mire, Turtle Rock. Nearest remaining: Thieves' Town. No walking route known; it lies west, about three screens away.") == 0,
          "objective: the maidens stage names held, remaining and nearest");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 1);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Rescue the seven maidens. Three of seven crystals held. Remaining: Thieves' Town, Ice Palace, Misery Mire, Turtle Rock.") == 0,
          "objective: indoors names the maidens stage without a direction");

    // All seven crystals: the goal is Ganon's Tower, named from either world.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(INDOORS, 0); put8(0x7EF37Au, 0x7F);
    put16(AREA, 0x2C); put16(SCREEN, 0x2C); put16(X, 2232); put16(Y, 2792);
    oga::kZeldaLinkToThePast.command(oga::Command::Objective);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0],
          "Objective: Climb Ganon's Tower and face Ganon. No walking route known; it lies north, about five screens away.") == 0,
          "objective: seven crystals point at Ganon's Tower");
    put8(0x7EF3C5u, 0); put8(0x7EF37Au, 0);


    // ---- auto walk (docs/design/auto-walk.md) -------------------------------
    // A second host WITH the walk verbs, recording everything: the stub
    // set_button must never be called by walk code (the structural
    // guarantee), set_direction writes are the walker's only output, and
    // the player feed is driven by hand. The sim moves Link 2 px/frame
    // in whatever direction the stub currently holds and re-derives the
    // screen/area from his world position, the way the game does.
    oga::Host H2 = {};
    H2.read8 = r8; H2.read16 = r16; H2.read32 = r32;
    H2.speak = spk2; H2.log = lg; H2.set_button = btnrec;
    H2.set_direction = dirset; H2.player_pad = padfeed; H2.ctx = nullptr;

    // A ground tile both masks accept, and a wall tile neither does,
    // found in the shipped tables rather than hardcoded.
    uint16_t groundTile = 0, wallTile = 0;
    for (uint32_t m = 0; m < 3752; m++) {
        if (!groundTile && oga::AlttpMap16Walkable((uint16_t) m) &&
            oga::AlttpMap16Steerable((uint16_t) m)) groundTile = (uint16_t) m;
        if (!wallTile && !oga::AlttpMap16Walkable((uint16_t) m)) wallTile = (uint16_t) m;
    }
    CHECK(groundTile != 0 && wallTile != 0, "walk: ground and wall tiles exist in the tables");
    auto fillTiles = [&](bool island) {
        for (int ry = 0; ry < 64; ry++)
            for (int rx = 0; rx < 64; rx++) {
                uint16_t t = groundTile;
                if (island && rx >= 26 && rx <= 30 && ry >= 26 && ry <= 30 &&
                    !(rx >= 27 && rx <= 29 && ry >= 27 && ry <= 29))
                    t = wallTile;   // a walled 3x3 island: a second region
                put16(0x7E2000u + (uint32_t) (ry * 64 + rx) * 2, t);
            }
        put16(0x7E0708u, 0); put16(0x7E070Au, 0);
        put16(0x7E070Cu, 0); put16(0x7E070Eu, 0);
    };
    auto placeLink = [&](int x, int y) {
        put16(X, (uint16_t) x); put16(Y, (uint16_t) y);
        int scr = ((y >> 9) & 7) * 8 + ((x >> 9) & 7);
        put16(SCREEN, (uint16_t) scr);
        put16(AREA, oga::AlttpAreaOfScreen((uint8_t) scr));
    };
    auto simFrames = [&](int n, bool move) {
        for (int i = 0; i < n; i++) {
            oga::kZeldaLinkToThePast.on_frame();
            if (!move) continue;
            int dx = 0, dy = 0;
            if (DIRDOWN[0]) dy -= 2;
            if (DIRDOWN[1]) dy += 2;
            if (DIRDOWN[2]) dx -= 2;
            if (DIRDOWN[3]) dx += 2;
            if (dx || dy) {
                int nx = (int) r16(nullptr, X) + dx;
                int ny = (int) r16(nullptr, Y) + dy;
                if (nx < 0) nx = 0; if (ny < 0) ny = 0;
                if (nx > 4095) nx = 4095; if (ny > 4095) ny = 4095;
                placeLink(nx, ny);
            }
        }
    };
    auto said = [&](const char* sub) {
        for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], sub)) return true;
        return false;
    };
    auto saidExact = [&](const char* line) {
        for (int i = 0; i < NSPOKEN; i++) if (strcmp(SPOKEN[i], line) == 0) return true;
        return false;
    };
    auto dirsClear = [&]() {
        return !DIRDOWN[0] && !DIRDOWN[1] && !DIRDOWN[2] && !DIRDOWN[3];
    };
    auto walkSetup = [&](int x, int y, uint8_t flags) {
        oga::kZeldaLinkToThePast.detach();
        for (int d = 0; d < 4; d++) DIRDOWN[d] = false;
        DIRPRESS = 0; BTNCALLS = 0; FEEDPRESSED = 0; FEEDHELD = 0;
        oga::kZeldaLinkToThePast.attach(&H2);
        oga::AlttpAutoWalk(true);
        fillTiles(false);
        put8(CAP, 0x18); put8(CUR, 0x18);
        put8(0x7E0010u, 9); put8(0x7E0011u, 0); put16(0x7E010Cu, 9);
        put8(INDOORS, 0); put16(0x7E1CF0u, 388); put8(0x7E040Cu, 0xFF);
        put8(0x7EF3C6u, flags); put8(0x7EF374u, 0); put8(0x7EF359u, 1);
        placeLink(x, y);
        NSPOKEN = 0; NEVENTS = 0; oga::AdapterSpeechReset();
    };
    /* The walk-stop earcon: one cue snapshot carrying walk_stop, then
     * nothing — the one-shot is consumed as the snapshot is taken. */
    auto takeStopCue = [&]() {
        oga::CueSnapshot c{};
        if (!oga::kZeldaLinkToThePast.cue_snapshot) return false;
        bool ok = oga::kZeldaLinkToThePast.cue_snapshot(&c);
        return ok && c.walk_stop && !c.battle && !c.locked && !c.field &&
               !c.foe_beep && !c.own_beep && !c.guard_active;
    };
    auto guideTo = [&](const char* name) {
        char want[128];
        snprintf(want, sizeof(want), "Guide: %s.", name);
        for (int i = 0; i < 12; i++) {
            NSPOKEN = 0; oga::AdapterSpeechReset();
            oga::kZeldaLinkToThePast.command(oga::Command::Guide);
            if (NSPOKEN >= 1 && strstr(SPOKEN[0], want) == SPOKEN[0]) return true;
        }
        return false;
    };

    // Proof 5: a host with no direction verb gets a spoken refusal,
    // never a silent dead button.
    {
        oga::kZeldaLinkToThePast.detach();
        oga::kZeldaLinkToThePast.attach(&H);
        oga::AlttpAutoWalk(true);
        put8(CAP, 0x18); put8(CUR, 0x18);
        put8(0x7E0010u, 9); put8(0x7E0011u, 0);
        put8(INDOORS, 0); put16(AREA, 0x2C); put16(SCREEN, 0x2C);
        put16(X, 2232); put16(Y, 2792); put8(0x7EF3C6u, 0x15);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        oga::kZeldaLinkToThePast.command(oga::Command::Guide);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
        CHECK(saidExact("Auto walk is unavailable."),
              "walk: a host without the verbs gets a spoken refusal");
        oga::kZeldaLinkToThePast.detach();
    }

    // Setting off: the one-time explanation, then the short form.
    walkSetup(2232, 2792, 0x15);
    oga::AlttpAutoWalk(false);
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(said("Auto walk is off.") && said("only ever holds a direction"),
          "walk: setting off answers with the one-time explanation");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk is off. Turn it on in this game's settings."),
          "walk: the explanation is given once, then the short form");
    CHECK(DIRPRESS == 0, "walk: setting off never touches a direction");

    // No route named yet: the honest answer.
    walkSetup(2232, 2792, 0x15);
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk: ask for a route first."),
          "walk: no Guide route yet is answered honestly");
    CHECK(DIRPRESS == 0, "walk: no route, no movement");
    CHECK(!takeStopCue(), "walk: a refusal sounds no stop earcon");

    // The full walk: Link's house north one screen into Hyrule Castle.
    // The start line is spoken before the first direction is written.
    walkSetup(2232, 2792, 0x11);
    CHECK(guideTo("Hyrule Castle"), "walk: guide names the castle");
    NSPOKEN = 0; NEVENTS = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk: Hyrule Castle."), "walk: the start line names the target");
    {
        int sIdx = -1, dIdx = -1;
        for (int i = 0; i < NEVENTS; i++) {
            if (sIdx < 0 && strstr(EVENTS[i], "S Auto walk: Hyrule Castle.") == EVENTS[i]) sIdx = i;
            if (dIdx < 0 && EVENTS[i][0] == 'D') dIdx = i;
        }
        simFrames(1, true);
        for (int i = 0; i < NEVENTS; i++)
            if (dIdx < 0 && EVENTS[i][0] == 'D') dIdx = i;
        CHECK(sIdx >= 0 && dIdx > sIdx,
              "walk: the target is spoken before the first direction is written");
        CHECK(!takeStopCue(), "walk: a walk under way sounds no stop earcon");
    }
    {
        int frames = 0;
        while (r16(nullptr, AREA) != 0x1B && frames < 4000) { simFrames(20, true); frames += 20; }
        CHECK(r16(nullptr, AREA) == 0x1B, "walk: the walker steered Link into Hyrule Castle");
        CHECK(said("Outside, Hyrule Castle."), "walk: arrival speaks the Where Am I line");
        simFrames(5, true);
        CHECK(dirsClear(), "walk: arrival releases every direction");
        CHECK(BTNCALLS == 0, "walk: set_button was never called (the structural guarantee)");
        CHECK(takeStopCue(), "walk: arrival raises the stop earcon");
        CHECK(!takeStopCue(), "walk: the stop earcon is delivered exactly once");
    }

    // Progress is a Guide press during the walk, in route units.
    walkSetup(2232, 2792, 0x15);
    CHECK(guideTo("Desert of Mystery"), "walk: guide names the desert");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(200, true);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::Guide);
    CHECK(said("screens to go.") && !said("Guide:"),
          "walk: Guide during a walk reports progress in screens");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk off."), "walk: WalkToggle during a walk stops it");
    CHECK(dirsClear(), "walk: the stop releases every direction");
    CHECK(takeStopCue(), "walk: WalkToggle off raises the stop earcon");

    // The player's own press cancels, silently, within a frame.
    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(150, true);
    {
        int px = (int) r16(nullptr, X), py = (int) r16(nullptr, Y);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        FEEDPRESSED = 1u << 4;   // POKE_BTN_RIGHT
        oga::kZeldaLinkToThePast.on_frame();
        CHECK(dirsClear(), "walk: a player press releases the directions at once");
        CHECK(!said("Auto walk"), "walk: a player cancel is silent");
        CHECK(takeStopCue(), "walk: a silent player cancel still sounds the stop earcon");
        simFrames(120, true);
        CHECK((int) r16(nullptr, X) == px && (int) r16(nullptr, Y) == py,
              "walk: after a player cancel the walker stays stopped");
    }

    // Dialogue cancels silently; a menu cancels silently; harm speaks.
    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(60, true);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    /* A box really open: interface module running RenderText, the
     * saved module still the overworld (measured on the intro message). */
    put8(0x7E0010u, 14); put8(0x7E0011u, 2); put8(0x7E010Cu, 9);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(dirsClear() && !said("Auto walk"), "walk: dialogue cancels silently");
    CHECK(takeStopCue(), "walk: a dialogue cancel sounds the stop earcon");
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);

    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(60, true);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(0x7E0010u, 14); put8(0x7E0011u, 1);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(dirsClear() && !said("Auto walk stopped"), "walk: a menu cancels silently");
    put8(0x7E0010u, 9); put8(0x7E0011u, 0);

    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(60, true);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put8(CUR, 0x10);
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(saidExact("Auto walk stopped: hurt."), "walk: a health decrease stops the walk, said once");
    CHECK(dirsClear(), "walk: harm releases every direction");
    CHECK(takeStopCue(), "walk: a hurt stop sounds the stop earcon");

    // The gates that refuse to start.
    walkSetup(2232, 2792, 0x15);
    put8(CUR, 8);
    guideTo("Kakariko Village");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk: too hurt to start."), "walk: one heart refuses to start");
    CHECK(DIRPRESS == 0, "walk: the health gate never moves");

    walkSetup(2232, 2792, 0x15);
    guideTo("Lost Woods");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk does not walk the Lost Woods."),
          "walk: the Lost Woods are refused by name");
    CHECK(DIRPRESS == 0, "walk: the denylist never moves");

    walkSetup(2232, 2792, 0x15);
    put8(INDOORS, 1); put8(0x7E0010u, 7);
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk works outside."), "walk: indoors is answered honestly");

    walkSetup(2232, 2792, 0x15);
    guideTo("Link's House");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(NSPOKEN >= 1 && !said("Auto walk:") && DIRPRESS == 0,
          "walk: already at the target answers with Where Am I, no walk");

    // Stuck: frozen in place, one re-plan, then the blocked line.
    // (The stall cycle now includes up to three tile exclusions
    // before each re-plan, so the whole sequence takes longer.)
    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(1600, false);
    CHECK(said("Re-routing."), "walk: a stall earns one re-plan");
    CHECK(saidExact("Auto walk stopped: blocked."), "walk: a second stall on the leg cancels");
    CHECK(dirsClear(), "walk: blocked releases every direction");

    // An unscheduled screen change is a route failure.
    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(60, true);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    placeLink(2148, 3684);   // screen 60, nowhere on the route
    // The screen register bounces at real seams (measured live), so a
    // change must hold for several frames before it is believed; an
    // unscheduled one that holds is a route failure.
    for (int i = 0; i < 12; i++) oga::kZeldaLinkToThePast.on_frame();
    CHECK(saidExact("Auto walk stopped: blocked."),
          "walk: an unscheduled transition cancels as route failure");

    // The costly leg: from Kakariko's east screen the castle route opens
    // with a ledge drop. The walker stops BEFORE it, moves nothing, and
    // a fresh press walks the leg.
    fillTiles(true);   // screen 25's table has two regions; so does this grid
    walkSetup(768, 1792, 0x11);
    fillTiles(true);   // (walkSetup refilled plain; the island must be back)
    CHECK(r16(nullptr, SCREEN) == 25, "walk: the ledge test stands on screen 25");
    CHECK(guideTo("Hyrule Castle"), "walk: guide names the castle from Kakariko");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    CHECK(saidExact("Auto walk: Hyrule Castle."), "walk: the costly walk still names its target first");
    CHECK(saidExact("Ledge ahead. Press auto walk again to hop down."),
          "walk: the walker stops before the ledge and says what it does");
    simFrames(60, true);
    CHECK(DIRPRESS == 0 && (int) r16(nullptr, X) == 768,
          "walk: nothing moves until the player confirms the ledge");
    CHECK(!takeStopCue(), "walk: a stop before a first costly leg, never having moved, sounds nothing");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(150, true);
    CHECK(DIRPRESS > 0 && (int) r16(nullptr, X) > 768,
          "walk: the confirming press walks the ledge leg");
    FEEDPRESSED = 1u << 4;
    oga::kZeldaLinkToThePast.on_frame();
    CHECK(dirsClear(), "walk: the ledge walk still yields to the player");
    CHECK(takeStopCue(), "walk: the ledge walk's player stop sounds the stop earcon");
    CHECK(BTNCALLS == 0, "walk: set_button was never called on the ledge route either");

    // Detach drops a stop earcon nobody polled: the session is gone.
    walkSetup(2232, 2792, 0x15);
    guideTo("Desert of Mystery");
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    simFrames(30, true);
    oga::kZeldaLinkToThePast.command(oga::Command::WalkToggle);
    oga::kZeldaLinkToThePast.detach();
    oga::kZeldaLinkToThePast.attach(&H2);
    CHECK(!takeStopCue(), "walk: detach drops an unpolled stop earcon");

    // ---- the push-block census (docs/design/auto-walk.md §6) ---------------
    // The game's own table at $7E:F940: 99 slots of {u16 room, u16
    // tilemap}. Outdoors it describes the last dungeon, so it answers -1;
    // a never-initialised (all-zero) table answers -1; otherwise the
    // count for the room, with the blocks' 16 px tiles.
    {
        const uint32_t MB = 0x7EF940u;
        for (int i = 0; i < 99 * 4; i++) put8(MB + (uint32_t) i, 0);
        put8(INDOORS, 0);
        CHECK(oga::AlttpPushBlocksInRoom(0x0099, nullptr, 0) == -1,
              "push blocks: outdoors the census is unknown");
        put8(INDOORS, 1);
        CHECK(oga::AlttpPushBlocksInRoom(0x0099, nullptr, 0) == -1,
              "push blocks: an all-zero table is unknown, never 'none'");
        /* Two blocks in room 0x99, one in 0x2A. Block A: 8 px tile
         * (col 20, row 10) on BG2 -> dsto 10*64+20, tilemap dsto*2.
         * Block B: (col 40, row 50) on BG1 (the 0x2000 layer bit). */
        auto putBlock = [&](int slot, uint16_t room, int col8, int row8, bool bg1) {
            uint16_t tm = (uint16_t) ((row8 * 64 + col8) * 2) | (bg1 ? 0x2000 : 0);
            put16(MB + (uint32_t) slot * 4, room);
            put16(MB + (uint32_t) slot * 4 + 2, tm);
        };
        putBlock(0, 0x0099, 20, 10, false);
        putBlock(1, 0x002A, 4, 4, false);
        putBlock(7, 0x0099, 40, 50, true);
        uint16_t tiles[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
        int n = oga::AlttpPushBlocksInRoom(0x0099, tiles, 4);
        CHECK(n == 2, "push blocks: the census counts the room's blocks");
        CHECK(tiles[0] == 5 * 32 + 10 && tiles[1] == 25 * 32 + 20,
              "push blocks: positions come back as the room's 16 px tiles, either layer");
        CHECK(oga::AlttpPushBlocksInRoom(0x002A, nullptr, 0) == 1 &&
              oga::AlttpPushBlocksInRoom(0x0010, nullptr, 0) == 0,
              "push blocks: another room's blocks are its own; an empty room is 0");
        /* A block pushed into a pit moves to the room below in the
         * game's own table; the census follows it. */
        put16(MB + 0 * 4, 0x00A9);
        CHECK(oga::AlttpPushBlocksInRoom(0x0099, nullptr, 0) == 1 &&
              oga::AlttpPushBlocksInRoom(0x00A9, nullptr, 0) == 1,
              "push blocks: a block that fell through a pit counts in the room below");
        uint16_t one = 0xFFFF;
        CHECK(oga::AlttpPushBlocksInRoom(0x0099, &one, 0) == 1 && one == 0xFFFF,
              "push blocks: cap 0 counts without writing");
        put16(ROOM, 0x0099);
        NLOGGED = 0;
        oga::kZeldaLinkToThePast.command(oga::Command::DumpState);
        CHECK(NLOGGED >= 1 && strstr(LOGGED[0], " pushblocks=1") != nullptr,
              "push blocks: DumpState logs the current room's census");
        put8(INDOORS, 0);
        for (int i = 0; i < 99 * 4; i++) put8(MB + (uint32_t) i, 0);
    }

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
