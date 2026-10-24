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
static void put_u8(uint32_t a, uint8_t v) { RAM[OFF(a)] = v; }
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
// The positive title witnesses (2026-10-22): flow phase, title step,
// fade word — see khbbs_adapter.cpp's header note.
static const uint32_t TITLE_PHASE  = 0x09920CD0;
static const uint32_t TITLE_STEP   = 0x09920D98;
static const uint32_t TITLE_FADE   = 0x09920F44;
static const uint32_t DIFF_OBJ     = 0x09922EB0;
static const uint32_t DIFF_CURSOR  = 0x09922FA6;
static const uint32_t DIFF_COUNT   = 0x09922FAA;
// The object's system-data placement (khbbs-diffgate, 2026-10-22):
// base 0x09922ED0, cursor/count at the same +0xF6/+0xFA offsets.
static const uint32_t DIFF_OBJ_HI    = 0x09922ED0;
static const uint32_t DIFF_CURSOR_HI = DIFF_OBJ_HI + 0xF6;
static const uint32_t DIFF_COUNT_HI  = DIFF_OBJ_HI + 0xFA;

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
    // Game-library data (docs/proposals/adapter-game-library.md): the
    // proposed library list and game pages will read coverage/notes off the
    // Adapter. Until that UI lands, this checks the data is present and
    // honest for the two PSP launch titles.
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

    // At every title-era screen both map-id words read the no-map
    // sentinel (0xFF) — no world is loaded — and the title gate's
    // no-map guard reads them, so plant the sentinel for the whole
    // title era; the field sections below overwrite it.
    RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
    RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;

    // ---- ⛔ THE BOOT-DIALOG TRAP (hunts 1/4, live-proven) -------------------------
    // On the boot dialogs the title cursor and gate pointer both read 0 —
    // exactly a title menu at NEW GAME — but the count field reads
    // garbage. The gate must refuse.
    put_u32(SUBMENU_PTR, 0);
    put_u16(TITLE_CURSOR, 0);
    put_u16(TITLE_COUNT, 2189);                     // the dialogs' real garbage
    CHECK(!a->ready(), "the boot-dialog state (count garbage) fails closed");

    // ---- ⛔ THE EARLY-TITLE TRAP (app-observed 2026-10-21, fixed 2026-10-22) ------
    // TITLE.ELF writes the title count and cursor when the title struct
    // is built — before the boot logos finish. The old gate (pointer 0
    // + count + cursor) therefore fired during the Disney logo fade
    // and stayed fired through the opening movie and the title's own
    // fade-in, while the D-pad did nothing. Bench proof over six cold
    // boots (khbbs-titlegate): the gate needs the positive witnesses —
    // flow phase 3 (title's own phase; 1 = logos, 2 = movie), title
    // step 4 (menu idle; 3 = still fading in), fade word 0 (settled).
    // Each fake phase below plants the full old-gate state.
    put_u32(SUBMENU_PTR, 0);
    put_u16(TITLE_CURSOR, 0);
    put_u16(TITLE_COUNT, 3);
    put_u32(TITLE_PHASE, 1);                        // boot logos
    put_u8(TITLE_STEP, 1);
    CHECK(!a->ready(), "the logo phase (title values pre-written) fails closed");
    put_u32(TITLE_PHASE, 2);                        // opening movie
    put_u8(TITLE_STEP, 2);
    CHECK(!a->ready(), "the opening movie fails closed");
    put_u32(TITLE_PHASE, 3);                        // title screen fading in
    put_u8(TITLE_STEP, 3);
    put_f32(TITLE_FADE, 61.5f);                     // fade word mid-ramp
    CHECK(!a->ready(), "the title fade-in (input still locked) fails closed");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
          "the fade-in speaks no title row");
    put_u8(TITLE_STEP, 4);                          // idle step, fade unsettled
    CHECK(!a->ready(), "an unsettled fade word fails closed");
    put_f32(TITLE_FADE, 0.0f);                      // settled: the real title

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
    put_u32(DIFF_OBJ, 0xFFFFFFFF);                  // the object's identity words
    put_u32(DIFF_OBJ + 0x0C, 0x08B90160);
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

    // ---- ⛔ THE MOVED DIFFICULTY OBJECT (cold boots, 2026-10-22) -------------------
    // On a system-data profile (title count 4) the title leaves one
    // extra 0x20 heap block ahead of the difficulty object, which
    // lands at 0x09922ED0; the old gate's fixed addresses read 0
    // there and the screen went unspoken (khbbs-diffgate, three
    // boots per lineage, cursor walks screenshot-verified). The gate
    // follows the submenu pointer and verifies the object's header
    // words — the same screen must speak at the shifted base.
    put_u16(TITLE_COUNT, 4);                        // the system-data profile
    put_u16(DIFF_CURSOR, 0);                        // the old base reads 0
    put_u16(DIFF_COUNT, 0);
    put_u32(SUBMENU_PTR, DIFF_OBJ_HI);
    put_u32(DIFF_OBJ_HI, 0xFFFFFFFF);
    put_u32(DIFF_OBJ_HI + 0x0C, 0x08B90160);
    put_u16(DIFF_COUNT_HI, 4);
    put_u16(DIFF_CURSOR_HI, 2);                     // Proud Mode
    CHECK(a->ready(), "ready on the difficulty select at the shifted base (system-data lineage)");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Select a game mode. Proud Mode, 3 of 4"),
          "WhereAmI speaks the difficulty row from the shifted object");

    put_u16(DIFF_CURSOR_HI, 3);                     // Critical Mode
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Critical Mode, 4 of 4"),
          "MenuNext speaks the difficulty row landed on at the shifted base");

    // The confirm dialog ("Play the game in ... Mode?") keeps the
    // pointer and the identity but drops the count word to 2 — its
    // own option count. The gate closes there, as it did on the
    // mapped lineage (live-proven on both, khbbs-diffgate).
    put_u16(DIFF_COUNT_HI, 2);
    CHECK(!a->ready(), "the confirm dialog (count word 2) fails closed at the shifted base");
    put_u16(DIFF_COUNT_HI, 4);

    // ---- the impostor object -----------------------------------------------------
    // A submenu pointer at some other heap block — even one whose
    // +0xF6/+0xFA words happen to look like a cursor and a count —
    // is not the difficulty select. Without the header identity
    // words the gate must refuse; with them, the address itself
    // no longer matters.
    static const uint32_t IMPOSTOR = 0x09923000;
    put_u32(SUBMENU_PTR, IMPOSTOR);
    put_u16(IMPOSTOR + 0xFA, 4);
    put_u16(IMPOSTOR + 0xF6, 1);
    CHECK(!a->ready(), "a pointer at an unidentified block fails closed");
    put_u32(IMPOSTOR, 0xFFFFFFFF);                  // half the identity
    CHECK(!a->ready(), "half an identity is no identity");
    put_u32(IMPOSTOR + 0x0C, 0x08B90160);           // the full identity
    CHECK(a->ready(), "the identity words name the object, not its address");
    put_u32(SUBMENU_PTR, 0);
    put_u16(TITLE_COUNT, 3);                        // restore the fresh-profile plant

    // ---- field state (verified 2026-10-18, decomp-led) -----------------------------
    // The actor plant goes in while the title plant above is STILL
    // valid (cursor 0, count 3, gate pointer 0): in the real game the
    // title structures can hold stale-but-valid values under the field,
    // so the actor's fingerprints must win. Addresses from the adapter
    // map: actor 0x09976090, cur HP +0x380, max HP +0x382, gauge
    // +0x226, mark +0x330, flags +0x234, deck cursor 0x099964EC.
    // A field means a world is loaded: clear the no-map sentinel so
    // the actor sweep runs (it is skipped while both words read 0xFF).
    RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
    RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;
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

    // The first deck nav with no prior read must stay silent: nothing has
    // been seen to move, so the adapter cannot claim a landed command.
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "the first deck nav with no prior read stays silent");

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
    // must go — the (still valid) title plant speaks instead. The
    // no-map sentinel returns with the actor gone: this fallback is
    // the title-era state, where no world is loaded.
    RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
    RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;
    put_u16(ACTOR + 0x382, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "with the actor gone, the field line fails closed to the title");
    RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
    RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;   // a world is loaded again below

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
    // With a world loaded, the ambiguous field fails closed AND the
    // stale title plant stays silent behind it: the title gate's
    // no-map guard refuses while a world is loaded (2026-10-24 —
    // the guard is what lets the title gate accept the flow
    // counter's later odd phases without aliasing in the field).
    RAM[OFF(0x08B61CA0)] = 6;  RAM[OFF(0x08B61CA1)] = 13;
    RAM[OFF(0x08B759C0)] = 6;  RAM[OFF(0x08B759C1)] = 13;
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
          "two fingerprint twins with a world loaded: the field fails closed and the stale title stays silent");
    // With no world loaded, the same ambiguity leaves the title —
    // the screen that legitimately owns a no-world state — speaking.
    RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
    RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 3"),
          "two fingerprint twins: the field fails closed to the title");
    RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
    RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;   // restore the zeroed plant
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

    // ---- low-health warning (2026-10-24; the game's own line) ----------------
    // The trigger fraction is the game's, not an adapter invention:
    // FUN_089327e8 boosts the HP gauge's presentation exactly when
    // (cur * 100) / max < 26, and the khbbs-lowhp1 bench watched its
    // output byte (actor +0x5F1) flip between cur 39 and 38 at max
    // 149 — the same boundary. Max 200 here makes the test
    // arithmetic round: 52 = 26% (not low), 51 = 25% (low), the
    // re-arm line at 31%: 61 = 30% (still latched), 62 = 31%.
    const uint32_t ACTOR5 = 0x09950000;
    put_u16(ACTOR5 + 0x380, 200);      // cur HP
    put_u16(ACTOR5 + 0x382, 200);      // max HP
    put_u16(ACTOR5 + 0x226, 0);        // gauge
    put_u16(ACTOR5 + 0x330, 1);        // mark (not locked on)
    put_u32(ACTOR5 + 0x234, 0x8068);   // flags (bit 0x20 set)
    put_u32(ACTOR5 + 0x220, 1);        // actor state
    put_u32(ACTOR5 + 0x58, 0x099816A0);// command cluster pointer
    auto lowhp_frames = [&]() { for (int i = 0; i < 4; i++) a->on_frame(); };

    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: full health stays silent");

    put_u16(ACTOR5 + 0x380, 52);       // exactly 26%: the game's line is < 26
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: at 26 percent the warning stays silent");

    put_u16(ACTOR5 + 0x380, 51);       // 25%: below the game's line
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Low health."),
          "low HP: crossing below the line speaks Low health once");

    put_u16(ACTOR5 + 0x380, 30);       // deeper damage, still low
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: more damage while low does not repeat it");

    put_u16(ACTOR5 + 0x380, 61);       // healed to 30%: above the game
    lowhp_frames();                    // line, below the re-arm line
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: a shallow heal does not re-arm the warning");

    put_u16(ACTOR5 + 0x380, 62);       // healed to 31%: re-armed
    lowhp_frames();
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Low health."),
          "low HP: healed clear and dropped again, it speaks again");

    put_u16(ACTOR5 + 0x380, 0);        // death: the game-over flow's
    NSPOKEN = 0; oga::AdapterSpeechReset();   // territory, and HP 0
    lowhp_frames();                    // fails the field gate itself
    CHECK(NSPOKEN == 0, "low HP: death never speaks the warning");
    put_u16(ACTOR5 + 0x380, 200);      // back at full health (a load)
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: recovering to full stays silent");
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Low health."),
          "low HP: after death and recovery, the next drop speaks");

    // The camp menu sits on the field with live HP words: a drop
    // first seen under it must not speak there, and must not fire
    // late when the menu closes (the latch takes it silently).
    put_u16(ACTOR5 + 0x380, 200);
    lowhp_frames();                    // armed at full health
    put_u32(0x08B5BEF8, 8);            // camp witnesses up
    put_u32(0x08BD194C, 2);
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: a drop under the camp menu stays silent");
    put_u32(0x08B5BEF8, 0);            // camp closes, still at 51
    put_u32(0x08BD194C, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: closing camp does not fire it late");
    put_u16(ACTOR5 + 0x380, 200);      // heal, then drop in the field
    lowhp_frames();
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Low health."),
          "low HP: healed and dropped in the field, it speaks");
    put_u16(ACTOR5 + 0x380, 0);        // retire the low-HP plant
    put_u16(ACTOR5 + 0x382, 0);
    put_u16(ACTOR5 + 0x330, 0);
    lowhp_frames();

    // ---- camp menu (verified 2026-10-18, fixture lineage) ---------------------------
    // Camp plant: the screen objects at their epoch-1 addresses,
    // reached through the game's per-session screen registry at
    // 0x09919264 (slot 3 = main object, slot 6 = decks object) —
    // the objects relocate on later visits (relocation audit
    // 2026-10-22), the registry does not. Object layout: head
    // 0xFFFFFFFF @ +0x00, class tag @ +0x0C (main 0x08BAD400,
    // decks 0x08BA59D0, pane 0x08BA29E0); main cursor @ +0x66,
    // count @ +0x6A; decks cursor @ +0x126, group focus @ +0xA4,
    // pane pointer @ +0x9C; the pane's slot widget pointer @ +0x84;
    // widget slot cursor @ +0x66, count @ +0x6A, tab @ +0x134,
    // inventory cursor @ +0x138. Active witnesses u32 @ 0x08B5BEF8
    // == 8 and u32 @ 0x08BD194C == 2. The title plant above is
    // still valid underneath — the camp menu sits on top of
    // everything, so it must win.
    // This section models the menus over title-era state (no world
    // loaded), so the map words carry the no-map sentinel again.
    RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
    RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;
    const uint32_t CAMPOBJ = 0x099195F0, CAMPPANE = 0x09928250,
                   CAMPWIDGET = 0x09925170;
    put_u32(0x09919264 + 12, CAMPOBJ);              // registry slot 3: main object
    put_u32(CAMPOBJ, 0xFFFFFFFF);                   // object head
    put_u32(CAMPOBJ + 0x0C, 0x08BAD400);            // main class tag
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
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Decks, 1 of 9"),
          "camp reopens on the main list after the witness returns");
    put_u32(0x09919264 + 12, 0);                    // main object unregistered: dived
    put_u32(0x09919264 + 24, CAMPOBJ);              // registry slot 6: decks object
    put_u32(CAMPOBJ + 0x0C, 0x08BA59D0);            // decks class tag
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

    // The editor opens: its pane object is built (the slot widget
    // is not — the slot word is unreachable until the list opens).
    put_u32(CAMPOBJ + 0x9C, CAMPPANE);              // pane pointer
    put_u32(CAMPPANE, 0xFFFFFFFF);                  // pane head
    put_u32(CAMPPANE + 0x0C, 0x08BA29E0);           // pane class tag
    put_u32(CAMPPANE + 0x84, 0);                    // slot widget not built yet
    put_u16(0x09919694, 1);                         // editor group: Action Commands
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Action Commands"),
          "a group-focus move promotes to the Edit Deck screen");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Edit Deck. Action Commands"),
          "WhereAmI names the editor and its focused group");

    put_u32(CAMPPANE + 0x84, CAMPWIDGET);           // slot widget built
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

    // ---- camp sub-screen churn trap (cold-boot hunt, 2026-10-22) ---------------
    // On cold boots the decks-submenu word does NOT freeze under the
    // slot list: opening the list rewrites it with heap churn, and a
    // plausible churn value (1) lands in the same frame the slot
    // word arrives (khbbs-campgate cg-std1: the adapter demoted the
    // screen to the decks submenu and spoke "Meld Commands, 2 of 5"
    // through the whole slot list). Arrival must outrank the stale
    // sub move. Replay that frame: reset the tracker by closing
    // camp, dive again to the editor, then churn + arrival at once.
    put_u32(0x08B5BEF8, 0);                         // camp closed: tracker resets
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    put_u32(0x08B5BEF8, 8);                         // camp open again, on the main list
    put_u32(0x09919264 + 24, 0);                    // decks object unregistered
    put_u32(0x09919264 + 12, CAMPOBJ);              // main object registered again
    put_u32(CAMPOBJ + 0x0C, 0x08BAD400);            // main class tag again
    put_u16(0x0991965A, 9);                         // count word: 9 again
    put_u16(0x09919656, 0);                         // main cursor: Command Decks
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Decks, 1 of 9"),
          "the second camp open also starts on the main list");
    put_u32(0x09919264 + 12, 0);                    // dived again
    put_u32(0x09919264 + 24, CAMPOBJ);              // decks object again
    put_u32(CAMPOBJ + 0x0C, 0x08BA59D0);            // decks class tag again
    put_u32(CAMPPANE + 0x84, 0);                    // slot widget unbuilt again
    put_u16(0x09919716, 0);                         // decks submenu: Edit Deck
    put_u16(0x09919694, 0);                         // group focus: Battle Commands
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Decks menu. Edit Deck, 1 of 5"),
          "the re-dive infers the decks submenu again");

    put_u16(0x09919694, 1);                         // editor group: Action Commands
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Action Commands"),
          "a group-focus move promotes to the Edit Deck screen again");

    put_u16(0x09919716, 1);                         // sub word: heap churn (cg-std1 value)
    put_u32(CAMPPANE + 0x84, CAMPWIDGET);           // slot widget built
    put_u16(0x099251DA, 8);                         // slot count
    put_u16(0x099251D6, 0);                         // slot word arrives: cursor 0
    put_u16(0x099252A8, 0);                         // inventory word holds 0 under the list
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 1 of 8"),
          "slot arrival outranks the churned sub word — no demotion to Meld Commands");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 1 of 8"),
          "WhereAmI names the slot list after the churned arrival");

    put_u16(0x099251D6, 1);                         // slot cursor moves
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 2 of 8"),
          "a slot move after the churned arrival still speaks the slot row");

    // Delayed churn: the sub word can also be rewritten a few
    // frames AFTER the arrival frame, while the slot list is
    // already the tracked screen (frame trace cg-p4b: churn landed
    // 4 frames after arrival; cg-std3/cg-std4 demoted from the
    // list's first row). A sub move there must not demote either —
    // the slot list has no direct exit to the submenu.
    put_u16(0x09919716, 2);                         // sub word: delayed heap churn
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 2 of 8"),
          "delayed sub churn under the slot list does not demote the screen");

    put_u16(0x099252A8, 5);                         // inventory cursor moves
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Row 6"),
          "an inventory move still promotes from the slot list after churn");

    // The same churn under the inventory must not demote either:
    // leaving the inventory goes through the slot list or the
    // editor, never straight to the submenu.
    put_u16(0x09919716, 0);                         // sub word churns again
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Row 6"),
          "sub churn under the command list does not demote the screen");

    put_u16(0x099251D6, 2);                         // slot cursor moves: back out
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 3 of 8"),
          "a slot move returns from the command list to the slot list");

    // Under the slot list the inventory word settles at 0 (its
    // live off-screen value); that is a re-seed, not a move, and
    // must not re-promote — the contents section below relies on
    // the tracker sitting in the slot list with the word seeded.
    put_u16(0x099252A8, 0);                         // inventory word settles
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 3 of 8"),
          "the inventory word settling at 0 does not re-promote");

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

    put_u32(0x09919264 + 24, 0);                    // decks object unregistered
    put_u32(0x09919264 + 12, CAMPOBJ);              // main object registered again
    put_u32(CAMPOBJ + 0x0C, 0x08BAD400);            // main class tag again
    put_u16(0x0991965A, 9);                         // backed out to the main list
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Decks, 1 of 9"),
          "returning to the main list speaks the main row again");

    // ---- camp sub-screens beyond decks (surveyed 2026-10-24, khbbs-scan
    // sub1/sub2/sub3) ---------------------------------------------------
    // Slot 6 names the current sub-screen object by class tag; each
    // screen's cursor sits at +0x66, inline (Stats, Game Help) or on
    // a child widget (Items +0x260, Abilities +0x7C, D-Links +0x220,
    // Config +0x7C). Counts ride at the widget's +0x6C (Config's is
    // per-tab: 9 General, 6 Multiplayer). Abilities rows are 12-byte
    // records at the widget's +0x80 (def id @ +0, installed @ +6);
    // D-Links rows are 20-byte records at the object's +0x80.
    // Reports kills the camp witnesses and is located through the
    // static pointer cell 0x0991C214 (cursor @ +0x66, count @ +0x6A).
    {
        const uint32_t SUBOBJ = 0x09924000, SUBWID = 0x09924800,
                       REPOBJ = 0x09925000, SUBNAMES = 0x09F31000;
        auto subdef = [&](uint16_t id, uint8_t fam, uint8_t sub,
                          const char* name, uint32_t strAt) {
            put_u16(0x08B34924 + (uint32_t) id * 16,
                    (uint16_t)(fam | (sub << 8)));
            put_u32(0x08B34924 + (uint32_t) id * 16 + 8, strAt);
            put_ascii(strAt, name);
        };
        auto dive = [&](uint32_t tag) {
            put_u32(0x09919264 + 12, 0);            // main unregistered
            put_u32(0x09919264 + 24, SUBOBJ);       // slot 6: the screen
            put_u32(SUBOBJ, 0xFFFFFFFF);            // object head
            put_u32(SUBOBJ + 0x0C, tag);            // class tag
        };
        // Items: fixed two-row category list on the child widget.
        dive(0x08B946C0);
        put_u32(SUBOBJ + 0x260, SUBWID);
        put_u32(SUBWID, 0xFFFFFFFF);
        put_u32(SUBWID + 0x0C, 0x08B94790);
        put_u16(SUBWID + 0x66, 0);                  // Equip
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Items. Equip, 1 of 2"),
              "the Items screen speaks its category row");
        put_u16(SUBWID + 0x66, 1);                  // Stock
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Stock, 2 of 2"),
              "a nav move speaks the landed Items category");
        // Abilities: named rows from the widget's record array.
        subdef(425, 9, 16, "Treasure Magnet", SUBNAMES);
        subdef(429, 9, 11, "HP Boost", SUBNAMES + 0x20);
        dive(0x08B8F720);
        put_u32(SUBOBJ + 0x7C, SUBWID);
        put_u32(SUBWID + 0x0C, 0x08B8F7F0);
        put_u16(SUBWID + 0x6C, 2);                  // row count
        put_u16(SUBWID + 0x80, 425);                // record 0: def id
        put_u16(SUBWID + 0x80 + 6, 5);              // record 0: installed
        put_u16(SUBWID + 0x80 + 12, 429);           // record 1
        put_u16(SUBWID + 0x80 + 12 + 6, 3);
        put_u16(SUBWID + 0x66, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Abilities. Treasure Magnet, 5 installed, 1 of 2"),
              "the Abilities screen names the row and its installs");
        put_u16(SUBWID + 0x66, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "HP Boost, 3 installed, 2 of 2"),
              "a nav move speaks the landed ability");
        // D-Links: named rows from the object's record array.
        subdef(383, 7, 14, "Ventus", SUBNAMES + 0x40);
        subdef(382, 7, 14, "Aqua", SUBNAMES + 0x60);
        dive(0x08BB2960);
        put_u32(SUBOBJ + 0x220, SUBWID);
        put_u32(SUBWID + 0x0C, 0x08BB2A30);
        put_u16(SUBWID + 0x6C, 2);
        put_u16(SUBOBJ + 0x80, 383);                // record 0 (20-byte)
        put_u16(SUBOBJ + 0x80 + 20, 382);           // record 1
        put_u16(SUBWID + 0x66, 1);                  // Aqua
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Links. Aqua, 2 of 2"),
              "the D-Links screen names the highlighted partner");
        put_u16(SUBWID + 0x66, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ventus, 1 of 2"),
              "a nav move speaks the landed D-Link");
        // Stats: fixed categories, cursor inline on the object.
        dive(0x08BA8B00);
        put_u16(SUBOBJ + 0x66, 1);                  // Command Styles
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Stats. Command Styles, 2 of 3"),
              "the Stats screen speaks its category row");
        put_u16(SUBOBJ + 0x66, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Finish Commands, 3 of 3"),
              "a nav move speaks the landed Stats category");
        // Config: the count word picks the tab's row table.
        dive(0x08B92910);
        put_u32(SUBOBJ + 0x7C, SUBWID);
        put_u32(SUBWID + 0x0C, 0x08B929E0);
        put_u16(SUBWID + 0x6C, 9);                  // General tab
        put_u16(SUBWID + 0x66, 4);                  // Maps
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Config. Maps, 5 of 9"),
              "the Config screen speaks its General row");
        put_u16(SUBWID + 0x6C, 6);                  // Multiplayer tab
        put_u16(SUBWID + 0x66, 1);                  // Armor Color
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Armor Color, 2 of 6"),
              "the Multiplayer tab speaks its own row table");
        // Game Help: fixed topics, cursor inline.
        dive(0x08BB4CD8);
        put_u16(SUBOBJ + 0x66, 3);                  // Adaptive Combat
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Game Help. Adaptive Combat, 4 of 6"),
              "the Game Help screen speaks its topic row");
        // A dived screen whose widget does not resolve falls back
        // to the camp item's name, the pre-hunt behaviour.
        put_u32(0x09919264 + 12, CAMPOBJ);          // main back up
        put_u16(0x0991965A, 9);
        put_u16(0x09919656, 1);                     // main cursor: Items
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);         // tracker learns main=1
        dive(0x08B946C0);
        put_u32(SUBOBJ + 0x260, 0);                 // widget pointer broken
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Items") &&
              !strstr(SPOKEN[0], "of 2"),
              "an unresolvable sub-screen falls back to the item name");
        // Reports: camp witnesses dead; the static cell names it,
        // and it must beat the stale title plant underneath.
        put_u32(0x08B5BEF8, 0);                     // witness A dead
        put_u32(0x0991C214, REPOBJ);                // Reports object cell
        put_u32(REPOBJ, 0xFFFFFFFF);
        put_u32(REPOBJ + 0x0C, 0x08B8E2F0);
        put_u16(REPOBJ + 0x6A, 9);                  // category count
        put_u16(REPOBJ + 0x66, 2);                  // Game Records
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Reports. Game Records, 3 of 9"),
              "Reports speaks its category despite the dead camp witnesses");
        put_u16(REPOBJ + 0x66, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Character Files, 4 of 9"),
              "a nav move speaks the landed Reports category");
        put_u32(0x0991C214, 0);                     // Reports closed
        put_u32(0x08B5BEF8, 8);                     // witnesses restored
        put_u32(0x09919264 + 24, 0);
        put_u32(0x09919264 + 12, CAMPOBJ);
        put_u16(0x09919656, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Decks, 1 of 9"),
              "closing Reports returns speech to the camp main list");
    }

    // ---- camp relocation (audit 2026-10-22, cg-p8/cg-p9) --------------------------
    // The same screens rebuilt at the REAL relocated addresses of
    // a later visit — main object 0x0992E020 (cg-p8 cycle 2),
    // decks object 0x0991F4F0 (cg-p8 cycle 2), pane 0x09928310 and
    // slot widget 0x09922CF0 (cg-p9 visit 2). Speech must follow
    // the registry and the pointer chain, not epoch-1 addresses.
    const uint32_t CAMPOBJ2 = 0x0992E020, CAMPDECK2 = 0x0991F4F0,
                   CAMPPANE2 = 0x09928310, CAMPWIDGET2 = 0x09922CF0;
    put_u32(0x09919264 + 12, CAMPOBJ2);             // main object, relocated
    put_u32(CAMPOBJ2, 0xFFFFFFFF);
    put_u32(CAMPOBJ2 + 0x0C, 0x08BAD400);
    put_u16(CAMPOBJ2 + 0x6A, 9);
    put_u16(CAMPOBJ2 + 0x66, 6);                    // cursor: Command Board
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Camp menu. Command Board, 7 of 9"),
          "the main row speaks from the relocated main object");

    put_u16(CAMPOBJ2 + 0x66, 0);                    // cursor back to Command Decks
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    put_u32(0x09919264 + 12, 0);                    // dived: main unregistered
    put_u32(0x09919264 + 24, CAMPDECK2);            // decks object, relocated
    put_u32(CAMPDECK2, 0xFFFFFFFF);
    put_u32(CAMPDECK2 + 0x0C, 0x08BA59D0);
    put_u16(CAMPDECK2 + 0x126, 0);                  // decks cursor: Edit Deck
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Decks menu. Edit Deck, 1 of 5"),
          "the decks submenu speaks from the relocated decks object");

    put_u16(CAMPDECK2 + 0x126, 3);                  // decks cursor: Choose Deck
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Choose Deck, 4 of 5"),
          "a relocated decks cursor move speaks the landed row");

    put_u32(CAMPDECK2 + 0x9C, CAMPPANE2);           // editor pane, relocated
    put_u32(CAMPPANE2, 0xFFFFFFFF);
    put_u32(CAMPPANE2 + 0x0C, 0x08BA29E0);
    put_u32(CAMPPANE2 + 0x84, 0);
    put_u16(CAMPDECK2 + 0xA4, 2);                   // group focus: Shotlock Command
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Shotlock Command"),
          "the editor speaks from the relocated pane chain");

    put_u32(CAMPPANE2 + 0x84, CAMPWIDGET2);         // slot widget, relocated
    put_u16(CAMPWIDGET2 + 0x6A, 1);                 // slot count: 1 (Shotlock deck)
    put_u16(CAMPWIDGET2 + 0x66, 0);                 // slot cursor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 1 of 1"),
          "the slot list speaks from the relocated widget");

    put_u16(CAMPWIDGET2 + 0x138, 2);                // inventory cursor
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Row 3"),
          "the command list speaks from the relocated widget");

    put_u32(CAMPPANE2 + 0x84, 0);                   // widget freed: back out
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Shotlock Command"),
          "a freed widget returns the tracker to the editor");

    // A registry impostor — right head, wrong class tag, plausible
    // cursor and count — must not be followed.
    put_u32(0x08B5BEF8, 0);                         // camp closed: tracker resets
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    put_u32(0x08B5BEF8, 8);                         // camp open again
    put_u32(0x09919264 + 24, 0);                    // no decks object either
    put_u32(0x09919264 + 12, CAMPOBJ2);             // slot 3 again, but...
    put_u32(CAMPOBJ2 + 0x0C, 0x08BA59D0);           // ...wearing the decks tag
    put_u16(CAMPOBJ2 + 0x66, 3);                    // cursor: D-Links
    put_u16(CAMPOBJ2 + 0x6A, 9);                    // count: 9
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "D-Links"),
          "a registry impostor with the wrong class tag is not followed");

    // ---- Meld Commands (mapped 2026-10-24, bench meld runs 1-7) -------------------
    // The decks object stays in registry slot 6; a meld pane hangs
    // off its +0x9C (tag 0x08B9CF88) and the pane's +0x88 is the
    // meld widget (tag 0x08B9CEB8): glove @ +0x66, panel count @
    // +0x6A, phase flags @ +0x74 (0x200 list, 0x400 dialog, 0x800
    // complete), tab @ +0x14A, list cursor @ +0x14E, Synthesis
    // cursor @ +0x150, filter @ +0x163 (a u8). The dialog's Yes/No
    // cursor is stored nowhere readable, so it is tracked from
    // the Left/Right echoes. Picks are inferred from the host's
    // Cross/Circle echoes (OskType/OskDelete).
    {
        const uint32_t MELDPANE = 0x0992E250, MELDW = 0x09925170;
        const uint32_t POOL = 0x09F262B8, NAMES = 0x09F31000;
        put_u32(0x08B5BEF8, 8);                     // witnesses up
        put_u32(0x08BD194C, 2);
        put_u32(0x09919264 + 12, CAMPOBJ);          // main object up
        put_u32(CAMPOBJ + 0x0C, 0x08BAD400);        // main class tag
        put_u16(CAMPOBJ + 0x66, 0);                 // main cursor: Command Decks
        put_u16(CAMPOBJ + 0x6A, 9);                 // camp item count
        a->on_frame();                              // tracker on the main list
        put_u32(0x09919264 + 12, 0);                // dived
        put_u32(0x09919264 + 24, CAMPOBJ);          // slot 6: decks
        put_u32(CAMPOBJ + 0x0C, 0x08BA59D0);        // decks class tag
        put_u16(CAMPOBJ + 0x126, 2449);             // sub word: churn garbage
        auto def = [&](uint16_t id, uint8_t fam, uint8_t sub,
                       const char* name, uint32_t strAt, uint8_t ord) {
            put_u16(0x08B34924 + (uint32_t) id * 16,
                    (uint16_t)(fam | (sub << 8)));
            put_u32(0x08B34924 + (uint32_t) id * 16 + 8, strAt);
            put_u8(0x08B34924 + (uint32_t) id * 16 + 7, ord);
            put_ascii(strAt, name);
        };
        auto inst = [&](int slot, uint16_t id, uint8_t level,
                        uint16_t ability) {
            uint32_t a = POOL + (uint32_t) slot * 10;
            put_u16(a, id);
            put_u16(a + 2, level);
            put_u16(a + 6, ability);
        };
        def(91, 2, 1, "Quick Blitz", NAMES, 0);
        def(96, 2, 1, "Fire Dash", NAMES + 0x20, 0);
        def(98, 2, 1, "Sonic Blade", NAMES + 0x40, 0);
        def(108, 2, 1, "Thunder Surge", NAMES + 0x60, 0);
        def(188, 2, 3, "Potion", NAMES + 0x80, 0);
        def(243, 3, 4, "Slide", NAMES + 0xA0, 19);   // order byte 19
        def(244, 3, 4, "Jump", NAMES + 0xC0, 1);     // order byte 1
        def(440, 0, 0, "Combo Plus", NAMES + 0xE0, 0);
        inst(0, 96, 4, 0);          // Fire Dash, plain
        inst(1, 96, 4, 440);        // Fire Dash with Combo Plus
        inst(2, 108, 4, 0);         // Thunder Surge
        inst(3, 91, 3, 0);          // Quick Blitz
        inst(4, 188, 60, 0);        // Potion: excluded from meld
        inst(5, 244, 0, 0);         // Jump (action)
        inst(6, 243, 4, 0);         // Slide (action)
        inst(7, 98, 5, 0);          // Sonic Blade
        put_u32(0x08B75E70, POOL);
        // Meld All rows: Quick Blitz, Fire Dash, Fire Dash +CP,
        // Sonic Blade, Thunder Surge, Jump, Slide, then crystals.
        put_u32(CAMPOBJ + 0x9C, MELDPANE);
        put_u32(MELDPANE, 0xFFFFFFFF);
        put_u32(MELDPANE + 0x0C, 0x08B9CF88);
        put_u32(MELDPANE + 0x88, MELDW);
        put_u32(MELDW, 0xFFFFFFFF);
        put_u32(MELDW + 0x0C, 0x08B9CEB8);
        put_u16(MELDW + 0x66, 0);                   // glove: Slot 1
        put_u16(MELDW + 0x6A, 3);                   // panel rows: 3
        put_u16(MELDW + 0x74, 8);                   // phase: panel
        put_u16(MELDW + 0x14A, 0);                  // tab: All
        put_u8(MELDW + 0x164, 0xFF);                // filter flag's neighbour byte (live value)
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Meld Commands. Slot 1."),
              "entering Meld Commands speaks the screen and first row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Meld Commands. Slot 1."),
              "WhereAmI on the meld panel names the focused slot");

        put_u16(MELDW + 0x66, 1);                   // glove: Slot 2
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 2"),
              "a panel move speaks the landed slot");

        // Slot 1's list opens (glove freezes at 0); the cursor word
        // is stale at the open and adopts on the first real move.
        put_u16(MELDW + 0x66, 0);
        put_u16(MELDW + 0x74, 0x208);               // phase: list up
        put_u16(MELDW + 0x14E, 0);
        a->on_frame();
        put_u16(MELDW + 0x14E, 2);                  // Fire Dash +CP
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Fire Dash, level 4, with Combo Plus, 3 of 16"),
              "a meld list row speaks name, level, ability, position");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Meld Commands, Slot 1. Fire Dash, level 4, with Combo Plus, 3 of 16"),
              "WhereAmI in the list names the slot being edited");

        // Cross picks the row; the list closing resolves the press.
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);
        CHECK(NSPOKEN == 0, "the Cross echo itself stays silent");
        put_u16(MELDW + 0x74, 8);                   // list closed
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Slot 1: Fire Dash, level 4, with Combo Plus."),
              "a pick announces what locked into slot 1");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Meld Commands. Slot 1: Fire Dash, level 4, with Combo Plus."),
              "the panel row carries the locked-in ingredient");

        // A refused pick in slot 2's list: Cross, nothing changes.
        put_u16(MELDW + 0x66, 1);
        put_u16(MELDW + 0x74, 0x208);
        put_u16(MELDW + 0x14E, 0);
        a->on_frame();
        a->command(oga::Command::OskType);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        for (int i = 0; i < 60; i++) a->on_frame();
        int refusals = 0;
        for (int i = 0; i < NSPOKEN; i++)
            if (strstr(SPOKEN[i], "That pair won't meld.")) refusals++;
        CHECK(refusals == 1,
              "a Cross the game ignores is spoken once as a refusal");
        // Circle backs out of the same list: no pick is announced.
        a->command(oga::Command::OskDelete);
        put_u16(MELDW + 0x74, 8);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "backing out of a list announces no pick");

        // Slot 2 picked for real: Thunder Surge.
        put_u16(MELDW + 0x74, 0x208);
        put_u16(MELDW + 0x14E, 0);
        a->on_frame();
        put_u16(MELDW + 0x14E, 4);                  // Thunder Surge
        a->on_frame();
        a->command(oga::Command::OskType);
        put_u16(MELDW + 0x74, 8);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Slot 2: Thunder Surge, level 4."),
              "a pick announces what locked into slot 2");

        // The confirm dialog: the cursor starts on Yes and is
        // tracked from the Left/Right echoes (it is stored
        // nowhere readable in the game).
        put_u16(MELDW + 0x66, 3);                   // glove: OK
        put_u16(MELDW + 0x6A, 4);                   // panel rows: 4
        put_u16(MELDW + 0x74, 0x408);               // phase: dialog
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Meld these commands? Yes."),
              "the confirm dialog speaks its question and cursor");
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "No."),
              "moving the dialog cursor speaks the landed choice");
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[2], "Yes."),
              "moving the dialog cursor back speaks Yes");

        // Yes executes: the pool is rewritten (result over the
        // second ingredient's slot, the first slot zeroed).
        inst(2, 98, 1, 0);                          // Thunder Surge slot -> Sonic Blade Lv1
        inst(1, 0, 4, 440);                         // Fire Dash slot consumed
        put_u16(MELDW + 0x74, 0x808);               // phase: complete
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Melding complete. Sonic Blade, level 1."),
              "the completion line names the created command");

        // A dialog that falls without the completion banner is a
        // cancellation, and it is spoken.
        put_u16(MELDW + 0x74, 8);
        a->on_frame();                              // banner falls
        put_u16(MELDW + 0x74, 0x408);               // dialog again
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Meld these commands? Yes."),
              "the dialog speaks again on a second visit");
        put_u16(MELDW + 0x74, 8);                   // chose No: falls
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Melding cancelled."),
              "choosing No speaks the cancellation");

        // The Synthesis list: pick a crystal for the Item row.
        put_u16(MELDW + 0x74, 8);
        put_u16(MELDW + 0x66, 2);                   // glove: Item
        put_u16(MELDW + 0x6A, 3);
        put_u16(MELDW + 0x74, 0x208);               // list up (item)
        put_u16(MELDW + 0x150, 0);
        a->on_frame();
        put_u16(MELDW + 0x150, 1);                  // Fleeting Crystal
        a->on_frame();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Fleeting Crystal, 2 of 9"),
              "a Synthesis row speaks the crystal and its position");
        a->command(oga::Command::OskType);
        put_u16(MELDW + 0x74, 8);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Item: Fleeting Crystal."),
              "a crystal pick announces the chosen item");

        // The usable-only filter's rows cannot be rebuilt: they
        // speak as row numbers.
        put_u16(MELDW + 0x66, 0);
        put_u16(MELDW + 0x74, 0x208);
        put_u16(MELDW + 0x163, 1);                  // filter on
        put_u16(MELDW + 0x14E, 0);
        a->on_frame();
        put_u16(MELDW + 0x14E, 3);
        a->on_frame();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Row 4"),
              "filtered rows fall back to the row number");
        put_u16(MELDW + 0x163, 0);
        put_u16(MELDW + 0x74, 8);
        a->on_frame();

        // Leaving Meld Commands returns speech to the decks
        // submenu — with the sub word still holding churn garbage,
        // it re-seeds from the live cursor.
        put_u32(CAMPOBJ + 0x9C, 0);                 // meld pane gone
        put_u16(CAMPOBJ + 0x126, 1);                // submenu: Meld Commands
        a->on_frame();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Command Decks menu. Meld Commands, 2 of 5"),
              "leaving the meld screen returns to the decks submenu");
    }

    // ---- Mirage Arena Hub guide (landmarks VERIFIED 2026-10-22) -------------------
    // The room's own spawn data (games/kh-bbs/hub-landmarks.md):
    // world 15 room 1 is two floors joined only by a transporter —
    // the entrance platform (save point (0.01, -43), ring (0.04,
    // -37)) and the lobby (arena terminal (0, 0), Moogle Medal Shop
    // (4.68, 9.01), Log In terminal (0, 8.20), return ring (0, -9)). Guide speaks only the
    // player's own floor (split z = -20), each landmark with its
    // distance and, once a movement leg gives a facing, a clock
    // bearing. Reuse the fixture lineage: ACTOR2 and the hub NODE
    // plant (its +0x38 already names ACTOR2).
    put_u32(0x08B5BEF8, 0);                         // camp closed
    put_u16(ACTOR2 + 0x330, 1);                     // fixture actor live again
    RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;   // A: Mirage Arena / Hub
    RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 1;   // B agrees
    a->detach();
    a->attach(&H);                                  // fresh caches, no facing yet
    put_f32(NODE + 0x40, 0.0f); put_f32(NODE + 0x48, -43.0f);   // at the save point
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Save point, right here.") &&
              strstr(SPOKEN[1], "Transporter, 6 away."),
          "on the platform with no facing yet, Guide speaks the platform landmarks");
    CHECK(!strstr(SPOKEN[0], "o'clock") && !strstr(SPOKEN[1], "o'clock") &&
              !strstr(SPOKEN[0], "straight ahead") &&
              !strstr(SPOKEN[1], "straight ahead"),
          "no facing, no bearing — never a guessed direction");

    a->on_frame();                                  // anchor at the save point
    put_f32(NODE + 0x48, -40.0f);                   // a 3-unit leg toward the ring (+z)
    a->on_frame();                                  // facing (0, 1): +z
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Save point, 3 away, 6 o'clock.") &&
              strstr(SPOKEN[1], "Transporter, 3 away, straight ahead."),
          "with a +z facing, Guide adds clock bearings");

    put_f32(NODE + 0x48, -10.0f); a->on_frame();    // jump leg; facing stays +z
    put_f32(NODE + 0x48, -7.86f); a->on_frame();    // lobby arrival, beside the return ring
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 4 &&
              strstr(SPOKEN[0], "Arena terminal, 8 away, straight ahead.") &&
              strstr(SPOKEN[1], "Moogle Medal Shop, 18 away, 1 o'clock.") &&
              strstr(SPOKEN[2], "Log In terminal, 16 away, straight ahead.") &&
              strstr(SPOKEN[3], "Transporter, right here."),
          "in the lobby, Guide speaks the lobby landmarks with bearings");
    CHECK(!strstr(SPOKEN[0], "Save point") && !strstr(SPOKEN[1], "Save point") &&
              !strstr(SPOKEN[2], "Save point") && !strstr(SPOKEN[3], "Save point"),
          "the other floor's landmarks are never named");

    put_f32(NODE + 0x48, -20.0f);                   // exactly on the split
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 4 && strstr(SPOKEN[0], "Arena terminal, 20 away, straight ahead."),
          "z = -20 is the lobby (the platform is z < -20)");
    put_f32(NODE + 0x48, -20.5f);                   // just platform-side of the split
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Save point, 23 away, 6 o'clock."),
          "z = -20.5 is the platform");

    RAM[OFF(0x08B61CA0)] = 6;  RAM[OFF(0x08B61CA1)] = 13;  // Radiant Garden
    RAM[OFF(0x08B759C0)] = 6;  RAM[OFF(0x08B759C1)] = 13;
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 0, "Guide is silent outside the Hub");
    RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;   // A: Hub again
    RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 2;   // B names another room
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 0, "Guide is silent while the location copies disagree");
    RAM[OFF(0x08B759C1)] = 1;                        // copies agree again

    put_u32(NODE + 0x1C, 0);                         // the component cannot be located
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 0, "Guide is silent when the position cannot be located");
    put_u32(NODE + 0x1C, 0x08B73DB4);                // restore the descriptor word

    put_u32(0x08B5BEF8, 8);                          // camp menu opens over the Hub
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 0, "Guide is silent while the camp menu is open");
    put_u32(0x08B5BEF8, 0);                          // camp closed again
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Guide);
    CHECK(NSPOKEN == 2, "Guide speaks again once camp closes");

    // ---- the game-over menu (VERIFIED 2026-10-24, bench go1/go2/go3) ------------
    // Death in the field -> Continue / Retry / Load Game. The gate
    // is the conjunction the bench proved: the list object's
    // six-word signature (planted at its real FE-lineage address),
    // the corpse of the actor that died still reading dead at the
    // last base where it verified alive, and the object's own
    // overlay fields (+0x14/+0x16) at 0 — they read 14/2 under
    // the Load screen and 5/2 on the world map, where the
    // signature lingers on. Each witness is knocked out in turn
    // below; every knockout must stay silent.
    {
        static const uint32_t GOACTOR = 0x09980B10;   // the FE fight lineage's actor base
        static const uint32_t GOSTR   = 0x099BC5A0;   // the list object (same lineage)
        // Retire the earlier actor plants so the sweep sees one
        // actor at a time, and close the title/difficulty gates:
        // this section's silence checks must mean the NEW gates.
        put_u16(0x09976090 + 0x382, 0);               // ACTOR (tutorial plant)
        put_u16(0x0996C250 + 0x382, 0);               // ACTOR2 (fixture plant)
        put_u16(0x09910000 + 0x382, 0);               // ACTOR3 (battle plant)
        put_u32(TITLE_PHASE, 0);
        put_u32(SUBMENU_PTR, 0);
        put_u32(0x08B5BEF8, 0);                       // camp closed
        a->detach();
        a->attach(&H);

        // A live field actor at full health.
        put_u16(GOACTOR + 0x382, 149);                // max HP
        put_u16(GOACTOR + 0x380, 149);                // cur HP
        put_u16(GOACTOR + 0x226, 0);                  // gauge
        put_u16(GOACTOR + 0x330, 1);                  // spawn mark
        put_u32(GOACTOR + 0x234, 0x8068);             // flags, bit 0x20
        put_u32(GOACTOR + 0x220, 12);                 // state: FE field
        put_u32(GOACTOR + 0x58, 0x099816A0);          // live pointer field
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "game over: a live actor at full health is silent");
        CHECK(a->ready(), "game over setup: ready in the field");

        // The list object, complete with overlay fields at 0, but
        // nobody died: the struct alone must never speak.
        put_u16(GOSTR + 0x00, 0xFFFF); put_u16(GOSTR + 0x02, 0xFFF8);
        put_u16(GOSTR + 0x04, 0);      put_u16(GOSTR + 0x06, 0);
        put_u16(GOSTR + 0x08, 0);      put_u16(GOSTR + 0x0A, 3);
        put_u16(GOSTR + 0x14, 0);      put_u16(GOSTR + 0x16, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "game over: signature and overlay fields without a death stay silent");

        // Death, mid-fade: the corpse reads dead, but the list
        // object is not built yet (signature hidden) — silent.
        put_u16(GOACTOR + 0x380, 0);                  // cur HP 0
        put_u32(GOACTOR + 0x220, 15);                 // state: dead
        put_u16(GOSTR + 0x00, 0);                     // object not built
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "game over: the death fade (no list object yet) stays silent");

        // The menu is up: arrival speaks once, naming the row it
        // opened on, and does not repeat while it stays up.
        put_u16(GOSTR + 0x00, 0xFFFF);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Game over. Continue, 1 of 3."),
              "game over: arrival speaks the menu and its opening row");
        a->on_frame();
        CHECK(NSPOKEN == 1, "game over: the arrival line does not repeat");
        CHECK(a->ready(), "game over: ready while the menu is up");

        // A two-frame gate flicker — the live menu drops the
        // gate briefly during a cursor move (khbbs-menus-live) —
        // must not re-fire the arrival line.
        put_u16(GOSTR + 0x14, 14);
        a->on_frame(); a->on_frame();
        put_u16(GOSTR + 0x14, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "game over: a two-frame gate flicker does not re-fire the arrival");

        // Nav echoes read the live cursor.
        put_u16(GOSTR + 0x06, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Retry, 2 of 3"),
              "game over: nav speaks the landed row (Retry)");
        put_u16(GOSTR + 0x06, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Load Game, 3 of 3"),
              "game over: nav speaks the landed row (Load Game)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Game over. Load Game, 3 of 3."),
              "game over: WhereAmI names the menu and the current row");

        // The Load-screen look-alike: same corpse, same lingering
        // signature, but the overlay fields have moved on (14/2)
        // — the Load screen owns the display now.
        put_u16(GOSTR + 0x14, 14); put_u16(GOSTR + 0x16, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        for (int i = 0; i < 40; i++) a->on_frame();   // the Load screen lasts; the gate is down throughout
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "game over: the Load screen over the stale signature stays silent");

        // Backing out of the Load screen returns to the menu: it
        // owns the screen again, so the arrival line speaks again,
        // on the row left highlighted.
        put_u16(GOSTR + 0x14, 0); put_u16(GOSTR + 0x16, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Game over. Load Game, 3 of 3."),
              "game over: returning from the Load screen speaks the menu again");

        // The corpse witness knocked out on its own: signature
        // and overlay fields perfect, but the actor struct is
        // freed — state 0, as it reads on the world map after
        // Continue.
        put_u32(GOACTOR + 0x220, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "game over: a freed actor struct (post-Continue) stays silent");

        // The signature knocked out on its own: a wrong count word
        // (a different list) with corpse and overlay fields perfect.
        put_u32(GOACTOR + 0x220, 15);
        put_u16(GOSTR + 0x0A, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "game over: a wrong count word fails the signature closed");

        // Retire the section's plants.
        put_u16(GOSTR + 0x00, 0); put_u16(GOSTR + 0x0A, 0);
        put_u32(GOACTOR + 0x220, 0);
        a->on_frame();
    }

    // ---- the movie pause menu (VERIFIED 2026-10-24, bench mp3/mp4) --------------
    // START during an in-game scene: Resume / Skip Scene. The
    // gate is the conjunction the bench proved: count word 2,
    // state words 1024 and 255, cursor in range. The count word
    // alone already reads 2 while the scene plays on (it lingers
    // once the object exists), so each witness is knocked out in
    // turn; every knockout must stay silent.
    {
        static const uint32_t MPCUR = 0x09918D76;
        static const uint32_t MPCNT = 0x09918D7A;
        static const uint32_t MPW82 = 0x09918D82;
        static const uint32_t MPW84 = 0x09918D84;
        a->detach();
        a->attach(&H);

        // The scene playing unpaused: the count word reads its
        // lingering 2, the state words read their playing values.
        put_u16(MPCUR, 0); put_u16(MPCNT, 2);
        put_u16(MPW82, 5120); put_u16(MPW84, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "movie pause: the unpaused scene stays silent");

        // One witness at a time knocked out of the full plant.
        put_u16(MPW82, 1024); put_u16(MPW84, 255); put_u16(MPCNT, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "movie pause: no count word, no menu");
        put_u16(MPCNT, 2); put_u16(MPW82, 5120);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "movie pause: the 1024 witness is load-bearing");
        put_u16(MPW82, 1024); put_u16(MPCUR, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 0, "movie pause: an out-of-range cursor fails closed");

        // The menu is up: arrival speaks once, on Resume.
        put_u16(MPCUR, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Paused. Resume, 1 of 2."),
              "movie pause: arrival speaks the menu and its opening row");
        a->on_frame();
        CHECK(NSPOKEN == 1, "movie pause: the arrival line does not repeat");
        CHECK(a->ready(), "movie pause: ready while the menu is up");

        // Nav echoes and WhereAmI read the live cursor.
        put_u16(MPCUR, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Skip Scene, 2 of 2"),
              "movie pause: nav speaks the landed row (Skip Scene)");
        put_u16(MPCUR, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Resume, 1 of 2"),
              "movie pause: nav speaks the landed row (Resume)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Paused. Resume, 1 of 2."),
              "movie pause: WhereAmI names the menu and the current row");

        // Resume chosen: the state words fall back to their
        // playing values and the menu is gone.
        put_u16(MPW82, 5120); put_u16(MPW84, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->on_frame();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "movie pause: after Resume the menu is silent");

        // Retire the section's plants.
        put_u16(MPCUR, 0); put_u16(MPCNT, 0); put_u16(MPW82, 0); put_u16(MPW84, 0);
    }

    // ---- the Mirage Arena event menu (VERIFIED 2026-10-24, bench g10) --------
    // The Hub lobby's Log In console (gimmick g10vs00 at
    // (0, 8.20)) opens the MIRAGE ARENA menu: New Event /
    // Active Events / D-Links. The widget exists in two homes
    // with one layout and one signature (screen 2, the
    // constant words 15 / 0xDBFF / 0xFFF8 / 0, count 3,
    // cursor < 3): home A (0x09919208) for fresh opens, home
    // B (0x0991BCB8) for the menu rebuilt after the New Event
    // mode child — conjoined with the flow word at
    // 0x08B5BEF8 (8 while the event flow owns the screen),
    // which is what separates the live menu from home B's
    // lingering post-close widget. In the field both homes
    // read junk (A: 40849 / 54715 / 11786 in both bench
    // lineages) and the flow word is 0; on the mode child A
    // reads 0 and B holds the child's ghost (screen 3,
    // count 0) — each witness is knocked out in turn below;
    // every knockout must stay silent.
    {
        static const uint32_t EVA = 0x09919208;
        static const uint32_t EVB = 0x0991BCB8;
        static const uint32_t EVFLOW = 0x08B5BEF8;
        put_u32(0x08BD194C, 0);   // camp witness B dead for this section
        a->detach();
        a->attach(&H);
        auto ev_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        // Plant one home's widget: screen/count/cursor vary,
        // the constant signature words are written in full.
        // The flow word goes to 8 with every plant (it is up
        // through the whole event flow, the mode child too);
        // phases that simulate the field clear it explicitly.
        auto plant_ev = [&](uint32_t base, uint16_t scr, uint16_t cur, uint16_t cnt) {
            put_u32(EVFLOW, 8);
            put_u16(base + 0x00, scr);
            put_u16(base + 0x04, 15);
            put_u16(base + 0x08, 0xDBFF);
            put_u16(base + 0x0A, 0xFFF8);
            put_u16(base + 0x0C, 0);
            put_u16(base + 0x0E, cur);
            put_u16(base + 0x12, cnt);
        };
        auto junk_ev = [&](uint32_t base) {   // the freed-block bench junk
            put_u16(base + 0x00, 52692); put_u16(base + 0x04, 0);
            put_u16(base + 0x08, 0); put_u16(base + 0x0A, 0);
            put_u16(base + 0x0C, 0); put_u16(base + 0x0E, 2272);
            put_u16(base + 0x12, 2272);
        };

        // The field beside the console: home A reads its
        // bench junk, home B is junk too, the flow word is
        // down, and nothing speaks.
        put_u32(EVFLOW, 0);
        put_u16(EVA + 0x00, 40849); put_u16(EVA + 0x0E, 54715); put_u16(EVA + 0x12, 11786);
        junk_ev(EVB);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "event menu: the field's junk words stay silent");

        // One witness at a time knocked out of the full plant.
        plant_ev(EVA, 2, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 0, "event menu: no count word, no menu (the mode child reads 0 here)");
        plant_ev(EVA, 0, 0, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 0, "event menu: the screen word is load-bearing");
        plant_ev(EVA, 2, 3, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 0, "event menu: an out-of-range cursor fails closed");

        // The menu is up at home A: arrival speaks once.
        plant_ev(EVA, 2, 0, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. New Event, 1 of 3."),
              "event menu: arrival speaks the menu and its opening row");
        ev_frames(2);
        CHECK(NSPOKEN == 1, "event menu: the arrival line does not repeat");
        CHECK(a->ready(), "event menu: ready while the menu is up");

        // A few-frame gate flicker (the cursor-move pattern the
        // game-over debounce exists for) must not re-fire.
        put_u16(EVA + 0x12, 0);
        ev_frames(3);
        put_u16(EVA + 0x12, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 0, "event menu: a three-frame gate flicker does not re-fire the arrival");

        // Nav echoes and WhereAmI read the live cursor.
        plant_ev(EVA, 2, 1, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Active Events, 2 of 3"),
              "event menu: nav speaks the landed row (Active Events)");
        plant_ev(EVA, 2, 2, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Links, 3 of 3"),
              "event menu: nav speaks the landed row (D-Links)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. D-Links, 3 of 3."),
              "event menu: WhereAmI names the menu and the current row");

        // Into the mode child (home A reads 0 there): the menu
        // goes silent, and coming back after a real stretch
        // down re-announces the top menu.
        plant_ev(EVA, 0, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(40);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "event menu: the mode child stays silent");
        plant_ev(EVA, 2, 0, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. New Event, 1 of 3."),
              "event menu: returning from the child re-announces the menu");

        // The rebuilt menu lives at home B (bench g10e: after
        // the mode child, A reads freed junk while B tracks
        // the cursor). Retire A to its junk, stretch down,
        // then the menu returns at B and speaks from there.
        junk_ev(EVA); plant_ev(EVB, 0, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(40);
        plant_ev(EVB, 2, 1, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(2);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. Active Events, 2 of 3."),
              "event menu: the rebuilt menu at home B announces its row");
        plant_ev(EVB, 2, 2, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Links, 3 of 3"),
              "event menu: nav reads the cursor at home B");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. D-Links, 3 of 3."),
              "event menu: WhereAmI names the rebuilt menu and its row");

        // Home B's mode-child ghost (screen 3, count 0) is not
        // the menu: silence, even on a nav command.
        plant_ev(EVB, 3, 1, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(40);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "event menu: home B's child ghost stays silent");

        // The stale widget: after a mode-child trip the menu
        // closes but home B's block keeps the full signature
        // (bench g10c/g10f end dumps) — only the flow word
        // drops. With the word down the lingering widget must
        // stay silent: no arrival, no nav echo.
        plant_ev(EVB, 2, 0, 3);
        put_u32(EVFLOW, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(40);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "event menu: a lingering widget with the flow word down stays silent");

        // Retire the section's plants.
        plant_ev(EVA, 0, 0, 0); plant_ev(EVB, 0, 0, 0);
        put_u32(EVFLOW, 0);
    }

    // ---- the world map (VERIFIED 2026-10-24, bench wm1) --------------------------
    // World 17 room 1 in BOTH map-id copies, held across the
    // whole session. The selected-world word reads the world
    // id (0 in open space). Two things are proven here: the
    // map's own speech (arrival, selection by name, the visit
    // dialog, the map's pause menu) and the field gate's
    // exclusion — a fully plausible actor at 1 HP must NOT
    // trip the low-HP warning on the map, and MUST trip it
    // the moment the same actor stands in a real field.
    {
        static const uint32_t MAPA = 0x08B61CA0, MAPB = 0x08B759C0;
        static const uint32_t SELW = 0x099658C0;
        static const uint32_t WMACTOR = 0x09950000;   // the low-HP section's plant base
        static const uint32_t VDOBJ = 0x099A0000, VDOBJ2 = 0x099A0800;
        static const uint32_t MPCUR = 0x09918D76, MPCNT = 0x09918D7A,
                              MPW82 = 0x09918D82, MPW84 = 0x09918D84;
        a->detach();
        a->attach(&H);
        auto wm_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        auto spoken_has = [&](const char* needle) {
            for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], needle)) return true;
            return false;
        };

        // A plausible actor at 1 HP — the glider-side stand-in.
        put_u16(WMACTOR + 0x380, 1);         // cur HP
        put_u16(WMACTOR + 0x382, 149);       // max HP
        put_u16(WMACTOR + 0x226, 0);         // gauge
        put_u16(WMACTOR + 0x330, 1);         // spawn mark
        put_u32(WMACTOR + 0x234, 0x8068);    // flags, bit 0x20
        put_u32(WMACTOR + 0x220, 12);        // state
        put_u32(WMACTOR + 0x58, 0x099816A0); // command cluster pointer
        // On the map, in open space (nothing selected).
        RAM[OFF(MAPA)] = 17; RAM[OFF(MAPA) + 1] = 1;
        RAM[OFF(MAPB)] = 17; RAM[OFF(MAPB) + 1] = 1;
        put_u32(SELW, 0);

        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "World map.") == 0,
              "world map: arrival in open space speaks World map");
        CHECK(!spoken_has("Low health"),
              "world map: the 1-HP actor does NOT trip the low-HP warning on the map");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        CHECK(NSPOKEN == 0, "world map: arrival does not repeat, and the map stays quiet");
        CHECK(a->ready(), "world map: ready on the map");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "World map.") == 0,
              "world map: WhereAmI in open space gives the map form, not a field readout");

        // Fly into Radiant Garden's ring.
        put_u32(SELW, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(8);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Radiant Garden.") == 0,
              "world map: reaching a world speaks its name");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "World map. Radiant Garden.") == 0,
              "world map: WhereAmI names the selected world");

        // A two-frame graze of another ring must not chatter.
        put_u32(SELW, 9);
        wm_frames(2);
        put_u32(SELW, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(8);
        CHECK(NSPOKEN == 0, "world map: a brief graze of another ring stays silent");

        // A real move to Deep Space, then out to open space.
        put_u32(SELW, 9);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(8);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Deep Space.") == 0,
              "world map: moving to another world speaks its name");
        put_u32(SELW, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(8);
        CHECK(NSPOKEN == 0, "world map: leaving a world for open space stays silent");

        // The same actor, the same 1 HP, in a real field: the
        // warning must fire (the exclusion is map-only).
        RAM[OFF(MAPA)] = 6; RAM[OFF(MAPA) + 1] = 13;
        RAM[OFF(MAPB)] = 6; RAM[OFF(MAPB) + 1] = 13;
        put_u16(WMACTOR + 0x380, 149);       // full: settles the latch
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        CHECK(NSPOKEN == 0, "world map fix: full health in the field stays silent");
        put_u16(WMACTOR + 0x380, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Low health."),
              "world map fix: the same 1 HP in a real field still warns");

        // Back on the map, parked at Radiant Garden: arrival
        // names the world she is already parked on.
        RAM[OFF(MAPA)] = 17; RAM[OFF(MAPA) + 1] = 1;
        RAM[OFF(MAPB)] = 17; RAM[OFF(MAPB) + 1] = 1;
        put_u32(SELW, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "World map. Radiant Garden.") == 0,
              "world map: arriving parked on a world names it in the arrival line");

        // The visit dialog: signature object, active witness
        // clear, cursor starting on Leave as the bench proved.
        put_u16(VDOBJ + 0x00, 0xE7FF); put_u16(VDOBJ + 0x02, 0xFFF8);
        put_u16(VDOBJ + 0x04, 0);      put_u16(VDOBJ + 0x06, 1);
        put_u16(VDOBJ + 0x08, 1);      put_u16(VDOBJ + 0x0A, 2);
        put_u32(VDOBJ + 0x10, 0x04000000);   // bit 28 clear: open
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Visit Radiant Garden? Leave, 2 of 2.") == 0,
              "visit dialog: arrival names the world and the opening row (Leave)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Leave, 2 of 2"),
              "visit dialog: nav speaks the landed row");
        put_u16(VDOBJ + 0x06, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Visit This World, 1 of 2"),
              "visit dialog: nav speaks the landed row (Visit This World)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Visit Radiant Garden? Visit This World, 1 of 2.") == 0,
              "visit dialog: WhereAmI names the dialog and the current row");

        // Closed: the active witness sets, the object lingers.
        put_u32(VDOBJ + 0x10, 0x14000000);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "visit dialog: the lingering closed object stays silent");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "World map. Radiant Garden.") == 0,
              "visit dialog: after it closes, WhereAmI is the map form again");

        // The stale-object look-alike: signature broken (the
        // 0xFDFF head the bench found in the field) with the
        // active bit clear — still not a dialog.
        put_u16(VDOBJ + 0x00, 0xFDFF);
        put_u32(VDOBJ + 0x10, 0x04000000);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "visit dialog: a broken signature fails closed");

        // Relocation: the dialog reopens at a new address and
        // the locator adopts it. (The 40 closed frames matter:
        // the arrival edge is debounced, like the game-over
        // menu's, so a rise only speaks after a real stretch
        // down — a genuine reopen qualifies, a flicker doesn't.)
        wm_frames(40);
        put_u16(VDOBJ2 + 0x00, 0xE7FF); put_u16(VDOBJ2 + 0x02, 0xFFF8);
        put_u16(VDOBJ2 + 0x04, 0);      put_u16(VDOBJ2 + 0x06, 0);
        put_u16(VDOBJ2 + 0x08, 0);      put_u16(VDOBJ2 + 0x0A, 2);
        put_u32(VDOBJ2 + 0x10, 0x04000000);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Visit Radiant Garden? Visit This World, 1 of 2.") == 0,
              "visit dialog: a relocated object is found and speaks");

        // The live flicker: the gate drops for a few frames
        // during a cursor move (khbbs-wm-live's first run) —
        // the arrival line must not re-fire.
        put_u32(VDOBJ2 + 0x10, 0x14000000);   // gate down...
        wm_frames(3);
        put_u32(VDOBJ2 + 0x10, 0x04000000);   // ...and back up
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        CHECK(NSPOKEN == 0, "visit dialog: a three-frame gate flicker does not re-fire the arrival");
        put_u32(VDOBJ2 + 0x10, 0x14000000);   // close it
        wm_frames(2);

        // The map's own pause menu, on the movie pause's
        // statics but with count 3.
        put_u16(MPCUR, 0); put_u16(MPCNT, 3);
        put_u16(MPW82, 1024); put_u16(MPW84, 255);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Paused. Resume, 1 of 3.") == 0,
              "map pause: arrival speaks the menu and its opening row");
        put_u16(MPCUR, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Open Menu, 2 of 3"),
              "map pause: nav speaks the landed row (Open Menu)");
        put_u16(MPCUR, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Save, 3 of 3"),
              "map pause: nav speaks the landed row (Save)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Paused. Save, 3 of 3.") == 0,
              "map pause: WhereAmI names the menu and the current row");
        // After hosting the savedata dialog, the pause's word 84
        // settles at 4 instead of 255 (bench sl4/sl8) — the menu
        // is still live and must still speak.
        put_u16(MPW84, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Paused. Save, 3 of 3.") == 0,
              "map pause: after a save dialog round trip the pause still speaks");
        put_u16(MPW84, 255);
        // Closed (state words fall; the count word lingers at 3).
        put_u16(MPW82, 5120); put_u16(MPW84, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "map pause: after Resume the menu is silent");

        // The same statics off the map must not read as the
        // map's pause (the gate requires the map ids).
        RAM[OFF(MAPA)] = 6; RAM[OFF(MAPA) + 1] = 13;
        RAM[OFF(MAPB)] = 6; RAM[OFF(MAPB) + 1] = 13;
        put_u16(WMACTOR + 0x380, 149);
        put_u16(MPCUR, 0); put_u16(MPW82, 1024); put_u16(MPW84, 255);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        wm_frames(4);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "map pause: the open statics off the map stay silent");

        // Retire the section's plants.
        put_u16(MPCUR, 0); put_u16(MPCNT, 0); put_u16(MPW82, 0); put_u16(MPW84, 0);
        put_u16(VDOBJ2 + 0x00, 0); put_u32(SELW, 0);
        put_u16(WMACTOR + 0x380, 0); put_u16(WMACTOR + 0x382, 0); put_u16(WMACTOR + 0x330, 0);
        wm_frames(2);
    }

    // ---- the save / load screens (VERIFIED 2026-10-24, bench sl1-sl6) --------
    // The firmware savedata dialog: the gate is the dialog work
    // area's two pointer words (open in every dialog dump, zero in
    // every closed one) AND the game's SceUtilitySavedataParam
    // block by signature (size 0x600, gameName at +0x3C, fileName
    // at +0x64, mode 4=LISTLOAD / 5=LISTSAVE at +0x30). The block
    // LINGERS after a load session closes, so neither witness may
    // speak alone. The selection is host-side; the cursor is
    // echo-tracked from the focus seed the param carries. A short
    // 6-name list stands in for the game's 200.
    {
        static const uint32_t SLP = 0x09950000;   // the param block
        static const uint32_t SLL = 0x09960000;   // the name list
        static const uint32_t GATEA = 0x09FFF5C4;
        static const uint32_t GATEB = 0x09FFF5C8;
        memset(RAM, 0, sizeof(RAM));
        a->detach();
        a->attach(&H);
        auto sl_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        auto sl_plant = [&](uint32_t mode, uint32_t focus, const char* sname, int nnames) {
            memset(&RAM[OFF(SLP)], 0, 0x600);
            put_u32(SLP, 0x600);
            put_u32(SLP + 0x30, mode);
            put_ascii(SLP + 0x3C, "ULUS10505");
            put_ascii(SLP + 0x64, "SAVEDATA.DAT");
            if (sname) put_ascii(SLP + 0x4C, sname);
            put_u32(SLP + 0x60, SLL);
            put_u32(SLP + 0x5C8, focus);
            memset(&RAM[OFF(SLL)], 0, 20 * 16);
            for (int i = 0; i < nnames; i++) {
                char nm[8];
                snprintf(nm, sizeof(nm), "%04d", i);
                put_ascii(SLL + (uint32_t) i * 20, nm);
            }
        };
        auto sl_open  = [&]() { put_u32(GATEA, 0x09C035D0); put_u32(GATEB, 0x09C03670); };
        auto sl_close = [&]() { put_u32(GATEA, 0); put_u32(GATEB, 0); };

        // S1: save arrival (FOCUS_NAME on the session's own file),
        // rows, the clamp at the end of the list.
        sl_plant(5, 0, "0002", 6);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(13);   // arrival + past the opener-echo guard
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Save. Slot 3 of 6, current save."),
              "save/load: save arrival names the screen and the seeded slot");
        CHECK(a->ready(), "save/load: ready while the dialog is up");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 4 of 6"),
              "save/load: MenuNext speaks the landed slot");
        a->command(oga::Command::MenuNext);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[2], "Slot 6 of 6"),
              "save/load: rows track to the last slot");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "save/load: a clamped press at the end stays silent");
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 5 of 6"),
              "save/load: MenuPrev speaks the landed slot");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Save screen. Slot 5 of 6."),
              "save/load: WhereAmI names the save screen and slot");
        sl_close(); sl_frames(3);

        // S2: the overwrite confirm, answered Yes: the write runs
        // and the close speaks the completion.
        sl_frames(35);   // let the arrival debounce re-arm
        sl_plant(5, 0, "0000", 6);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(13);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Save. Slot 1 of 6, current save."),
              "save/load: a second save dialog announces afresh");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);   // Cross on an occupied slot
        CHECK(NSPOKEN == 0, "save/load: the first Cross alone announces nothing yet");
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Overwrite this save? Yes."),
              "save/load: Left proves the confirm and speaks it on Yes");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No."),
              "save/load: Right moves the confirm back to No");
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Yes."),
              "save/load: Left moves the confirm to Yes again");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);   // answer Yes
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Saving."),
              "save/load: answering Yes speaks the write starting");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_close(); sl_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Saved."),
              "save/load: the dialog closing after the write speaks Saved.");
        sl_frames(3);
        CHECK(NSPOKEN == 1, "save/load: Saved. is spoken once");

        // S3: the confirm answered No returns to the list, and a
        // plain cancel out of the list completes nothing.
        sl_frames(35);
        sl_plant(5, 0, "0000", 6);
        sl_open();
        sl_frames(13);
        a->command(oga::Command::OskType);
        a->command(oga::Command::MenuLeft);   // confirm, on Yes
        a->command(oga::Command::MenuRight);  // back to No
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);    // answer No
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 1 of 6, current save"),
              "save/load: answering No returns to the list and re-speaks the slot");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskDelete);  // Circle cancels the dialog
        sl_close(); sl_frames(3);
        CHECK(NSPOKEN == 0, "save/load: cancelling the list speaks no completion");

        // S4: Cross, Cross with no Left — the No answer and an
        // empty slot's write are indistinguishable; stay silent.
        sl_frames(35);
        sl_plant(5, 0, "0000", 6);
        sl_open();
        sl_frames(13);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);
        a->command(oga::Command::OskType);
        CHECK(NSPOKEN == 0, "save/load: the ambiguous second Cross stays silent");
        a->command(oga::Command::OskDelete);
        sl_close(); sl_frames(3);
        CHECK(NSPOKEN == 0, "save/load: the ambiguous close speaks no Saved.");

        // S5: load with FOCUS_LATEST — unseeded arrival, range
        // narrowing, convergence at the end of the list, and a
        // completion the field, not this screen, announces.
        sl_frames(35);
        sl_plant(4, 3, "", 6);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(13);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Load.") && !strstr(SPOKEN[0], "Slot"),
              "save/load: a latest-focus load announces no unproven slot");
        a->command(oga::Command::MenuNext);
        a->command(oga::Command::MenuNext);
        a->command(oga::Command::MenuPrev);
        a->command(oga::Command::MenuPrev);
        a->command(oga::Command::MenuPrev);
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1, "save/load: rows stay silent while the position is a range");
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 2 && strstr(SPOKEN[1], "Slot 1 of 6"),
              "save/load: the range collapsing to one slot starts row speech");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Loading."),
              "save/load: Cross on a load slot speaks the load starting");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskDelete);  // dismiss the completed screen
        sl_close(); sl_frames(3);
        CHECK(NSPOKEN == 0, "save/load: a finished load lets the field speak, not Saved.");

        // S6: a synced load, and a failed load's return to the
        // list (the dismiss press, the dialog still up past the
        // timeout, the row re-spoken).
        sl_frames(35);
        sl_plant(4, 1, "", 6);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(13);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Load. Slot 1 of 6."),
              "save/load: a first-list load seeds on slot 1");
        a->command(oga::Command::OskType);
        a->command(oga::Command::OskDelete);  // failed load: Back returns to the list
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(320);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 1 of 6"),
              "save/load: a dismissed press that keeps the dialog up returns to the list");
        sl_close(); sl_frames(3);

        // S7a: the work-area words without the param — some other
        // utility state; never the save/load screen.
        memset(&RAM[OFF(SLP)], 0, 0x600);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(3);
        CHECK(NSPOKEN == 0 && !a->ready(),
              "save/load: dialog work area without the savedata param stays silent");
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "save/load: WhereAmI does not claim the save screen without the param");
        sl_close(); sl_frames(3);

        // S7b: the param block without the work-area words — the
        // lingering block after a load session; not a live dialog.
        sl_plant(5, 0, "0000", 6);
        sl_close();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(3);
        CHECK(NSPOKEN == 0 && !a->ready(),
              "save/load: a lingering param block alone is not a dialog");
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "save/load: WhereAmI ignores the lingering block");

        // S7c: the camp trap — the camp witnesses are set under
        // the save dialog from the save point; the dialog wins.
        put_u32(0x08B5BEF8, 8);
        put_u32(0x08BD194C, 2);
        sl_frames(35);
        sl_plant(5, 0, "0001", 6);
        sl_open();
        sl_frames(13);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Save screen. Slot 2 of 6, current save."),
              "save/load: over the camp witnesses, WhereAmI speaks the save screen");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 3 of 6"),
              "save/load: over the camp witnesses, nav speaks slots, not camp rows");
        sl_close(); sl_frames(3);
        put_u32(0x08B5BEF8, 0);
        put_u32(0x08BD194C, 0);

        // S7d: Load over the game-over menu — the menu's overlay
        // fields (14/2 under the Load screen) keep it silent
        // while the dialog speaks; closing the dialog back to
        // the menu re-announces it.
        {
            static const uint32_t GOACTOR = 0x09980B10;
            static const uint32_t GOSTR   = 0x099BC5A0;
            put_u16(GOACTOR + 0x382, 149); put_u16(GOACTOR + 0x380, 149);
            put_u16(GOACTOR + 0x226, 0);   put_u16(GOACTOR + 0x330, 1);
            put_u32(GOACTOR + 0x234, 0x8068); put_u32(GOACTOR + 0x220, 12);
            put_u32(GOACTOR + 0x58, 0x099816A0);
            a->on_frame();   // the actor verifies alive and latches
            put_u16(GOACTOR + 0x380, 0);   // death
            put_u32(GOACTOR + 0x220, 15);
            put_u16(GOSTR + 0x00, 0xFFFF); put_u16(GOSTR + 0x02, 0xFFF8);
            put_u16(GOSTR + 0x04, 0);      put_u16(GOSTR + 0x06, 0);
            put_u16(GOSTR + 0x08, 0);      put_u16(GOSTR + 0x0A, 3);
            put_u16(GOSTR + 0x14, 14);     put_u16(GOSTR + 0x16, 2);  // under Load
            sl_frames(35);
            sl_plant(4, 3, "", 6);
            sl_open();
            NSPOKEN = 0; oga::AdapterSpeechReset();
            sl_frames(35);
            CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Load."),
                  "save/load: Load over game over speaks the load, not the menu");
            CHECK(!strstr(SPOKEN[0], "Game over"),
                  "save/load: no double speech from the covered game-over menu");
            sl_close();
            put_u16(GOSTR + 0x14, 0);      put_u16(GOSTR + 0x16, 0);
            NSPOKEN = 0; oga::AdapterSpeechReset();
            sl_frames(3);
            CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Game over. Continue, 1 of 3."),
                  "save/load: closing Load back to the menu re-announces game over");
            put_u16(GOSTR + 0x00, 0);
            put_u16(GOACTOR + 0x380, 0); put_u16(GOACTOR + 0x382, 0);
            sl_frames(3);
        }

        // S8: the opener's own echo, landing just after the rise,
        // must not be read as a press inside the dialog (sl7's
        // phantom "Loading."); a later press is.
        sl_frames(35);
        sl_plant(4, 1, "", 6);
        sl_open();
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(2);   // arrival fires on the first up frame
        a->command(oga::Command::OskType);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Load. Slot 1 of 6."),
              "save/load: the opener's late echo does not start a phantom load");
        sl_frames(12);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::OskType);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Loading."),
              "save/load: a press after the guard window is the dialog's own");
        sl_close(); sl_frames(3);

        // S9: a quick close and reopen inside the arrival
        // debounce speaks no second arrival, but the tracker
        // re-seeds silently and nav tracks the new session.
        sl_plant(5, 0, "0000", 6);
        sl_open();   // only a few down-frames since S8 closed
        NSPOKEN = 0; oga::AdapterSpeechReset();
        sl_frames(3);
        CHECK(NSPOKEN == 0, "save/load: a quick reopen does not re-announce");
        sl_frames(10);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slot 2 of 6"),
              "save/load: the silently re-seeded tracker still tracks nav");
        sl_close(); sl_frames(3);
    }

    // ---- the cleared title + Trinity Archives (VERIFIED 2026-10-24, bench arch1-arch8) --
    // The phase word is a flow COUNTER: odd phases are title
    // instances (3 from boot, 5 after one Archives visit, 7 after
    // two), even phases >= 4 are Archives instances. The cleared
    // title carries 4 rows (Trinity Archives between Continue and
    // Install Game). The Archives top level is its own widget
    // (0x099214D0: header DFFF/FFF8, cursor +0x06, count +0x0A == 5,
    // child witness +0x14: HIGH half 0x0002 while a child is open,
    // 0x0006 for two frames on a top-level cursor move, low half
    // 0x0008 at the top after any child visit). The Theater child
    // uses the shared list widget at 0x09927640 (header word +0x02
    // == 0xFFF8) with controller words (count/cursor
    // 0x09927704/08); Secret Reports has a private controller
    // cluster (cursor 0x09927C18, count 0x09927C20 read live,
    // visible-window 0x09927C22 == 10).
    {
        memset(RAM, 0, sizeof(RAM));
        a->detach();
        a->attach(&H);
        auto arch_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        static const uint32_t ARCHBASE = 0x099214D0, ARCHCHILD = 0x099214E4;
        static const uint32_t LISTHEAD = 0x09927642;
        static const uint32_t THCOUNT = 0x09927704, THCURSOR = 0x09927708;
        static const uint32_t RPCURSOR = 0x09927C18, RPCOUNT = 0x09927C20, RPWIN = 0x09927C22;
        auto title_plant = [&](uint32_t phase, uint16_t count, uint16_t cursor) {
            put_u32(TITLE_PHASE, phase);
            put_u8(TITLE_STEP, 4);
            put_f32(TITLE_FADE, 0.0f);
            put_u32(SUBMENU_PTR, 0);
            put_u16(TITLE_COUNT, count);
            put_u16(TITLE_CURSOR, cursor);
            RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
            RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;
        };
        auto arch_top = [&](uint16_t cursor) {
            put_u32(ARCHBASE, 0xFFF8DFFF);          // header words DFFF / FFF8
            put_u16(ARCHBASE + 0x0A, 5);            // the five top-level rows
            put_u16(ARCHBASE + 0x06, cursor);
            put_u32(ARCHCHILD, 0);                  // no child open
        };

        // The title AFTER an Archives visit: phase 5, cleared rows.
        title_plant(5, 4, 2);                       // Trinity Archives
        CHECK(a->ready(), "archives: ready on the title at phase 5 (returned from the Archives)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. Trinity Archives, 3 of 4"),
              "archives: the phase-5 title speaks its rows");
        title_plant(7, 4, 0);                       // second return, New Game
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Title menu. New Game, 1 of 4"),
              "archives: the phase-7 title speaks too");

        // Parity: at an even phase the title words alone must NOT
        // speak — that instance belongs to the Archives.
        title_plant(4, 4, 0);
        CHECK(!a->ready(), "archives: phase 4 with title words planted fails closed");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "archives: phase 4 speaks no title row");

        // The no-map guard: title words at an odd phase with a
        // world loaded stay silent (the field owns that state).
        title_plant(5, 4, 0);
        RAM[OFF(0x08B61CA0)] = 6;  RAM[OFF(0x08B61CA1)] = 13;
        RAM[OFF(0x08B759C0)] = 6;  RAM[OFF(0x08B759C1)] = 13;
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "archives: title words with a world loaded stay silent");
        RAM[OFF(0x08B61CA0)] = 0xFF; RAM[OFF(0x08B61CA1)] = 0xFF;
        RAM[OFF(0x08B759C0)] = 0xFF; RAM[OFF(0x08B759C1)] = 0xFF;

        // The Archives top level at phase 4.
        put_u32(TITLE_PHASE, 4);
        arch_top(0);                                // Timeline
        CHECK(a->ready(), "archives: ready on the Archives top level");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Timeline, 1 of 5"),
              "archives: WhereAmI speaks the top-level row");
        put_u16(ARCHBASE + 0x06, 1);                // Theater
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Theater, 2 of 5"),
              "archives: a nav command speaks the landed top-level row");

        // Arrival speaks once, after the stability window.
        a->detach();
        a->attach(&H);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        arch_frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Theater, 2 of 5"),
              "archives: the top level announces on arrival");
        arch_frames(10);
        CHECK(NSPOKEN == 1, "archives: the arrival line does not repeat");

        // A child open with no child anchors planted: the top level
        // must close and nothing may speak for the unknown child.
        put_u32(ARCHCHILD, 0x00020008);
        CHECK(!a->ready(), "archives: an unmapped child closes the top-level gate");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "archives: an unmapped child speaks nothing");

        // The Theater child: six stories on the shared list widget.
        put_u16(ARCHBASE + 0x06, 1);                // top cursor: Theater
        put_u16(LISTHEAD, 0xFFF8);
        put_u16(THCOUNT, 6);
        put_u16(THCURSOR, 2);                       // Aqua
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Theater. Aqua, 3 of 6"),
              "archives: WhereAmI speaks the Theater story");
        put_u16(THCOUNT, 5);                        // no such Theater exists
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "archives: a wrong Theater count fails closed");
        put_u16(THCOUNT, 6);

        // The Secret Reports child: the count word is read live.
        put_u16(ARCHBASE + 0x06, 4);                // top cursor: Secret Reports
        put_u16(RPCOUNT, 14);                       // Letter + Reports 1..13
        put_u16(RPWIN, 10);                         // the visible window
        put_u16(RPCURSOR, 0);                       // Xehanort's Letter
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Secret Reports. Xehanort's Letter, 1 of 14"),
              "archives: WhereAmI speaks the first report row");
        put_u16(RPCURSOR, 5);                       // Xehanort's Report 5
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Xehanort's Report 5, 6 of 14"),
              "archives: a nav command speaks the landed report row");

        // The second Archives visit (phase 6) speaks the top level.
        put_u32(ARCHCHILD, 0);
        put_u32(TITLE_PHASE, 6);
        put_u16(ARCHBASE + 0x06, 4);                // Secret Reports
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Secret Reports, 5 of 5"),
              "archives: the top level speaks at phase 6 too");

        // The child witness's other settled shapes must also leave
        // the top level speaking: the low half keeps 0x0008 after a
        // child round trip, and the high half pulses 0x0006 for two
        // frames on a cursor move (arch6/arch7 frame logs — the
        // first wiring's exact-equality tests failed both).
        put_u32(ARCHCHILD, 0x00000008);             // after a child visit
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Secret Reports, 5 of 5"),
              "archives: the top level still speaks with the post-child witness residue");
        put_u32(ARCHCHILD, 0x00060000);             // the cursor-move pulse
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trinity Archives. Secret Reports, 5 of 5"),
              "archives: the top level still speaks through the witness move pulse");
        put_u32(ARCHCHILD, 0);

        // A wrong top-level count fails closed.
        put_u16(ARCHBASE + 0x0A, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "not mapped yet"),
              "archives: a wrong top-level count fails closed");
    }

    // ---- field dialogue boxes (VERIFIED 2026-10-24, bench dlg1-3) ------------------
    // The event system's command records and the resident message
    // tables, planted with the bench's real layouts and strings:
    // a record is live furniture only while dormant (serial -1);
    // an activation (a small serial) speaks its message once.
    {
        auto put_bytes = [&](uint32_t a, const uint8_t* b, size_t len) {
            for (size_t i = 0; i < len; i++) RAM[OFF(a) + i] = b[i];
        };
        auto frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };

        // A message table: header, 12-byte entries, string pool.
        const uint32_t CTD = 0x08E1A2C0;
        put_u32(CTD + 0x00, 0x44544340);
        put_u32(CTD + 0x04, 1);
        put_u32(CTD + 0x08, 0x032C0000);
        put_u32(CTD + 0x10, 0x20);
        put_u32(CTD + 0x14, 0x20 + 6 * 12);
        put_u32(CTD + 0x18, 0x200);
        uint32_t pool = CTD + 0x200;
        auto plant_str = [&](const uint8_t* b, size_t len) -> uint32_t {
            uint32_t off = (uint32_t)(pool - CTD);
            put_bytes(pool, b, len);
            pool += (uint32_t) len + 1;   // strings are NUL-terminated; RAM starts zeroed
            return off;
        };
        static const uint8_t S_RING[] = {0xF9,'Y','P','r','e','s','s',' ',0xF1,0xB1,' ','t','o',' ','a','t','t','a','c','k',' ','t','h','e',' ','r','i','n','g','!',0xF9,'X','\n','K','e','e','p',' ','a','t','t','a','c','k','i','n','g',' ','t','o',' ','s','t','r','i','n','g',' ','t','o','g','e','t','h','e','r','\n','a',' ','p','o','w','e','r','f','u','l',' ','c','o','m','b','o','.'};
        static const uint8_t S_PRIZES[] = {'Y','o','u',' ','c','a','n',' ','e','a','r','n',' ',0xF9,'Y','p','r','i','z','e','s',0xF9,'X',' ','b','y',' ','d','e','f','e','a','t','i','n','g',' ','e','n','e','m','i','e','s','\n','o','r',' ','h','i','t','t','i','n','g',' ','t','h','i','n','g','s',' ','i','n',' ','t','h','e',' ','f','i','e','l','d','.',' ','G','r','e','e','n',' ','p','r','i','z','e','s','\n','r','e','s','t','o','r','e',' ','H','P','.',' ','Y','e','l','l','o','w',' ','o','n','e','s',' ','g','i','v','e',' ','y','o','u',' ',0xF9,'Y','m','u','n','n','y',0xF9,'X','.'};
        static const uint8_t S_DECK[] = {'Y','o','u','r',' ',0xF9,'P','C','o','m','m','a','n','d',' ','D','e','c','k',0xF9,'X',' ','c','o','n','t','a','i','n','s',' ','s','p','e','c','i','a','l',' ','m','o','v','e','s','.','\n','U','s','e',' ',0xF1,0xC5,' ','t','o',' ','s','e','l','e','c','t',' ','o','n','e',',',' ','t','h','e','n',' ','p','r','e','s','s',' ',0xF1,0xAE,' ','t','o','\n','u','s','e',' ','i','t',' ','o','n','c','e',' ','i','t','s',' ','g','a','u','g','e',' ','i','s',' ','f','i','l','l','e','d','.'};
        static const uint8_t S_TALK[] = {'I','\'','v','e',' ','g','o','t',' ','t','i','m','e',' ','t','o',' ','s','p','a','r','e','!'};
        static const uint8_t S_SJIS[] = {'\n','?',0x81,0x40,'?',0x81,0x40};
        // The ring string sits behind 3 NUL bytes, as on the bench:
        // the xx00 variant entry points at the NULs, xx01 at the text.
        put_bytes(pool, (const uint8_t*) "\0\0\0", 3);
        uint32_t off_ring_nul = (uint32_t)(pool - CTD);
        pool += 3;
        uint32_t off_ring = plant_str(S_RING, sizeof(S_RING));
        uint32_t off_prizes = plant_str(S_PRIZES, sizeof(S_PRIZES));
        uint32_t off_deck = plant_str(S_DECK, sizeof(S_DECK));
        uint32_t off_talk = plant_str(S_TALK, sizeof(S_TALK));
        uint32_t off_sjis = plant_str(S_SJIS, sizeof(S_SJIS));
        auto entry = [&](int i, uint32_t id, uint32_t off) {
            put_u32(CTD + 0x20 + i * 12 + 0, id);
            put_u32(CTD + 0x20 + i * 12 + 4, off);
            put_u32(CTD + 0x20 + i * 12 + 8, (uint32_t) i);
        };
        entry(0, 0x032C0100, off_ring_nul);
        entry(1, 0x032C0200, off_ring);
        entry(2, 0x032C0300, off_prizes);
        entry(3, 0x032C0400, off_deck);
        entry(4, 0x032D0100, off_talk);
        entry(5, 0x032D8500, off_sjis);
        // A name table (the shape of the resident 0x00FA table):
        // its ids must never speak through the dialogue channel.
        const uint32_t CTD2 = 0x08D93AC0;
        put_u32(CTD2 + 0x00, 0x44544340);
        put_u32(CTD2 + 0x04, 1);
        put_u32(CTD2 + 0x08, 0x00FA0000);
        put_u32(CTD2 + 0x10, 0x20);
        put_u32(CTD2 + 0x14, 0x20 + 3 * 12);
        put_u32(CTD2 + 0x18, 0x200);
        put_ascii(CTD2 + 0x200, "Attack");
        put_ascii(CTD2 + 0x210, "Surge");
        put_ascii(CTD2 + 0x220, "Fire");
        put_u32(CTD2 + 0x20 + 0, 0x00FA0001); put_u32(CTD2 + 0x20 + 4, 0x200); put_u32(CTD2 + 0x20 + 8, 0);
        put_u32(CTD2 + 0x20 + 12, 0x00FA0002); put_u32(CTD2 + 0x20 + 16, 0x210); put_u32(CTD2 + 0x20 + 20, 1);
        put_u32(CTD2 + 0x20 + 24, 0x00FA0003); put_u32(CTD2 + 0x20 + 28, 0x220); put_u32(CTD2 + 0x20 + 32, 2);

        // Command records: base word -> own +0x24, id at +0x24,
        // serial at +0x2C (0xFFFFFFFF = dormant).
        auto record = [&](uint32_t base, uint32_t id, uint32_t serial) {
            put_u32(base + 0x00, base + 0x24);
            put_u32(base + 0x24, id);
            put_u32(base + 0x28, 5);
            put_u32(base + 0x2C, serial);
        };
        const uint32_t REC1 = 0x099A4000, REC2 = 0x099A5000, REC3 = 0x099A6000, REC4 = 0x099A7000;
        record(REC1, 0x032C0200, 0xFFFFFFFF);
        record(REC2, 0x032D0100, 0xFFFFFFFF);

        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(60);
        CHECK(NSPOKEN == 0, "dialogue: dormant records stay silent");

        // The ring prompt activates (serial 3, as on the bench).
        put_u32(REC1 + 0x2C, 3);
        frames(1);
        CHECK(NSPOKEN == 0, "dialogue: a one-frame activation does not speak yet");
        frames(2);
        CHECK(NSPOKEN == 1 &&
              strcmp(SPOKEN[0], "Press Cross to attack the ring! Keep attacking to string together a powerful combo.") == 0,
              "dialogue: the ring prompt speaks, decoded, on activation");
        frames(90);
        CHECK(NSPOKEN == 1, "dialogue: an idle box does not re-speak");

        // The box closes: the record is recycled (bench: the id
        // word is overwritten and the serial goes dormant).
        put_u32(REC1 + 0x24, 0x09975610);
        put_u32(REC1 + 0x2C, 0xFFFFFFFF);
        frames(10);
        CHECK(NSPOKEN == 1, "dialogue: closing the box speaks nothing");

        // The prizes info box, on a different record (the pool
        // relocated between lessons on the bench; the census is
        // address-free).
        record(REC3, 0x032C0300, 0xFFFFFFFF);
        frames(60);
        CHECK(NSPOKEN == 1, "dialogue: a fresh dormant record stays silent");
        put_u32(REC3 + 0x2C, 5);
        frames(3);
        CHECK(NSPOKEN == 2 &&
              strcmp(SPOKEN[1], "You can earn prizes by defeating enemies or hitting things in the field. Green prizes restore HP. Yellow ones give you munny.") == 0,
              "dialogue: the prizes info speaks on its activation");

        // Advance: the deck info box follows on the recycled record.
        put_u32(REC3 + 0x24, 0x09975610);
        put_u32(REC3 + 0x2C, 0xFFFFFFFF);
        frames(10);
        record(REC3, 0x032C0400, 6);
        frames(60);
        CHECK(NSPOKEN == 3 &&
              strcmp(SPOKEN[2], "Your Command Deck contains special moves. Use D-pad to select one, then press Triangle to use it once its gauge is filled.") == 0,
              "dialogue: the deck info speaks with its icons named");

        // The talk record, dormant since the start, fires late.
        put_u32(REC2 + 0x2C, 7);
        frames(3);
        CHECK(NSPOKEN == 4 && strcmp(SPOKEN[3], "I've got time to spare!") == 0,
              "dialogue: a talk line speaks when its record activates");

        // A name-table id on an activated record: not dialogue.
        record(REC4, 0x00FA0001, 8);
        frames(60);
        CHECK(NSPOKEN == 4, "dialogue: a lone command name never speaks");

        // A string outside the proven encoding fails closed, and
        // the watcher is not wedged by consuming it.
        put_u32(REC4 + 0x24, 0x09975610);
        frames(10);
        record(REC4, 0x032D8500, 9);
        frames(60);
        CHECK(NSPOKEN == 4, "dialogue: an undecodable string stays silent");
        put_u32(REC4 + 0x24, 0x09975610);
        frames(10);
        record(REC4, 0x032D0100, 10);
        frames(60);
        CHECK(NSPOKEN == 5 && strcmp(SPOKEN[4], "I've got time to spare!") == 0,
              "dialogue: the next real activation still speaks");

        // The NUL-prefixed variant of the ring string resolves to
        // the same text under a fresh activation.
        put_u32(REC4 + 0x24, 0x09975610);
        frames(10);
        record(REC4, 0x032C0100, 11);
        frames(60);
        CHECK(NSPOKEN == 6 &&
              strcmp(SPOKEN[5], "Press Cross to attack the ring! Keep attacking to string together a powerful combo.") == 0,
              "dialogue: a variant entry decodes from its NUL prefix");
    }

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
