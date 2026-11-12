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
static void put16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }
static void put32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void putbytes(uint32_t a, const char* s, size_t n) { memcpy(&RAM[OFF(a)], s, n); }

static const uint32_t PARTY0 = 0x020F3888u, PSTRIDE = 0x964u;
static const uint32_t MAPCODE = 0x020FB3FCu, PPTR = 0x020F33E0u;
static const uint32_t GOLD = 0x020F6D48u, BATTLE = 0x020EF0E8u;
static const uint32_t CAMERA = 0x0210A134u;
static const uint32_t OBJTABLE = 0x02107600u, OBJCOUNT = 0x02107680u;
static const uint32_t MENU = 0x02118FFCu, PHASE = 0x02109DA6u, CHOICE = 0x021153A4u;
static const uint32_t TITLE1 = 0x0231624Cu, TITLE2 = 0x023162D4u; // title menu objects
static const uint32_t GRID = 0x022A517Cu;   // creation grid / name keyboard control

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
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Someone") && strstr(SPOKEN[0], "5 steps up")
          && strstr(SPOKEN[0], "1 of 2"),
          "NextEnemy names the nearest person with camera-relative direction and its ordinal");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Story character") && strstr(SPOKEN[0], "20 steps left")
          && strstr(SPOKEN[0], "2 of 2"),
          "second NextEnemy steps to the farther story model, ordinal 2 of 2");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Someone") && strstr(SPOKEN[0], "1 of 2"),
          "PrevEnemy steps back to the nearest, ordinal 1 of 2");

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

    // ---- automatic announcements (on_frame) --------------------------------
    // From here the test drives frames as well as commands. setMenu replaces
    // the whole markup buffer (zeroed first: the reader stops at the NUL).
    auto setMenu = [&](const char* s) {
        memset(&RAM[OFF(MENU)], 0, 1200);
        if (*s) putbytes(MENU, s, strlen(s));
    };
    auto frames = [&](int n) { for (int i = 0; i < n; i++) oga::kDragonQuestIX.on_frame(); };

    // A menu appears: silent on its first frame (half-built buffers are
    // real), one line once steady, then silence while nothing changes.
    setMenu("<CURSOR=1><N=0>Fight</N><N=1>Spells</N>");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(1);
    CHECK(NSPOKEN == 0, "auto: a menu does not announce on its first frame");
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Menu. Spells. 2 items"),
          "auto: menu appearance announces the cursor item and the count");
    frames(3);
    CHECK(NSPOKEN == 1, "auto: an unchanged menu stays silent");
    // Cursor move inside a persisting menu: the newly selected item only.
    setMenu("<CURSOR=0><N=0>Fight</N><N=1>Spells</N>");
    frames(2);
    CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "Fight") == 0,
          "auto: a cursor move announces the newly selected item only");
    // Close: silence (including the redraw grace), reopen: announced again.
    setMenu("");
    frames(16);
    CHECK(NSPOKEN == 2, "auto: a menu closing is silent");
    setMenu("<CURSOR=0><N=0>Fight</N><N=1>Spells</N>");
    frames(2);
    CHECK(NSPOKEN == 3 && strstr(SPOKEN[2], "Menu. Fight. 2 items"),
          "auto: a reopened menu announces again");

    // Battle: away from the command phase nothing announces, even when the
    // buffer changes underneath; at phase 3 the pending change lands.
    put8(BATTLE, 1); put8(PHASE, 0);
    setMenu("<CURSOR=1><N=0>Fight</N><N=1>Spells</N>");
    frames(3);
    CHECK(NSPOKEN == 3, "auto: battle away from the command phase stays silent");
    put8(PHASE, 3);
    frames(2);
    CHECK(NSPOKEN == 4 && strcmp(SPOKEN[3], "Spells") == 0,
          "auto: at the command phase the pending menu change announces");
    put8(BATTLE, 0); put8(PHASE, 0);

    // Yes/no edge: announced once when the prompt appears, quiet while it
    // is held, and the edge re-arms once the prompt clears.
    setMenu("");
    frames(16);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    put32(CHOICE, 1);
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Choose. Yes or no"),
          "auto: a yes/no prompt announces once when it appears");
    frames(2);
    CHECK(NSPOKEN == 1, "auto: a held yes/no prompt does not repeat");
    put32(CHOICE, 0);
    frames(1);
    oga::AdapterSpeechReset();   // the dedup layer is separate; the edge is what is tested
    put32(CHOICE, 1);
    frames(1);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Choose. Yes or no"),
          "auto: the yes/no edge re-arms after the prompt clears");
    put32(CHOICE, 0);
    frames(1);

    // ---- start flow: title --------------------------------------------------
    // Tear the world down to a title-shaped machine: no party, no map, no
    // player, the battle flag SET (title junk, as in the live snapshot),
    // and the two script-engine title objects, the no-log one filled.
    memset(&RAM[OFF(PARTY0)], 0, 32);
    memset(&RAM[OFF(PARTY0 + PSTRIDE)], 0, 32);
    memset(&RAM[OFF(MAPCODE)], 0, 8);
    put32(PPTR, 0);
    put8(BATTLE, 1);
    setMenu("");
    put32(TITLE1, 0x021FE80Cu); put16(TITLE1 + 6, 0x12D);
    put16(TITLE1 + 0x74, 50);   put32(TITLE1 + 0x28, 0);
    put32(TITLE2, 0x021FE80Cu); put16(TITLE2 + 6, 0x12E);
    put16(TITLE2 + 0x74, 0);    put32(TITLE2 + 0x28, 0);
    CHECK(oga::kDragonQuestIX.ready(), "title: a built title menu is ready with no party");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(39);
    CHECK(NSPOKEN == 0, "title: silent while the menu settles");
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title screen. Create a new adventure log"),
          "title: the settled menu announces its cursor item");
    put32(TITLE1 + 0x28, 1);
    frames(1);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Wi-Fi"),
          "title: a cursor move announces the new item");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Menu. Configure Nintendo Wi-Fi Connection settings. 2 items"),
          "title: MenuState answers the title menu");
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "No party members"),
          "title: party cycling still refuses without a party");
    // Neither object is a menu but script objects exist: the opening movie.
    put32(TITLE1, 0x11111111u);
    put32(TITLE2, 0x22222222u);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(89);
    CHECK(NSPOKEN == 0, "title: the opening movie is silent before its line is due");
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Opening movie"),
          "title: the opening movie announces once");
    frames(5);
    CHECK(NSPOKEN == 1, "title: the opening movie does not repeat");

    // ---- start flow: character creation -------------------------------------
    // The PLTT field list (separators are a literal backslash-n, as the
    // game writes them). Field 3 (Hairstyle) is highlighted.
    setMenu("<PLTT=15><X=33>Male\\n<PLTT=15><X=22>Type 5\\n<PLTT=14><X=22>Type 3\\n");
    CHECK(oga::kDragonQuestIX.ready(), "creation: the field list is ready with no party");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(1);
    CHECK(NSPOKEN == 0, "creation: a field does not announce on its first frame");
    frames(1);
    CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Hairstyle: Type 3, bowl cut") == 0,
          "creation: the highlighted field announces its value and its look");
    setMenu("<PLTT=15><X=33>Male\\n<PLTT=15><X=22>Type 5\\n<PLTT=14><X=22>Type 4\\n");
    frames(2);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Hairstyle: Type 4, short crop with a fringe"),
          "creation: changing the picture announces the new look");
    setMenu("<PLTT=15><X=33>Male\\n<PLTT=14><X=22>Type 2\\n<PLTT=15><X=22>Type 4\\n");
    frames(2);
    CHECK(NSPOKEN == 3 && strstr(SPOKEN[2], "Body type: Type 2, tall"),
          "creation: moving to another field announces it");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Body type: Type 2, tall"),
          "creation: MenuState answers the highlighted field");
    // Gender lives on the 1x2 grid over the shared control, not in a value.
    put32(GRID + 0x20, GRID + 0xC);  put32(GRID + 0x24, GRID + 8);
    put32(GRID + 0x28, GRID + 0x1C); put32(GRID + 0x2C, GRID + 0x18);
    put32(GRID + 8, 1); put32(GRID + 0xC, 2);
    put32(GRID + 0x18, 0); put32(GRID + 0x1C, 0);
    setMenu("<PLTT=14><X=33>Male\\n");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Gender: Male, 1 of 2"),
          "creation: the gender grid announces the selection");
    put32(GRID + 0x1C, 1);
    frames(1);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Gender: Female, 2 of 2"),
          "creation: moving on the gender grid announces the other option");

    // ---- start flow: name entry ----------------------------------------------
    // The same control in its 11x6 keyboard shape; the cursor sits on q.
    put32(GRID + 8, 11); put32(GRID + 0xC, 6);
    put16(GRID + 0x60, 40); put16(GRID + 0x62, 80);
    setMenu("<X=18>Retro***");
    CHECK(oga::kDragonQuestIX.ready(), "name entry: the keyboard screen is ready with no party");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    frames(1);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Name entry. Name so far: Retro.")
          && SPOKEN[0][strlen(SPOKEN[0]) - 1] == 'q',
          "name entry: arrival announces the name so far and the key under the cursor");
    put16(GRID + 0x60, 56);   // w
    frames(1);
    CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "w") == 0,
          "name entry: moving the cursor echoes the new key");
    setMenu("<X=18>RetroX**");
    frames(1);
    CHECK(NSPOKEN == 2, "name entry: a typed letter waits for the name to settle");
    frames(1);
    CHECK(NSPOKEN == 3 && strstr(SPOKEN[2], "X, name is now RetroX"),
          "name entry: typing announces the letter and the name");
    setMenu("<X=18>Retro***");
    frames(2);
    CHECK(NSPOKEN == 4 && strstr(SPOKEN[3], "Deleted, name is now Retro"),
          "name entry: deleting announces the shorter name");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Name entry. Name so far: Retro."),
          "name entry: MenuState answers the name so far");
    put32(GRID + 0x20, 0);   // keyboard control torn down
    setMenu("");
    frames(2);
    CHECK(NSPOKEN == 1, "name entry: closing the keyboard is silent");

    // ---- start flow: the creation confirm ------------------------------------
    // Arm step 8 (the name field highlighted in the full list), restore the
    // control pointers, then the Yes/No menu the game shows — with the
    // extra <SDRC> tag the real buffer carries (capture, Nov 2026).
    put32(GRID + 0x20, GRID + 0xC);
    put32(GRID + 8, 5); put32(GRID + 0xC, 2);   // a picture grid, not the keyboard
    setMenu("<PLTT=15><X=33>Male\\n<PLTT=15><X=22>Type 5\\n<PLTT=15><X=22>Type 1\\n"
            "<PLTT=15><X=22>Type 9\\n<PLTT=15><X=22>Type 7\\n<PLTT=15><X=22>Type 8\\n"
            "<PLTT=15><X=22>Type 6\\n<PLTT=14><X=46>Retro\\n");
    frames(2);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    setMenu("<CURSOR=0><SDRC=0,0,0,48,40><N=0>Yes</N>\n<N=1>No</N>");
    frames(2);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Begin your adventure with this character? Yes, 1 of 2"),
          "confirm: the creation confirm asks its question instead of reading a bare list");
    setMenu("<CURSOR=1><SDRC=0,0,0,48,40><N=0>Yes</N>\n<N=1>No</N>");
    frames(2);
    CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "No") == 0,
          "confirm: moving to No announces the item");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Begin your adventure with this character? No, 2 of 2"),
          "confirm: MenuState answers the same question form");

    // ---- the world comes back: in-game behavior is untouched ------------------
    setMenu("");
    memset(&RAM[OFF(GRID)], 0, 0x100);
    memset(&RAM[OFF(TITLE1)], 0, 0x90);
    memset(&RAM[OFF(TITLE2)], 0, 0x90);
    putbytes(MAPCODE, "M01M0100", 8);
    put32(PPTR, 0x02100000u);
    putbytes(PARTY0, "Hero", 5);
    putbytes(PARTY0 + PSTRIDE, "Biscuit", 8);
    put8(BATTLE, 0); put8(PHASE, 0);
    frames(16);   // every watcher settles silent on the restored world
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "M01M0100"),
          "restored world: WhereAmI still answers");
    oga::kDragonQuestIX.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Biscuit") && strstr(SPOKEN[1], "2 of 4"),
          "restored world: party cycling still answers");
    setMenu("<CURSOR=1><N=0>Fight</N><N=1>Spells</N>");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    oga::kDragonQuestIX.command(oga::Command::MenuState);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Menu. Spells. 2 items"),
          "restored world: MenuState still reads the menu");
    setMenu("");

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
