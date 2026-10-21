/*
 * khbbs_adapter_test.cpp — host test for the Kingdom Hearts: Birth by
 * Sleep adapter, against SYNTHETIC memory.
 *
 * WHY SYNTHETIC: the adapter's logic today is identity, honest speech,
 * the DumpState anchor scan, and the two verified title-flow structures
 * (title cursor + difficulty cursor, behind the submenu-pointer gate).
 * Feeding it a fake RAM image with the structures planted at their real
 * addresses tests exactly that logic — including the trap the live hunt
 * proved real: the difficulty cursor KEEPS its value after its screen
 * closes, so only the gate pointer may let it speak.
 *
 * ⛔ THE MOCK MUST MAP ABSOLUTE ADDRESSES (same rule as the MK test):
 * subtract the 0x08800000 base, mask with the power-of-two buffer size.
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

// ⛔ DECLARE THEM INSIDE THE NAMESPACE (the MK/DBZ tests document the
// link error the global-scope spelling produces).
namespace oga {
extern const Adapter kKingdomHeartsBirthBySleep;
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
static void put_u16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }
static void put_u32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void put_f32(uint32_t a, float v) { memcpy(&RAM[OFF(a)], &v, 4); }
static bool logged_has(const char* needle)
{
    for (int i = 0; i < NLOGGED; i++) if (strstr(LOGGED[i], needle)) return true;
    return false;
}

// The verified addresses (khbbs_adapter.cpp's map; hunts 4-5).
static const uint32_t TITLE_CURSOR = 0x09920D76;
static const uint32_t TITLE_COUNT  = 0x09920D7A;
static const uint32_t SUBMENU_PTR  = 0x09920D8C;
static const uint32_t DIFF_OBJ     = 0x09922EB0;
static const uint32_t DIFF_CURSOR  = 0x09922FA6;
static const uint32_t DIFF_COUNT   = 0x09922FAA;

int main()
{
    put_ascii(0x08910000, "NEW GAME");        // a real anchor, ASCII
    put_utf16(0x08930000, "Aqua");            // a real anchor, UTF-16LE
    put_ascii(0x08940000, "NEW GANE");        // ⛔ DECOY: one letter wrong

    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;

    int pass = 0, fail = 0;
    #define CHECK(c,m) do { if (c) { pass++; } else { fail++; printf("  FAIL: %s\n", m); } } while (0)

    const oga::Adapter* a = &oga::kKingdomHeartsBirthBySleep;

    // ---- identity -------------------------------------------------------------
    CHECK(strcmp(a->game_code, "ULUS10505") == 0, "game code ULUS10505");
    CHECK(oga::find_by_game_code("ULUS10505") == a, "registry resolves ULUS10505");
    CHECK(oga::find_by_game_code("ULUS10102") == &oga::kMortalKombatUnchained,
          "ULUS10102 still resolves to MK Unchained (no registry theft)");
    // Game-library wiring (docs/proposals/adapter-game-library.md):
    // the library list and game pages read coverage/notes off the
    // Adapter, so both launch titles must carry them.
    CHECK(a->coverage != nullptr && a->coverage[0] != '\0',
          "khbbs carries a library coverage line");
    CHECK(a->notes != nullptr && strstr(a->notes, "ULUS10505") != nullptr,
          "khbbs library notes name the supported version");
    const oga::Adapter* mku_a = oga::find_by_game_code("ULUS10102");
    CHECK(mku_a->notes != nullptr &&
              strstr(mku_a->notes, "QUIT GAME") != nullptr,
          "mku library notes state the unmapped quit dialog plainly");
    CHECK(oga::find_by_game_code("ULUS1050") == nullptr,
          "a truncated PSP id matches nothing");

    CHECK(a->attach(&H), "attach accepts the host");

    // ---- scaffold honesty (RAM zeroed: no tracked screen) ----------------------
    CHECK(!a->ready(), "not ready with no title-flow signature (RAM zeroed)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
          "WhereAmI states the scaffold scope instead of inventing a location");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not mapped yet"),
          "NextEnemy refuses honestly until battle state is located");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "menu commands stay silent on untracked screens");

    // ---- the anchor scan ---------------------------------------------------------
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(NLOGGED >= 4 && strstr(LOGGED[0], "anchor scan"),
          "DumpState logs the scan header");
    CHECK(logged_has("\"NEW GAME\" ascii @ 0x08910000"),
          "the scan finds the planted ASCII NEW GAME at its real address");
    CHECK(logged_has("\"Aqua\" utf16 @ 0x08930000"),
          "the scan finds the planted UTF-16LE Aqua");
    CHECK(!logged_has("0x08940000"),
          "the decoy NEW GANE produces no hit");
    CHECK(logged_has("scan done: 2 hits"),
          "the scan summary counts exactly the planted anchors");

    // ---- ⛔ THE BOOT-DIALOG TRAP (hunts 1/4, live-proven) -------------------------
    // On the boot dialogs the title cursor and gate pointer both read 0 —
    // exactly a title menu at NEW GAME — but the count field reads
    // garbage. The gate must refuse.
    put_u32(SUBMENU_PTR, 0);
    put_u16(TITLE_CURSOR, 0);
    put_u16(TITLE_COUNT, 2189);                     // the dialogs' real garbage
    CHECK(!a->ready(), "the boot-dialog state (count garbage) fails closed");

    // ---- the title menu (verified structure, synthetic plant) --------------------
    put_u16(TITLE_COUNT, 3);
    put_u16(TITLE_CURSOR, 1);                       // Continue
    CHECK(a->ready(), "ready on the title menu (gate pointer 0, cursor in range)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. Continue, 2 of 3"),
          "WhereAmI speaks the title row");

    put_u16(TITLE_CURSOR, 2);                       // Install Game
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Install Game, 3 of 3"),
          "MenuNext speaks the row landed on");

    put_u16(TITLE_CURSOR, 7);                       // out of range: corrupt plant
    CHECK(!a->ready(), "an out-of-range title cursor fails closed");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
          "a corrupt cursor speaks no row");

    // ---- the four-item title (fixture lineage: Trinity Archives) -----------------
    // With clear data present the title gains Trinity Archives between
    // Continue and Install Game; the count word names the table.
    put_u16(TITLE_COUNT, 4);
    put_u16(TITLE_CURSOR, 2);                       // Trinity Archives
    CHECK(a->ready(), "ready on the four-item title");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. Trinity Archives, 3 of 4"),
          "WhereAmI speaks the Trinity Archives row");
    put_u16(TITLE_CURSOR, 3);                       // Install Game stays last
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. Install Game, 4 of 4"),
          "Install Game is fourth on the long title");
    put_u16(TITLE_CURSOR, 4);                       // == count: out of range
    CHECK(!a->ready(), "a cursor at the four-item count fails closed");
    put_u16(TITLE_COUNT, 5);                        // no such title exists
    put_u16(TITLE_CURSOR, 0);
    CHECK(!a->ready(), "an unknown title count fails closed");
    put_u16(TITLE_COUNT, 3);                        // restore the fresh-profile plant

    // ---- the difficulty select ---------------------------------------------------
    put_u16(TITLE_CURSOR, 0);
    put_u32(SUBMENU_PTR, DIFF_OBJ);
    put_u16(DIFF_COUNT, 4);
    put_u16(DIFF_CURSOR, 2);                        // Proud Mode
    CHECK(a->ready(), "ready on the difficulty select (gate pointer set)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Select a game mode. Proud Mode, 3 of 4"),
          "WhereAmI speaks the difficulty row, not the title row beneath it");

    put_u16(DIFF_CURSOR, 0);                        // Beginner Mode
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Beginner Mode, 1 of 4"),
          "MenuPrev speaks the difficulty row landed on");

    // ---- ⛔ THE STALE-CURSOR TRAP (hunt5 x0->x1, live-proven) ---------------------
    // Backing out to the title leaves the difficulty cursor holding its
    // last value. With the gate pointer back at 0, the adapter must speak
    // the TITLE — the stale cursor must never resurrect the submenu.
    put_u32(SUBMENU_PTR, 0);
    put_u16(DIFF_CURSOR, 3);                        // stale: Critical Mode
    put_u16(TITLE_CURSOR, 0);                       // New Game
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "after backing out, the title speaks — the stale difficulty cursor stays silent");

    // ---- field state (verified 2026-10-18, decomp-led) -----------------------------
    // The actor plant goes in while the title plant above is STILL
    // valid (cursor 0, count 3, gate pointer 0): in the real game the
    // title structures can hold stale-but-valid values under the field,
    // so the actor's fingerprints must win. Addresses from the adapter
    // map: actor 0x09976090, cur HP +0x380, max HP +0x382, gauge
    // +0x226, mark +0x330, flags +0x234, deck cursor 0x099964EC.
    static const uint32_t ACTOR = 0x09976090;
    put_u16(ACTOR + 0x382, 35);                     // max HP
    put_u16(ACTOR + 0x380, 35);                     // current HP
    put_u16(ACTOR + 0x226, 27);                     // command gauge
    put_u16(ACTOR + 0x330, 1);                      // spawn-init mark
    put_u32(ACTOR + 0x234, 0x8068);                 // flags, bit 0x20 set
    put_u32(ACTOR + 0x220, 1);                      // actor state: in field
    put_u32(ACTOR + 0x58, 0x099816A0);              // live pointer field
    put_u32(0x099964EC, 1);                         // deck cursor: Aero
    CHECK(a->ready(), "ready in the field (actor fingerprints planted)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ventus, health 35 of 35. Command: Aero. Gauge 27."),
          "WhereAmI speaks the field line — the actor beats the stale title plant");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "a nav command that did not move the deck cursor stays silent (change-only)");

    put_u32(0x099964EC, 2);                         // Strike Raid
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Strike Raid, 3 of 3"),
          "a real deck cycle speaks the landed command");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuPrev);
    CHECK(NSPOKEN == 0, "an unchanged deck cursor stays silent on MenuPrev too");

    // The field gate fails closed: kill the max HP and the field line
    // must go — the (still valid) title plant speaks instead.
    put_u16(ACTOR + 0x382, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "with the actor gone, the field line fails closed to the title");

    // ---- per-boot actor location (fixture lineage values, live-proven) ------------
    // The actor base floats per boot; the adapter locates it by the
    // spawn fingerprints. Plant the fixture lineage's real find: base
    // 0x0996C250, HP 145/145, gauge 22 (the tutorial plant above stays
    // dead — its max HP is 0).
    static const uint32_t ACTOR2 = 0x0996C250;
    put_u16(ACTOR2 + 0x382, 145);
    put_u16(ACTOR2 + 0x380, 145);
    put_u16(ACTOR2 + 0x226, 22);
    put_u16(ACTOR2 + 0x330, 1);
    put_u32(ACTOR2 + 0x234, 0x8068);
    put_u32(ACTOR2 + 0x220, 1);
    put_u32(ACTOR2 + 0x58, 0x09974C50);
    // The save-side player block for this lineage (aligned base
    // 0x09F00000): munny/LV/max-HP mirror + character byte 1 = Terra.
    static const uint32_t BLOCK = 0x09F00000;
    put_u32(BLOCK + 0x287DC, 999999);
    put_u16(BLOCK + 0x287E4, 99);
    put_u16(BLOCK + 0x287E6, 145);
    put_u32(BLOCK + 0x287F4, 1u << 24);
    CHECK(a->ready(), "ready in the field with a relocated actor");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Terra, health 145 of 145. Gauge 22."),
          "a relocated actor speaks its character name, health and gauge");
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Command:"),
          "a relocated actor speaks no deck command (tutorial-only fact)");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "deck nav stays silent on a relocated actor");

    // ---- field location: world/room words (both copies must agree) ------
    RAM[OFF(0x08B61CA0)] = 6;  RAM[OFF(0x08B61CA1)] = 13;  // A: Radiant Garden / Central Square
    RAM[OFF(0x08B759C0)] = 6;  RAM[OFF(0x08B759C1)] = 13;  // B agrees
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Radiant Garden, Central Square. Terra, health 145 of 145."),
          "WhereAmI names the world and room when both location copies agree");
    RAM[OFF(0x08B759C1)] = 9;                             // copies disagree (mid-transition)
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Radiant Garden") &&
          strstr(SPOKEN[0], "Terra, health 145 of 145."),
          "disagreeing location copies: no location spoken, the field line stands");
    RAM[OFF(0x08B61CA1)] = 99; RAM[OFF(0x08B759C1)] = 99; // unknown room, known world
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Radiant Garden. Terra, health 145 of 145."),
          "an unnamed room speaks the world alone");
    RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
    RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;  // no-map sentinel
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Radiant Garden") &&
          strstr(SPOKEN[0], "Terra, health 145 of 145."),
          "the 0xFF sentinel speaks no location");
    RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
    RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;   // restore the zeroed plant

    // ---- player position: the engagement component ----------------------
    // Real hub values (fixture lineage): the component at
    // 0x099752D0, position (0, -8.81, -42.99) at the save point.
    static const uint32_t NODE = 0x099752D0;
    put_u32(NODE + 0x04, 0x08B73DD0);
    put_u32(NODE + 0x0C, 0x08B73DE0);
    put_u32(NODE + 0x1C, 0x08B73DB4);
    put_u32(NODE + 0x38, ACTOR2);
    put_f32(NODE + 0x40, 0.0f); put_f32(NODE + 0x44, -8.81f); put_f32(NODE + 0x48, -42.99f);
    // A tutorial-style look-alike: right owner word, no descriptors.
    static const uint32_t NODEFAKE = 0x0997683C;
    put_u32(NODEFAKE + 0x38, ACTOR2);
    put_f32(NODEFAKE + 0x40, 1.0f); put_f32(NODEFAKE + 0x44, 0.0f); put_f32(NODEFAKE + 0x48, 0.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Position 0, -43. Terra, health 145 of 145."),
          "WhereAmI speaks the position from the uniquely located component");
    put_u32(NODE + 0x1C, 0);   // break a descriptor word: no unique component
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Position") &&
          strstr(SPOKEN[0], "Terra, health 145 of 145."),
          "a broken descriptor silences the position, the field line stands");
    put_u32(NODE + 0x1C, 0x08B73DB4);  // restore the descriptor word
    put_u32(NODE + 0x0C, 0x08B73E10);  // the locked-on state descriptor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Position 0, -43. Terra, health 145 of 145."),
          "the locked-on state descriptor still locates the component");
    put_u32(NODE + 0x0C, 0x08B73DE0);  // restore; later sections re-plant their own actors

    // ---- movement-derived facing --------------------------------------
    // The tracker anchors on the planted position (0, -42.99); no leg
    // has completed, so DumpState reports no facing yet.
    a->on_frame();
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(!logged_has("facing ("), "no facing before the first tracked leg");
    // A 3-unit leg in +x: facing (1.00, 0.00).
    put_f32(NODE + 0x40, 3.0f);
    a->on_frame();
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(logged_has("facing (1.00, 0.00)"), "a +x leg tracks facing (1.00, 0.00)");
    // A sub-unit shuffle does not move the facing.
    put_f32(NODE + 0x40, 3.4f);
    a->on_frame();
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(logged_has("facing (1.00, 0.00)"), "a sub-unit shuffle leaves the facing alone");
    // Back within the anchor's reach (no leg), then a 4-unit leg in +z:
    // facing (0.00, 1.00).
    put_f32(NODE + 0x40, 3.0f);
    a->on_frame();
    put_f32(NODE + 0x48, -38.99f);
    a->on_frame();
    NLOGGED = 0;
    a->command(oga::Command::DumpState);
    CHECK(logged_has("facing (0.00, 1.00)"), "a +z leg re-tracks facing (0.00, 1.00)");
    put_f32(NODE + 0x40, 0.0f); put_f32(NODE + 0x48, -42.99f);  // restore the plant

    // A second fingerprint twin makes a COLD sweep ambiguous: the
    // field must fail closed rather than guess between two structs.
    // (A warm cache is different: the already-located actor keeps
    // validating, so detach first to force the sweep.)
    static const uint32_t ACTOR3 = 0x09910000;
    put_u16(ACTOR3 + 0x382, 60);
    put_u16(ACTOR3 + 0x380, 60);
    put_u16(ACTOR3 + 0x226, 0);
    put_u16(ACTOR3 + 0x330, 1);
    put_u32(ACTOR3 + 0x234, 0x8068);
    put_u32(ACTOR3 + 0x220, 1);
    put_u32(ACTOR3 + 0x58, 0x09974C50);
    a->detach();
    a->attach(&H);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "two fingerprint twins: the field fails closed to the title");
    // Clear both relocated plants; the camp section below runs its
    // own gates and must not meet a live actor on the way.
    put_u16(ACTOR2 + 0x330, 0);
    put_u16(ACTOR3 + 0x330, 0);

    // ---- Final Episode state values (live-proven 2026-10-18) --------
    // The FE field actor carries state 12, not 1..3; the fingerprint
    // must still locate it. Plant the real FE find: base 0x09980B10,
    // HP 89/149, gauge 30 (battle27, mid-fight values).
    static const uint32_t ACTOR4 = 0x09980B10;
    put_u16(ACTOR4 + 0x382, 149);
    put_u16(ACTOR4 + 0x380, 89);
    put_u16(ACTOR4 + 0x226, 30);
    put_u16(ACTOR4 + 0x330, 1);
    put_u32(ACTOR4 + 0x234, 0x8068);
    put_u32(ACTOR4 + 0x220, 12);
    put_u32(ACTOR4 + 0x58, 0x099816A0);
    // Repoint the player block at this actor: max-HP mirror 149,
    // character byte 2 = Aqua (the FE slot's real values).
    put_u32(BLOCK + 0x287DC, 32821);
    put_u16(BLOCK + 0x287E6, 149);
    put_u32(BLOCK + 0x287F4, 2u << 24);
    a->detach();
    a->attach(&H);
    CHECK(a->ready(), "ready in the field with an FE-state actor");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Aqua, health 89 of 149. Gauge 30."),
          "an FE-state actor speaks its character name, health and gauge");
    // While locked on, the spawn mark reads 2 instead of 1; the
    // actor must still validate (battle30/31: mark 2 in both locked
    // dumps, 1 again after unlocking).
    put_u16(ACTOR4 + 0x330, 2);
    a->detach();
    a->attach(&H);
    CHECK(a->ready(), "ready in the field with a locked-on actor (mark 2)");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Aqua, health 89 of 149. Gauge 30."),
          "a locked-on actor still speaks its name, health and gauge");
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on."),
          "the field line reports the held lock-on");
    // Lock acquisition is announced once, on the edge (on_frame poll).
    put_u16(ACTOR4 + 0x330, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->on_frame();
    CHECK(NSPOKEN == 0, "no lock announcement while unlocked");
    put_u16(ACTOR4 + 0x330, 2);
    a->on_frame();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on."),
          "acquiring a lock announces it");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->on_frame();
    CHECK(NSPOKEN == 0, "a held lock does not re-announce");
    // ---- locked foe readout (live-proven 2026-10-19, battle41) ----
    // The lock-on record and the foe's entity block at their real
    // FE addresses/values: engagement id 19, the player at
    // (-5.28, 1.11, -0.21), the target at (3.69, 1.16, 0.63) —
    // 9 units away — the foe at 1076 of 1200.
    static const uint32_t REC = 0x0998CA90, FOEBLK = 0x0998D9F8;
    put_u32(ACTOR4 + 0x33C, 19);
    put_u32(REC + 0x38, ACTOR4);
    put_u32(REC + 0x3C, FOEBLK + 0x1B8);
    put_u32(FOEBLK + 0x1B8, 19);
    put_u16(FOEBLK + 0x380, 1076);
    put_u16(FOEBLK + 0x382, 1200);
    put_f32(REC + 0x40, -5.28f); put_f32(REC + 0x44, 1.11f); put_f32(REC + 0x48, -0.21f);
    put_f32(REC + 0x50, 3.69f);  put_f32(REC + 0x54, 1.16f); put_f32(REC + 0x58, 0.63f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Locked on. Enemy health 1076 of 1200, 9 units away."),
          "a held lock speaks the foe's health and distance");
    put_u32(ACTOR4 + 0x33C, 0);  // engagement id gone: fail closed
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on.") &&
              !strstr(SPOKEN[0], "Enemy health"),
          "an unverifiable target keeps the plain lock suffix");
    // ---- foe / lock queries (Dissidia battle mapping, 2026-10-19) ----
    put_u32(ACTOR4 + 0x33C, 19);  // restore the engagement id
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on. 9 away."),
          "NextEnemy speaks the lock state and distance");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Enemy health 1076 of 1200. 9 away."),
          "NextAlly speaks the foe's health and distance");
    put_f32(REC + 0x54, 12.16f);  // target hovers well above the player
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], ", above you."),
          "the foe query reports a target above the player");
    put_f32(REC + 0x54, 1.16f);
    put_u32(ACTOR4 + 0x33C, 0);  // unverifiable target again
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Lock lost."),
          "a locked but unverifiable target reads as a lost lock");
    put_u16(ACTOR4 + 0x330, 1);  // unlocked
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Lock off."),
          "NextEnemy reports the lock off");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not locked on."),
          "the foe query refuses honestly while unlocked");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Aqua, health 89 of 149."),
          "PrevEnemy falls back to the self readout in the field");
    // ---- cue snapshot: the beacon's data feed (2026-10-19) ----
    oga::CueSnapshot snap;
    snap.battle = false; snap.locked = true; snap.is_core = true;
    snap.dist = 99.0f; snap.core_gain = true;   // poison every field
    CHECK(a->cue_snapshot(&snap) && snap.battle && !snap.locked &&
              !snap.is_core && !snap.core_gain && snap.dist == 0.0f,
          "unlocked in the field: battle snapshot, no lock, fields clean");
    put_u16(ACTOR4 + 0x330, 2);
    put_u32(ACTOR4 + 0x33C, 19);
    CHECK(a->cue_snapshot(&snap) && snap.battle && snap.locked &&
              !snap.is_core && snap.dist > 8.9f && snap.dist < 9.1f,
          "a verified lock feeds the beacon its distance");
    put_u32(ACTOR4 + 0x33C, 0);  // target no longer verifiable
    CHECK(a->cue_snapshot(&snap) && snap.battle && !snap.locked,
          "an unverifiable target stays silent, never a stale distance");
    put_u32(ACTOR4 + 0x33C, 19);
    put_u16(ACTOR4 + 0x330, 0);  // actor plant retired: no field
    snap.locked = true; snap.dist = 99.0f;
    CHECK(!a->cue_snapshot(&snap) && !snap.locked && snap.dist == 0.0f,
          "off the field the snapshot refuses and clears");
    put_u16(BLOCK + 0x287E6, 0);  // retire the player-block plant

    // ---- camp menu (verified 2026-10-18, fixture lineage) ---------------------------
    // Camp plant: cursor u16 @ 0x09919656, count u16 @ +4 == 9, active
    // witnesses u32 @ 0x08B5BEF8 == 8 and u32 @ 0x08BD194C == 2. The
    // title plant above is still valid underneath — the camp menu sits
    // on top of everything, so it must win.
    put_u16(0x0991965A, 9);                         // camp item count
    put_u16(0x09919656, 3);                         // camp cursor: D-Links
    put_u32(0x08B5BEF8, 8);                         // active witness A
    put_u32(0x08BD194C, 2);                         // active witness B
    CHECK(a->ready(), "ready on the camp menu (witnesses + count planted)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. D-Links, 4 of 9"),
          "WhereAmI speaks the camp row — the menu beats the stale title plant");

    put_u16(0x09919656, 4);                         // Stats
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Stats, 5 of 9"),
          "a camp nav command speaks the landed row");

    // The cursor persists stale after close; killing a witness must
    // close the gate even though cursor and count still read fine.
    put_u32(0x08B5BEF8, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "with a camp witness gone, the stale camp cursor stays silent");

    // ---- camp sub-screens (verified 2026-10-18, hunts camp7-14) ---------------------
    // The screen tracker: the count leaves 9 on a dive; exactly one
    // cursor moves per tap and names the screen. Drive the decks
    // tree the way the game does.
    put_u32(0x08B5BEF8, 8);                         // witness A back on
    put_u16(0x09919656, 0);                         // main cursor: Command Decks
    put_u16(0x0991965A, 0);                         // count leaves 9: dived
    put_u16(0x09919716, 0);                         // decks submenu: Edit Deck
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Decks menu. Edit Deck, 1 of 5"),
          "a dive under Command Decks infers the decks submenu");

    put_u16(0x09919716, 2);                         // Command List
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command List, 3 of 5"),
          "a submenu cursor move speaks the landed submenu row");

    put_u16(0x09919694, 1);                         // editor group: Action Commands
    put_u16(0x099251D6, 2361);                      // slot word: off-screen garbage (live value)
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Action Commands"),
          "a group-focus move promotes to the Edit Deck screen");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Edit Deck. Action Commands"),
          "WhereAmI names the editor and its focused group");

    put_u16(0x099251DA, 8);                         // slot count
    put_u16(0x099251D6, 5);                         // slot cursor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 6 of 8"),
          "a slot cursor move promotes to the slot list");

    put_u16(0x099252A8, 3);                         // inventory cursor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Row 4"),
          "an inventory cursor move promotes to the command list");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command list. Row 4"),
          "WhereAmI names the command list and its row");

    // ---- camp inventory contents (verified 2026-10-19, fixture lineage) --------
    // Pool: 10-byte instance records at *0x08B75E70 {u16 def id, u8
    // level/count, u8 zero, ..., u16 ability @ +6}. Def records at
    // 0x08B34924 + id*16: family @ +0 (2 = deck), sub-type @ +1
    // (3 = item), name pointer @ +8. The list is the family-2
    // instances sorted by def id, pool order breaking ties.
    {
        const uint32_t POOL = 0x09F262B8, NAMES = 0x09F30000;
        auto def = [&](uint16_t id, uint8_t fam, uint8_t sub,
                       const char* name, uint32_t strAt) {
            put_u16(0x08B34924 + (uint32_t) id * 16,
                    (uint16_t)(fam | (sub << 8)));
            put_u32(0x08B34924 + (uint32_t) id * 16 + 8, strAt);
            put_ascii(strAt, name);
        };
        auto inst = [&](int slot, uint16_t id, uint8_t level,
                        uint16_t ability) {
            uint32_t a = POOL + (uint32_t) slot * 10;
            put_u16(a, id);
            put_u16(a + 2, level);       // level byte + the zero byte
            put_u16(a + 6, ability);
        };
        def(91, 2, 1, "Quick Blitz", NAMES);
        def(94, 2, 1, "Meteor Crash", NAMES + 0x20);
        def(96, 2, 1, "Fire Dash", NAMES + 0x40);
        def(188, 2, 3, "Potion", NAMES + 0x60);
        def(260, 3, 5, "Block", NAMES + 0x80);       // action: excluded
        def(440, 0, 0, "Combo Plus", NAMES + 0xA0);  // ability name only
        inst(0, 260, 3, 0);        // Block — not a deck command
        inst(1, 96, 4, 0);         // Fire Dash, plain
        inst(2, 91, 3, 0);         // Quick Blitz
        inst(3, 0, 7, 0);          // freed slot: id zeroed, rest stale
        inst(4, 94, 5, 440);       // Meteor Crash with Combo Plus
        inst(5, 188, 60, 0);       // Potion x60 (item: count, not level)
        inst(6, 96, 4, 440);       // Fire Dash with Combo Plus
        put_u32(0x08B75E70, POOL);                   // work block pointer
        // Sorted list: QB3, MC5+CP, FD4, FD4+CP, Potion 60.
        put_u16(0x099252A8, 3);                      // second Fire Dash
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Command list. Fire Dash, level 4, with Combo Plus, 4 of 5"),
              "inventory speech names the command, level and ability");

        put_u16(0x099252A8, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Quick Blitz, level 3, 1 of 5"),
              "a nav move speaks the landed command, not the row number");

        put_u16(0x099252A8, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Command list. Potion, 60 in stock, 5 of 5"),
              "items speak their count, not a level");

        put_u16(0x099252A4, 3);                      // Items tab
        put_u16(0x099252A8, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Command list. Potion, 60 in stock, 1 of 1"),
              "the Items tab filters the spoken list to items only");

        put_u16(0x099252A4, 1);                      // Battle tab
        put_u16(0x099252A8, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Command list. Fire Dash, level 4, 3 of 4"),
              "the Battle tab filters the spoken list to battle commands");

        put_u16(0x099252A4, 9);                      // unknown tab
        put_u16(0x099252A8, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command list. Row 1"),
              "an unknown tab value falls back to the row number");
        put_u16(0x099252A4, 0);

        put_u32(0x08B75E70, 0);                      // pool unavailable
        put_u16(0x099252A8, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command list. Row 1"),
              "with no pool, inventory speech falls back to the row number");
    }

    put_u16(0x0991965A, 9);                         // backed out to the main list
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Decks, 1 of 9"),
          "returning to the main list speaks the main row again");

    // ---- detach ---------------------------------------------------------------------
    a->detach();
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 0, "after detach the adapter is silent");

    printf("  khbbs adapter: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

// Stubs so adapters.o links without dragging in the FE/GBA machinery (the
// same arrangement the other adapter tests use).
namespace oga {
static bool s_att(const Host*) { return false; }
static void s_frm() {}
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11","FE","YFEE",s_att,s_frm,s_cmd,s_rdy,s_det};
const Adapter kGameBoyAdvance        = {"gba","GBA","",    s_att,s_frm,s_cmd,s_rdy,s_det};
}
