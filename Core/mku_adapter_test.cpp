/*
 * mku_adapter_test.cpp — host test for the Mortal Kombat: Unchained adapter,
 * against SYNTHETIC memory.
 *
 * WHY SYNTHETIC: the adapter's logic today is identity, honest speech,
 * the DumpState anchor scan, and the first verified structure (the main
 * menu, behind its cursor + current-item-pointer gates). Feeding it a fake RAM image
 * with menu strings planted at known addresses tests exactly that logic
 * (scan finds them, reports their real addresses, and does not invent hits)
 * without booting the game, the same rule the other adapter tests follow.
 *
 * ⛔ THE MOCK MUST MAP ABSOLUTE ADDRESSES. The adapter reads ABSOLUTE PSP
 * addresses (user RAM starts at 0x08800000); the mock translates them to a
 * buffer offset, exactly as a real Host does. Subtract the base, then mask
 * with the buffer size — indexing the buffer with the absolute address
 * would overflow instantly. And the buffer must be a POWER-OF-TWO size:
 * this mock started at 24 MB (the literal user-RAM size), whose mask drops
 * bit 23 and aliases every address 8 MB up — each planted string appeared
 * twice. 32 MB with a 0x1FFFFFF mask is exact.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t RAM[0x2000000];                       // 32 MB mock of PSP RAM
static const uint32_t RAMBASE = 0x08800000;
static inline size_t OFF(uint32_t a) { return (size_t)((a - RAMBASE) & 0x1FFFFFF); }

static char SPOKEN[64][192]; static int NSPOKEN = 0;
static char LOGGED[96][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 64) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 96) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

// ⛔ DECLARE THEM INSIDE THE NAMESPACE. `extern const oga::Adapter name;` at
// global scope declares a DIFFERENT variable (dbz_adapter_test.cpp documents
// the resulting link error).
namespace oga {
extern const Adapter kMortalKombatUnchained;
extern const Adapter kDissidiaFinalFantasy;
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
}

static void put_ascii(uint32_t a, const char* s)
{
    for (int i = 0; s[i]; i++) RAM[OFF(a) + i] = (uint8_t) s[i];
}
static void put_utf16(uint32_t a, const char* s)
{
    for (int i = 0; s[i]; i++) { RAM[OFF(a) + 2 * i] = (uint8_t) s[i]; RAM[OFF(a) + 2 * i + 1] = 0; }
}
static void put_u32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static bool logged_has(const char* needle)
{
    for (int i = 0; i < NLOGGED; i++) if (strstr(LOGGED[i], needle)) return true;
    return false;
}

int main()
{
    // Planted menu strings, at addresses chosen to be easy to verify by eye.
    put_ascii(0x08910000, "KONQUEST");        // a real anchor, ASCII
    put_ascii(0x08920000, "ARCADE");          // a real anchor, ASCII
    put_utf16(0x08930000, "VERSUS");          // a real anchor, UTF-16LE
    put_ascii(0x08940000, "KONQUAST");        // ⛔ DECOY: one letter wrong

    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;

    int pass = 0, fail = 0;
    #define CHECK(c,m) do { if (c) { pass++; } else { fail++; printf("  FAIL: %s\n", m); } } while (0)

    const oga::Adapter* a = &oga::kMortalKombatUnchained;

    // ---- identity -------------------------------------------------------------
    CHECK(strcmp(a->game_code, "ULUS10102") == 0, "game code ULUS10102");
    CHECK(oga::find_by_game_code("ULUS10102") == a, "registry resolves ULUS10102");
    CHECK(oga::find_by_game_code("ULUS10437") == &oga::kDissidiaFinalFantasy,
          "ULUS10437 still resolves to Dissidia (full-string match, no prefix theft)");
    CHECK(oga::find_by_game_code("ULUS1010") == nullptr,
          "a truncated PSP id matches nothing");

    CHECK(a->attach(&H), "attach accepts the host");

    // ---- scaffold honesty -------------------------------------------------------
    CHECK(!a->ready(), "not ready with the main-menu signature absent (RAM zeroed)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
          "WhereAmI states the scaffold scope instead of inventing a location");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not mapped yet"),
          "NextAlly refuses honestly until fight state is located");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not mapped yet"),
          "NextEnemy refuses honestly too");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "menu commands are ignored silently (no tracked menu yet)");

    // ---- the anchor scan ---------------------------------------------------------
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(NLOGGED >= 5 && strstr(LOGGED[0], "anchor scan"),
          "DumpState logs the scan header");
    CHECK(logged_has("\"KONQUEST\" ascii @ 0x08910000"),
          "the scan finds the planted ASCII KONQUEST at its real address");
    CHECK(logged_has("\"ARCADE\" ascii @ 0x08920000"),
          "the scan finds the planted ASCII ARCADE");
    CHECK(logged_has("\"VERSUS\" utf16 @ 0x08930000"),
          "the scan finds the planted UTF-16LE VERSUS");
    CHECK(!logged_has("0x08940000"),
          "the decoy KONQUAST produces no hit (no near-match reporting)");
    CHECK(logged_has("scan done: 3 hits"),
          "the scan summary counts exactly the planted anchors");

    // ---- the main menu (verified structure, synthetic plant) --------------------
    // The gate: cursor (1..9) + the current-item pointer holding exactly
    // the pool object the cursor names (magic " OBJ" at +4) + the Kombat
    // submenu's pointer NOT holding one of its objects.
    put_u32(0x09E61750, 4);                          // cursor: Konquest
    put_u32(0x0911F868, 0x0911D060);                 // holder -> object 4
    put_u32(0x0911D060 + 4, 0x4F424A20);             // object magic
    put_u32(0x09118348, 0);                          // submenu pointer: null
    CHECK(a->ready(), "ready on the main menu (cursor, holder and magic agree)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Main menu") &&
          strstr(SPOKEN[0], "Konquest") && strstr(SPOKEN[0], "4 of 9"),
          "WhereAmI speaks the verified main-menu row");

    put_u32(0x09E61750, 5);                          // the tap lands on MK Wireless
    put_u32(0x0911C380 + 4, 0x4F424A20);
    put_u32(0x0911F868, 0x0911C380);                 // holder follows the cursor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "MK Wireless") &&
          strstr(SPOKEN[0], "5 of 9"),
          "MenuNext speaks the row the cursor landed on");

    put_u32(0x09E61750, 6);                          // cursor moves, holder does not:
    CHECK(!a->ready(), "a cursor/holder disagreement is not usable state");

    put_u32(0x09E61750, 5);
    put_u32(0x09118348, 0x091181E0);                 // the Kombat submenu opens on top
    CHECK(!a->ready(), "not ready while a child screen covers the menu");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "menu nav is silent while the submenu is on top");
    put_u32(0x09118348, 0);

    put_u32(0x09E61750, 10);
    CHECK(!a->ready(), "an out-of-range cursor is not usable state");
    put_u32(0x09E61750, 0); put_u32(0x0911F868, 0);

    // ---- character select (verified structure, synthetic plant) -----------------
    put_u32(0x08C1B3B0, 14);                         // cursor: Blaze (row 2, col 5)
    put_u32(0x09F9C1A8, 0x0910E0A0);                 // holder -> Blaze's object
    put_u32(0x0910E0A0 + 4, 0x4F424A20);             // object magic
    CHECK(a->ready(), "ready on character select (index, holder and magic agree)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Character select") &&
          strstr(SPOKEN[0], "Blaze") && strstr(SPOKEN[0], "row 2, column 5"),
          "WhereAmI speaks the verified character-select cell");

    put_u32(0x08C1B3B0, 15);                         // RIGHT lands on Jax
    put_u32(0x0910DC10 + 4, 0x4F424A20);
    put_u32(0x09F9C1A8, 0x0910DC10);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuRight);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Jax") &&
          strstr(SPOKEN[0], "row 2, column 6"),
          "MenuRight speaks the cell the cursor landed on");

    put_u32(0x08C1B3B0, 16);                         // index moves, holder does not
    CHECK(!a->ready(), "an index/holder disagreement is not usable state");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "grid nav is silent when the gate fails");

    put_u32(0x08C1B3B0, 30);                         // one past the last fighter
    CHECK(!a->ready(), "an out-of-range index is not usable state");
    put_u32(0x08C1B3B0, 0); put_u32(0x09F9C1A8, 0);

    // ---- the Kombat submenu (verified structure, synthetic plant) ---------------
    // The front end keeps the parent index while the child screen is up: measured
    // on the game, the cursor reads 1 with the Kombat submenu open.
    put_u32(0x09E61750, 1);
    put_u32(0x09118348, 0x09117500);                 // holder -> Endurance's object
    put_u32(0x09117500 + 4, 0x4F424A20);             // object magic
    CHECK(a->ready(), "ready on the Kombat submenu (holder names a live object)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Kombat menu") &&
          strstr(SPOKEN[0], "Endurance") && strstr(SPOKEN[0], "2 of 3"),
          "WhereAmI speaks the verified submenu row");

    put_u32(0x09118348, 0x09116820);                 // DOWN lands on Practice
    put_u32(0x09116820 + 4, 0x4F424A20);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Practice") &&
          strstr(SPOKEN[0], "3 of 3"),
          "MenuNext speaks the submenu row landed on");

    put_u32(0x09118348, 0);                          // submenu closed
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "submenu nav is silent when its holder is null");
    put_u32(0x09118348, 0x09117500);                 // holder back, magic gone:
    put_u32(0x09117500 + 4, 0);
    CHECK(!a->ready(), "a holder pointing at a dead object is not usable state");
    put_u32(0x09118348, 0);

    // The attract demo plays a fight with the holder still naming a live object
    // (measured), so a dead front end must shut the submenu gate even though the
    // holder and its magic still check out.
    put_u32(0x09118348, 0x091181E0);
    put_u32(0x091181E0 + 4, 0x4F424A20);
    put_u32(0x09E61750, 0);                          // attract demo: index reads 0
    CHECK(!a->ready(), "a stale submenu holder in the attract demo is not usable state");
    put_u32(0x09E61750, 1);                          // front end live again

    // The submenu holder goes STALE in Konquest: it keeps a visited
    // object (magic intact) while the mode runs, so the plant below
    // would pass the old gate. The mode flag must shut it.
    put_u32(0x09118348, 0x091181E0);
    put_u32(0x091181E0 + 4, 0x4F424A20);
    put_u32(0x08AF69C8, 1); put_u32(0x08B123DC, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Konquest") &&
          !strstr(SPOKEN[0], "Kombat menu"),
          "the mode flag shuts the stale submenu gate in Konquest");
    put_u32(0x08AF69C8, 0); put_u32(0x08B123DC, 0);
    put_u32(0x09118348, 0);

    // ---- fight state (verified structure, synthetic plant) ----------------------
    // Neutralize every screen gate so only the fight gate can fire.
    put_u32(0x09E61750, 0); put_u32(0x0911F868, 0);  // main menu off
    put_u32(0x08C1B3B0, 0); put_u32(0x09F9C1A8, 0);  // character select off
    put_u32(0x09118348, 0);                          // submenu off
    auto put_f32 = [](uint32_t a, float f) {
        uint32_t bits; memcpy(&bits, &f, 4); put_u32(a, bits);
    };
    put_u32(0x08D929B4, 2);                          // P1: Scorpion
    put_f32(0x08D929B8, 0.904f);                     // P1 HP after the idle stretch
    put_u32(0x08D929EC, 1);                          // P1 wins
    put_f32(0x08D92A24, 0.333f);                     // P2 (Goro) HP
    put_u32(0x08D92A2C, 4);                          // P2: Goro
    put_u32(0x08D92A58, 0);                          // P2 wins
    put_u32(0x08D92AE0, 2);                          // round 2
    put_u32(0x08D92AF0, 48);                         // timer
    CHECK(a->ready(), "ready in a live fight (fight struct verifies)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Round 2") &&
          strstr(SPOKEN[0], "Scorpion 90 percent") &&
          strstr(SPOKEN[0], "Goro 33 percent") &&
          strstr(SPOKEN[0], "48 seconds left") &&
          strstr(SPOKEN[0], "You lead 1 to 0"),
          "WhereAmI speaks the verified fight state");

    put_u32(0x08D92AE0, 0);                          // no live round
    CHECK(!a->ready(), "fight gate closes when the round counter is 0");
    put_u32(0x08D92AE0, 2);
    put_f32(0x08D929B8, 0.0f); put_f32(0x08D92A24, 0.0f);
    CHECK(!a->ready(), "fight gate closes when nobody is standing");
    put_f32(0x08D929B8, 0.904f); put_f32(0x08D92A24, 0.333f);
    put_u32(0x08D92AF0, 0);                          // (leave a clean state)

    // ---- the pause menu (relocation by signature + nav confirmation) ------------
    // Fight plant is still live from the block above; restore round/timer.
    put_u32(0x08D92AE0, 1);                          // round 1
    put_u32(0x08D92AF0, 48);                         // timer (frozen)
    // Pause block at this boot's heap address (hunt12's), objects stepping
    // -0xAF0 from RESUME, magic at +4, class word at +0x0C.
    const uint32_t PTOP = 0x090607D0;
    for (int k = 0; k < 4; k++) {
        uint32_t obj = PTOP - (uint32_t) k * 0xAF0;
        put_u32(obj + 4, 0x4F424A20);
        put_u32(obj + 0x0C, 0x08BAF3C0);
    }
    const uint32_t HOLDER = PTOP + 0xFAC;            // holder rides at top+0xFAC
    put_u32(HOLDER, PTOP);                           // cursor on Resume
    put_u32(0x09044828, PTOP);                       // decoy link cell (never moves)
    const uint32_t TRAILER = 0x090462B8;             // trailing copy (hunt13's ring)
    put_u32(TRAILER, PTOP);                          // also steps, a few frames later

    for (int i = 0; i < 300; i++) a->on_frame();     // timer frozen -> scan runs
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Round 1"),
          "before holder confirmation, WhereAmI speaks the fight, not the pause");

    // First stepping tap (DOWN), phased like the real widget: the holder
    // moves first, the trailing copy follows a few frames later. The
    // adapter must confirm the HOLDER (earliest change), not the copy.
    put_u32(HOLDER, PTOP - 0xAF0);
    for (int i = 0; i < 2; i++) a->on_frame();
    put_u32(TRAILER, PTOP - 0xAF0);
    for (int i = 0; i < 2; i++) a->on_frame();
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Moves List") &&
          strstr(SPOKEN[0], "2 of 4"),
          "one stepping tap confirms the holder (not its trailing copy)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Paused") &&
          strstr(SPOKEN[0], "Moves List"),
          "WhereAmI speaks the pause menu once the holder is confirmed");

    put_u32(HOLDER, PTOP - 3 * 0xAF0);               // DOWN to Main Menu
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Main Menu") &&
          strstr(SPOKEN[0], "4 of 4"), "pause nav speaks Main Menu, 4 of 4");

    put_u32(HOLDER, PTOP);                           // DOWN wraps to Resume
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Resume") &&
          strstr(SPOKEN[0], "1 of 4"), "pause nav wraps to Resume, 1 of 4");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuLeft);
    CHECK(NSPOKEN == 0, "left/right stay silent in pause (quit dialog territory)");

    put_u32(0x08D92AF0, 47);                         // timer ticks: unpaused
    for (int i = 0; i < 3; i++) a->on_frame();
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Round 1") &&
          !strstr(SPOKEN[0], "Paused"),
          "a ticking timer closes the pause gate (fight line again)");

    // ---- Konquest mode flag: the arcade fight gate must stay shut there --------
    put_u32(0x08D92AF0, 48);                         // freeze the timer again
    for (int i = 0; i < 3; i++) a->on_frame();
    put_u32(0x08AF69C8, 1);                          // both mode words set
    put_u32(0x08B123DC, 1);
    CHECK(a->ready(), "ready in Konquest (mode flag verifies)");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Konquest") &&
          !strstr(SPOKEN[0], "Scorpion"),
          "in Konquest, WhereAmI speaks the mode, never the arcade fight line");
    put_u32(0x08AF69C8, 0);                          // one word is not enough
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Round 1"),
          "a single mode word does not claim Konquest");
    put_u32(0x08B123DC, 0);

    // ---- Konquest dialogue: signed buffer, box-up pair, settle-to-speak --------
    put_u32(0x08AF69C8, 1); put_u32(0x08B123DC, 1);
    {   // the text-box struct signature (12 words before the string)
        const uint32_t base = 0x09178568 - 48;
        const uint32_t sig[12] = {0x30, 0xE9, 0x24, 0x180, 0x2A, 0x11,
                                  0xFFFFFFFF, 0xC8C8C8FF, 0x6, 0x0,
                                  0x08AFD1C0, 0x0};
        for (int i = 0; i < 12; i++) put_u32(base + (uint32_t) i * 4, sig[i]);
    }
    put_u32(0x09178D30, 0x091782A0);                 // box-up pointer set
    put_ascii(0x09178568, "Step toward me three times.");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Step toward me three times."),
          "a settled dialogue line auto-speaks exactly once");
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 1, "the same line does not repeat while it stays up");
    put_ascii(0x09178568, "Now step away from me three times.");
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "step away"),
          "the next line speaks when it settles");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "step away"),
          "WhereAmI in dialogue speaks the current line");
    put_u32(0x09178D30, 0);                          // box closes
    for (int i = 0; i < 5; i++) a->on_frame();
    put_u32(0x09178D30, 0x091782A0);                 // same line shown again
    NSPOKEN = 0; oga::AdapterSpeechReset();
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "step away"),
          "a re-shown line speaks again after the box closed");
    put_u32(0x09178568 - 48 + 40, 0xDEADBEEF);       // break the signature
    NSPOKEN = 0; oga::AdapterSpeechReset();
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 0, "a broken signature fails closed (no dialogue speech)");
    put_u32(0x08AF69C8, 0); put_u32(0x08B123DC, 0);

    // ---- Konquest objective line (measured 2026-10-19, dojo tutorial) --------
    // The red instruction text uses the same buffer and struct as dialogue, but
    // the colour word reads 0xFF4848FF and no box is drawn. The old gate
    // treated that as "box down" and a blind player heard nothing.
    put_u32(0x08AF69C8, 1); put_u32(0x08B123DC, 1);
    {
        const uint32_t base = 0x09178568 - 48;
        const uint32_t sig[12] = {0x30, 0xE9, 0x24, 0x180, 0x2A, 0x11,
                                  0xFFFFFFFF, 0xC8C8C8FF, 0x6, 0x0,
                                  0x08AFD1C0, 0x0};
        for (int i = 0; i < 12; i++) put_u32(base + (uint32_t) i * 4, sig[i]);
    }
    put_u32(0x09178D30, 0x091782A0);
    put_u32(0x09178554, 0xFF4848FF);                 // red objective tint
    put_ascii(0x09178568, "Your first exercise will be to step toward me three times.");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Objective.") &&
          strstr(SPOKEN[0], "step toward me three times"),
          "the red objective line speaks with an Objective prefix");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Objective."),
          "WhereAmI speaks the live objective first");

    put_u32(0x09178554, 0xC8C8C8FF);                 // ordinary dialogue again
    put_ascii(0x09178568, "Well done, Shujinko.");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    for (int i = 0; i < 45; i++) a->on_frame();
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Objective.") &&
          strstr(SPOKEN[0], "Well done"),
          "a dialogue line stays on the dialogue channel");
    put_u32(0x08AF69C8, 0); put_u32(0x08B123DC, 0);

    // ---- a child screen that REUSES the Kombat submenu's object slot --------
    // Measured live 2026-10-18 on the Options screen: the cursor read 9
    // (Options), the current-item pointer named object 9, and the child cell
    // at kKombatSubHolderAddr held 0x091181E0 — the same live object the
    // Kombat list uses, because the slot is shared. The fight struct's words
    // also still read round 1, timer 72 and HP 1.0/1.0. Both older gates
    // passed that state ("Kombat menu, Arcade" and a fight line); the parent
    // index separates the screens, and a live front-end row outranks leftover
    // fight words.
    put_u32(0x09E61750, 9);
    put_u32(0x09119000 + 4, 0x4F424A20);             // object 9 magic
    put_u32(0x0911F868, 0x09119000);                 // holder on Options
    put_u32(0x091181E0 + 4, 0x4F424A20);             // child slot: a live object
    put_u32(0x091181E0 + 0x0C, 0x08BAF3C0);
    put_u32(0x09118348, 0x091181E0);
    put_u32(0x08D929B4, 0); put_u32(0x08D92A2C, 0);  // leftover fight words,
    put_f32(0x08D929B8, 1.0f); put_f32(0x08D92A24, 1.0f);
    put_u32(0x08D92AE0, 1); put_u32(0x08D92AF0, 72); // exactly as measured
    CHECK(a->ready(), "the Options tabs are a mapped state");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Options") &&
          strstr(SPOKEN[0], "Gameplay") &&
          !strstr(SPOKEN[0], "Kombat menu") && !strstr(SPOKEN[0], "Round"),
          "Options speaks its tab, not the Kombat list or a fight line");
    put_u32(0x09118348, 0);                          // close the child screen
    put_u32(0x09E61750, 0); put_u32(0x0911F868, 0);
    put_u32(0x08D92AE0, 0); put_u32(0x08D92AF0, 0);

    // ---- the Options settings lists (verified live 2026-10-18) --------------
    // Tabs: parent index 9 plus one of the three shared child objects.
    put_u32(0x09E61750, 9);
    put_u32(0x0911F868, 0);
    put_u32(0x09118348, 0x09117500);                 // Audio tab
    put_u32(0x09117500 + 4, 0x4F424A20);
    CHECK(a->ready(), "ready on the Options tabs");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Options") &&
          strstr(SPOKEN[0], "Audio, 2 of 3"),
          "WhereAmI speaks the Options tab");

    // GAMEPLAY rows step 0x1160 down from 0x09106040; row 2 is Chess CPU.
    put_u32(0x09118348, 0);
    put_u32(0x09104000 + 0, 0);                      // (harmless gap filler)
    put_u32(0x09106040 - 0x1160 + 4, 0x4F424A20);
    put_u32(0x09119CE8, 0x09106040 - 0x1160);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Chess CPU, 2 of 8"),
          "WhereAmI speaks the Gameplay settings row");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Chess CPU, 2 of 8"),
          "a tap inside the Gameplay list speaks the landed row");

    // AUDIO rows: 5, stride 0x10B0 from 0x0910B400.
    put_u32(0x0910B400 + 4, 0x4F424A20);
    put_u32(0x09119CE8, 0x0910B400);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Game Music, 1 of 5"),
          "WhereAmI speaks the Audio settings row");

    // A pointer outside every measured run fails closed.
    put_u32(0x09119CE8, 0x09123450);
    CHECK(!a->ready(), "an unmapped row pointer is not usable state");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "menu nav stays silent when the row pointer is unmapped");
    put_u32(0x09119CE8, 0);
    put_u32(0x09E61750, 0);

    // ---- detach -------------------------------------------------------------------
    a->detach();
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 0, "after detach the adapter is silent");

    printf("  mku adapter: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

// Stubs so adapters.o links without dragging in the FE/GBA machinery (the same
// arrangement dbz_adapter_test.cpp uses).
namespace oga {
static bool s_att(const Host*) { return false; }
static void s_frm() {}
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11","FE","YFEE",s_att,s_frm,s_cmd,s_rdy,s_det};
const Adapter kGameBoyAdvance        = {"gba","GBA","",    s_att,s_frm,s_cmd,s_rdy,s_det};
}
