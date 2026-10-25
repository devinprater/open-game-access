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
void KhbbsHudGaugeCues(bool useSound);   // the sound-first seam (khbbs_adapter.cpp)
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
    // ---- foe NAME (2026-10-24, bench name1-5 + journal census) ----
    // The foe's type rides in its scene object: the object code
    // at foe block +0x240 ("m03ex00"). The scene id at +0x1B8
    // stays an instance index and is never consulted. Names are
    // the journal's (category 0x3800, entry = number + 0x80),
    // proven for numbers 1..25; everything else fails closed.
    put_u16(ACTOR4 + 0x330, 2);   // locked again
    put_u32(ACTOR4 + 0x33C, 19);
    put_ascii(FOEBLK + 0x240, "m03ex00");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Bruiser. Health 1076 of 1200. 9 away."),
          "the foe query names a proven type: Bruiser");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on: Bruiser. 9 away."),
          "the lock query names the locked type");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0],
              "Locked on. Bruiser health 1076 of 1200, 9 units away."),
          "the field line names the locked foe");
    // The lock-on edge names the new target (and only that).
    put_u16(ACTOR4 + 0x330, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->on_frame();
    put_u16(ACTOR4 + 0x330, 2);
    a->on_frame();
    {
        bool named = false;
        for (int i = 0; i < NSPOKEN; i++)
            if (strstr(SPOKEN[i], "Locked on: Bruiser.")) named = true;
        CHECK(named, "acquiring a lock names the target: Locked on: Bruiser.");
    }
    // A second Bruiser elsewhere in RAM (same type, different
    // actor, different health) changes nothing: the name is a
    // type label, the numbers stay the LOCKED actor's.
    static const uint32_t FOEBLK3 = 0x099A0000;
    put_u32(FOEBLK3 + 0x1B8, 44);
    put_ascii(FOEBLK3 + 0x240, "m03ex00");
    put_u16(FOEBLK3 + 0x380, 55);
    put_u16(FOEBLK3 + 0x382, 55);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Bruiser. Health 1076 of 1200. 9 away."),
          "a same-type bystander does not disturb the locked readout");
    // Variant suffix digits are part of the spawn variant, not
    // the type: a Glidewinder color variant still names Glidewinder.
    put_ascii(FOEBLK + 0x240, "m24ex02");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Glidewinder. Health 1076 of 1200."),
          "variant suffix digits do not change the type name");
    // The arena spawns the same types under the vs infix (live:
    // Day of Reckoning fields m01vs00 Floods and an m03vs00
    // Bruiser, bench name8 census).
    put_ascii(FOEBLK + 0x240, "m01vs00");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Flood. Health 1076 of 1200. 9 away."),
          "an arena (vs) code names the same type");
    // Fail closed, every way: a boss code (no proven key), an
    // unproven variant number, an out-of-range number, garbage.
    put_ascii(FOEBLK + 0x240, "b51ex00");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Enemy health 1076 of 1200. 9 away."),
          "a boss code speaks no name (unproven key)");
    put_ascii(FOEBLK + 0x240, "m27ex00");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Enemy health 1076 of 1200. 9 away."),
          "an unproven variant number speaks no name");
    put_ascii(FOEBLK + 0x240, "m99ex00");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Enemy health 1076 of 1200. 9 away."),
          "an out-of-range enemy number speaks no name");
    for (int i = 0; i < 8; i++) put_u8(FOEBLK + 0x240 + (uint32_t) i, 0xFF);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Enemy health 1076 of 1200. 9 away."),
          "garbage in the code slot speaks no name");
    for (int i = 0; i < 8; i++) put_u8(FOEBLK + 0x240 + (uint32_t) i, 0);
    put_u16(ACTOR4 + 0x330, 1);   // restore the unlocked end state
    put_u32(ACTOR4 + 0x33C, 0);   // the sections below start from

    // ---- multi-enemy awareness: the roster census (2026-10-25) --------
    // Complete scene objects planted the way the name worker's RAM
    // census found them: scene id at obj+0x00, the 0x00011000
    // signature at obj+0x08, the object code at obj+0x88, the HP pair
    // at (obj-0x1B8)+0x380/+0x382, and a bone palette whose root
    // matrix (candidate index 3) carries the position.
    static const uint32_t NODE2 = 0x09977000;   // ACTOR4's node
    put_u32(NODE2 + 0x04, 0x08B73DD0);
    put_u32(NODE2 + 0x0C, 0x08B73DE0);
    put_u32(NODE2 + 0x1C, 0x08B73DB4);
    put_u32(NODE2 + 0x38, ACTOR4);
    put_f32(NODE2 + 0x40, 0.0f); put_f32(NODE2 + 0x44, 1.0f); put_f32(NODE2 + 0x48, 0.0f);
    auto plant_matrix = [&](uint32_t at, float x, float y, float z) {
        put_f32(at + 0x00, 1.0f); put_f32(at + 0x04, 0.0f); put_f32(at + 0x08, 0.0f); put_f32(at + 0x0C, 0.0f);
        put_f32(at + 0x10, 0.0f); put_f32(at + 0x14, 1.0f); put_f32(at + 0x18, 0.0f); put_f32(at + 0x1C, 0.0f);
        put_f32(at + 0x20, 0.0f); put_f32(at + 0x24, 0.0f); put_f32(at + 0x28, 1.0f); put_f32(at + 0x2C, 0.0f);
        put_f32(at + 0x30, x);    put_f32(at + 0x34, y);    put_f32(at + 0x38, z);    put_f32(at + 0x3C, 1.0f);
    };
    auto plant_foe = [&](uint32_t obj, uint32_t id, const char* code,
                         uint16_t cur, uint16_t mx, bool palette,
                         float px, float py, float pz) {
        put_u32(obj + 0x00, id);
        put_u32(obj + 0x08, 0x00011000);
        put_ascii(obj + 0x88, code);
        put_u16(obj - 0x1B8 + 0x380, cur);
        put_u16(obj - 0x1B8 + 0x382, mx);
        if (palette) {
            for (int k = 0; k < 5; k++) {
                float ox = 20.0f + 10.0f * (float) k, oz = 30.0f + 5.0f * (float) k;
                if (k == 3) { ox = px; oz = pz; }
                plant_matrix(obj + 0x700 + 0x40 * (uint32_t) k, ox, py, oz);
            }
        }
    };
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Enemies);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No enemies."),
          "an empty field answers the roster query with no enemies");
    // The pack: a boss (counted, unnamed), a Bruiser, a Flood, and a
    // Thornbite whose palette is unreadable (counted, no distance).
    static const uint32_t FOE1 = 0x099B0000, FOE2 = 0x099B4000,
                          FOE3 = 0x099B8000, FOE4 = 0x099BC000;
    plant_foe(FOE1, 51, "m03vs00", 75, 75, true, 4.0f, 1.0f, 0.0f);    // Bruiser, 4 away
    plant_foe(FOE2, 52, "m01vs00", 25, 25, true, 0.0f, 1.0f, -6.0f);   // Flood, 6 away
    plant_foe(FOE3, 53, "b10vs00", 1200, 1200, true, 0.0f, 1.0f, 3.0f); // boss, 3 away
    plant_foe(FOE4, 54, "m06vs00", 60, 60, false, 0, 0, 0);            // Thornbite, no position
    // Excluded by the validation rules, each HP-shaped on purpose:
    plant_foe(0x099C0000, 55, "g12vs00", 100, 100, false, 0, 0, 0);    // a prop ring
    plant_foe(0x099C4000, 56, "m02vs00", 0, 50, false, 0, 0, 0);       // a dead foe
    plant_foe(0x099C8000, 57, "m0xvs00", 50, 50, false, 0, 0, 0);      // a broken code
    plant_foe(0x099CC000, 58, "p13ex00", 100, 100, false, 0, 0, 0);    // the player
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Enemies);
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[0], "4 enemies."),
          "the roster counts only validated live foes (props, the dead, broken codes and the player excluded)");
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[1], "Enemy, 3 away."),
          "the roster is nearest-first; an unnamed boss speaks as Enemy");
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[2], "Bruiser, 4 away."),
          "the roster names a proven type with its distance");
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[3], "Flood, 6 away."),
          "the roster orders by distance");
    CHECK(NSPOKEN == 5 && strcmp(SPOKEN[4], "Thornbite.") == 0,
          "a foe with no readable palette is named but claims no distance");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "4 enemies."),
          "Where Am I carries the enemy count");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not locked on. 4 enemies."),
          "the foe query while unlocked answers with the count");
    // Lock the Bruiser: the roster marks it, the foe query counts
    // the others.
    put_u16(ACTOR4 + 0x330, 2);
    put_u32(ACTOR4 + 0x33C, 51);
    put_u32(REC + 0x3C, FOE1);
    put_f32(REC + 0x40, 0.0f); put_f32(REC + 0x44, 1.0f); put_f32(REC + 0x48, 0.0f);
    put_f32(REC + 0x50, 4.0f); put_f32(REC + 0x54, 1.0f); put_f32(REC + 0x58, 0.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Bruiser. Health 75 of 75. 4 away. 3 other enemies."),
          "the locked foe query appends how many others are in the fight");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Enemies);
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[2], "Bruiser, 4 away, locked on."),
          "the roster marks the locked foe");
    put_u16(ACTOR4 + 0x330, 1);   // unlock again
    put_u32(ACTOR4 + 0x33C, 0);
    // Movement-derived facing turns distances into clock bearings:
    // a leg in +x makes the facing (1, 0); the player ends at (3,1,0).
    a->on_frame();
    put_f32(NODE2 + 0x40, 3.0f);
    a->on_frame();
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::Enemies);
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[1], "Bruiser, 1 away, straight ahead."),
          "with a live facing, the roster speaks clock bearings");
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[2], "Enemy, 4 away, 8 o'clock."),
          "bearings are relative to the movement-derived facing");
    CHECK(NSPOKEN == 5 && strstr(SPOKEN[3], "Flood, 7 away, 4 o'clock."),
          "bearings and distances track the player's new position");
    // Retire every plant: sig and code zeroed (the census keys on
    // both), the node descriptors dropped, the actor left unlocked.
    {
        static const uint32_t kPlants[] = {FOE1, FOE2, FOE3, FOE4,
            0x099C0000, 0x099C4000, 0x099C8000, 0x099CC000};
        for (uint32_t obj : kPlants) {
            put_u32(obj + 0x08, 0);
            for (int i = 0; i < 8; i++) put_u8(obj + 0x88 + (uint32_t) i, 0);
        }
    }
    put_u32(NODE2 + 0x04, 0); put_u32(NODE2 + 0x0C, 0);
    put_u32(NODE2 + 0x1C, 0); put_u32(NODE2 + 0x38, 0);
    // The lock record's target and positions belong to the cue
    // section below, which inherits the name section's plant.
    put_u32(REC + 0x3C, FOEBLK + 0x1B8);
    put_f32(REC + 0x40, -5.28f); put_f32(REC + 0x44, 1.11f); put_f32(REC + 0x48, -0.21f);
    put_f32(REC + 0x50, 3.69f);  put_f32(REC + 0x54, 1.16f); put_f32(REC + 0x58, 0.63f);
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
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Your health 50 percent."),
          "low HP: at 26 percent the warning stays silent (the 50 milestone speaks)");

    put_u16(ACTOR5 + 0x380, 51);       // 25%: below the game's line
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
              strstr(SPOKEN[1], "Your health 25 percent."),
          "low HP: crossing below the line speaks Low health once (plus the 25 milestone)");

    put_u16(ACTOR5 + 0x380, 30);       // deeper damage, still low
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 0, "low HP: more damage while low does not repeat it");

    put_u16(ACTOR5 + 0x380, 61);       // healed to 30%: above the game
    lowhp_frames();                    // line, below the re-arm line
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Your health 25 percent."),
          "low HP: a shallow heal does not re-arm the warning (the 25 milestone re-arms at 30)");

    put_u16(ACTOR5 + 0x380, 62);       // healed to 31%: re-armed
    lowhp_frames();
    put_u16(ACTOR5 + 0x380, 51);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    lowhp_frames();
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
              strstr(SPOKEN[1], "Your health 25 percent."),
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
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
              strstr(SPOKEN[1], "Your health 25 percent."),
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
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
              strstr(SPOKEN[1], "Your health 25 percent."),
          "low HP: healed and dropped in the field, it speaks");
    put_u16(ACTOR5 + 0x380, 0);        // retire the low-HP plant
    put_u16(ACTOR5 + 0x382, 0);
    put_u16(ACTOR5 + 0x330, 0);
    lowhp_frames();

    // ---- battle gauges: Focus / shotlock + D-Link (2026-10-24) --------------
    // Signals from bench khbbs-foc1..foc7 (see the adapter's
    // constants block): the gauges object hangs off the fixed
    // global u32 @ 0x08B73BB8; f32 D-Link @ +0x3C, f32 Focus
    // @ +0x44, state @ +0x50 (1 aiming, 2 firing), flags @ +0x54
    // (0x10000 Focus full, 0x100 D-Link primed, 0x20000 aiming,
    // 0x200 linked), partner def @ +0x80 (0 = none; 383 = Ventus
    // in the game's def table, name pointer at def+8).
    const uint32_t ACTOR6 = 0x09948000, GAUGES = 0x099A4000,
                   GNAME = 0x099A5000;
    put_u16(ACTOR6 + 0x380, 149);      // cur HP
    put_u16(ACTOR6 + 0x382, 149);      // max HP
    put_u16(ACTOR6 + 0x226, 0);        // gauge
    put_u16(ACTOR6 + 0x330, 1);        // mark (not locked on)
    put_u32(ACTOR6 + 0x234, 0x8068);   // flags (bit 0x20 set)
    put_u32(ACTOR6 + 0x220, 1);        // actor state
    put_u32(ACTOR6 + 0x58, 0x099816A0);// command cluster pointer
    put_u32(0x08B73BB8, GAUGES);       // the gauges global
    put_f32(GAUGES + 0x3C, 40.0f);     // D-Link gauge
    put_f32(GAUGES + 0x44, 40.0f);     // Focus gauge
    put_u32(GAUGES + 0x50, 0);         // gauges state: idle
    put_u32(GAUGES + 0x54, 0);         // flags: nothing set
    put_u16(GAUGES + 0x80, 0);         // no link partner
    auto gauges_frames = [&]() { for (int i = 0; i < 4; i++) a->on_frame(); };

    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: first sight primes silently");

    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Focus 40.") &&
              strstr(SPOKEN[0], "D-Link 40."),
          "battle gauges: the field line reports Focus and D-Link");

    put_f32(GAUGES + 0x44, 100.0f);
    put_u32(GAUGES + 0x54, 0x10000);   // the game sets its full bit
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0,
          "battle gauges: Focus reaching full stays silent in sound mode");
    { oga::CueSnapshot snap{}; a->cue_snapshot(&snap);
      CHECK(snap.focus_cue && !snap.dlink_cue,
            "battle gauges: Focus reaching full raises the Focus earcon"); }
    { oga::CueSnapshot snap{}; a->cue_snapshot(&snap);
      CHECK(!snap.focus_cue,
            "battle gauges: the Focus earcon fires exactly once"); }
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: staying full does not repeat");

    put_u32(GAUGES + 0x54, 0x10100);   // D-Link gauge filled too
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0,
          "battle gauges: a full D-Link gauge stays silent in sound mode");
    { oga::CueSnapshot snap{}; a->cue_snapshot(&snap);
      CHECK(snap.dlink_cue && !snap.focus_cue,
            "battle gauges: a full D-Link gauge raises the D-Link earcon"); }

    // Shotlock: lock on first (the mark's lock edge speaks), then
    // aim — mark 4 must hold the field and the lock, not drop them.
    put_u16(ACTOR6 + 0x330, 2);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Locked on."),
          "battle gauges: locking on still announces before the aim");
    put_u16(ACTOR6 + 0x330, 4);        // aiming mark
    put_u32(ACTOR6 + 0x220, 24);       // aiming actor state
    put_u32(GAUGES + 0x50, 1);         // gauges state: aiming
    put_f32(GAUGES + 0x44, 73.0f);     // Focus drained by locks
    put_u32(GAUGES + 0x54, 0x20100);   // aim bit up, full bit gone
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Shotlock."),
          "battle gauges: entering the aim speaks Shotlock once");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Focus 73."),
          "battle gauges: the field still answers during the aim (mark 4)");
    // Fire: Cross — the firing state lands the same frame the aim
    // bit falls, and Focus stays spent. The game sounds the shot
    // itself (bench audio audit: the fire transition peaks at full
    // scale), so the adapter now says NOTHING for a fired shotlock.
    put_u32(GAUGES + 0x50, 2);
    put_u32(GAUGES + 0x54, 0x100);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0,
          "battle gauges: a fired shotlock stays silent (the game sounds it)");
    put_u32(GAUGES + 0x50, 0);
    put_u16(ACTOR6 + 0x330, 2);        // aim over, lock still held
    put_u32(ACTOR6 + 0x220, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: no Locked on re-announce after a shotlock");
    // Mark 4 alone is not the player's — a foe block carries it
    // in the Radiant Garden fight dumps — so without the gauges
    // object's aim flag it must fail the field gate outright.
    put_u16(ACTOR6 + 0x330, 4);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not mapped yet"),
          "battle gauges: mark 4 without the aim flag fails the field gate");
    put_u16(ACTOR6 + 0x330, 2);
    gauges_frames();
    // Cancel: aim again, release without firing — Focus refunds
    // to full, so the full bit rises on the same frame the aim
    // falls: the refund becomes the Focus earcon, the cancel keeps
    // its spoken line.
    put_u16(ACTOR6 + 0x330, 4);
    put_u32(ACTOR6 + 0x220, 24);
    put_u32(GAUGES + 0x50, 1);
    put_u32(GAUGES + 0x54, 0x20100);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Shotlock."),
          "battle gauges: a second aim speaks Shotlock again");
    put_u16(ACTOR6 + 0x330, 2);
    put_u32(ACTOR6 + 0x220, 1);
    put_u32(GAUGES + 0x50, 0);
    put_f32(GAUGES + 0x44, 100.0f);    // refunded
    put_u32(GAUGES + 0x54, 0x10100);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Shotlock cancelled."),
          "battle gauges: a cancelled aim speaks only the cancel");
    { oga::CueSnapshot snap{}; a->cue_snapshot(&snap);
      CHECK(snap.focus_cue,
            "battle gauges: the cancel's refund raises the Focus earcon"); }

    // D-Link activation without a resolvable partner name first:
    // the honest fallback line.
    put_u32(GAUGES + 0x54, 0x10300);   // linked bit up
    put_u16(GAUGES + 0x80, 383);       // Ventus's def (name not planted yet)
    put_f32(GAUGES + 0x3C, 99.9f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Link active."),
          "battle gauges: an unnamed link speaks the fallback line");
    put_u32(GAUGES + 0x54, 0x10000);   // link over (primed + linked gone)
    put_u16(GAUGES + 0x80, 0);
    put_f32(GAUGES + 0x3C, 0.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Link ended."),
          "battle gauges: the link ending speaks D-Link ended");
    // With the def name planted, the activation names the partner.
    // (Refill first, as the game sequences it: the primed bit
    // rises at the fill, the linked bit later at the confirm.)
    memcpy(&RAM[OFF(GNAME)], "Ventus", 7);
    put_u32(0x08B34924 + 383 * 16 + 8, GNAME);
    put_f32(GAUGES + 0x3C, 100.0f);
    put_u32(GAUGES + 0x54, 0x10100);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0,
          "battle gauges: refilling after a link stays silent in sound mode");
    { oga::CueSnapshot snap{}; a->cue_snapshot(&snap);
      CHECK(snap.dlink_cue,
            "battle gauges: refilling after a link raises the earcon again"); }
    put_u32(GAUGES + 0x54, 0x10300);
    put_u16(GAUGES + 0x80, 383);
    put_f32(GAUGES + 0x3C, 99.9f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Link: Ventus."),
          "battle gauges: activation names the partner from the def table");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Linked to Ventus, D-Link 100."),
          "battle gauges: the field line names the active link");
    put_u32(GAUGES + 0x54, 0x10000);
    put_u16(GAUGES + 0x80, 0);
    put_f32(GAUGES + 0x3C, 0.0f);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Link ended."),
          "battle gauges: the named link ending speaks too");

    // Menu silence: an edge first seen under the camp menu is
    // consumed silently and never fires late (low-HP discipline).
    put_f32(GAUGES + 0x44, 40.0f);
    put_u32(GAUGES + 0x54, 0);         // Focus spent again, bits clear
    gauges_frames();
    put_u32(0x08B5BEF8, 8);            // camp witnesses up
    put_u32(0x08BD194C, 2);
    put_f32(GAUGES + 0x44, 100.0f);
    put_u32(GAUGES + 0x54, 0x10000);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: a fill under the camp menu stays silent");
    put_u32(0x08B5BEF8, 0);            // camp closes
    put_u32(0x08BD194C, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: closing camp does not fire it late");

    // No object: the watch and the readout both stand down.
    put_u32(0x08B73BB8, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    gauges_frames();
    CHECK(NSPOKEN == 0, "battle gauges: no object, no speech");
    NSPOKEN = 0; oga::AdapterSpeechReset();
    a->command(oga::Command::PrevEnemy);
    CHECK(NSPOKEN == 1 && !strstr(SPOKEN[0], "Focus"),
          "battle gauges: without the object the field line omits the gauges");
    put_u32(0x08B34924 + 383 * 16 + 8, 0);   // retire the name plant
    put_u16(ACTOR6 + 0x380, 0);              // retire the actor plant
    put_u16(ACTOR6 + 0x382, 0);
    put_u16(ACTOR6 + 0x330, 0);
    gauges_frames();

    // ---- sound-first health cues (2026-10-24) ----------------------------------
    // The owner's rule: hits are sounded by the game already, so a
    // hit raises a pitch-mapped beep through the cue snapshot
    // (foe RIGHT +0.8, 1200->300 Hz; player LEFT -0.8, 800->200 Hz,
    // both logarithmic) and speech keeps only the milestone
    // crossings 75/50/25/10 with heal re-arm at +5. Max 200 for the
    // player (75%=150, 50%=100, 25%=50, 10%=20) and 1200 for the
    // foe, like the foe-readout section.
    const uint32_t ACTOR7 = 0x0994C000, REC2 = 0x0998D000,
                   FOEBLK2 = 0x0998E000;
    put_u16(ACTOR7 + 0x380, 200);      // cur HP
    put_u16(ACTOR7 + 0x382, 200);      // max HP
    put_u16(ACTOR7 + 0x226, 0);        // gauge
    put_u16(ACTOR7 + 0x330, 2);        // mark (locked on)
    put_u32(ACTOR7 + 0x234, 0x8068);   // flags (bit 0x20 set)
    put_u32(ACTOR7 + 0x220, 1);        // actor state
    put_u32(ACTOR7 + 0x58, 0x099816A0);// command cluster pointer
    put_u32(ACTOR7 + 0x33C, 21);       // the actor's engagement id
    put_u32(REC2 + 0x38, ACTOR7);      // the record points back
    put_u32(REC2 + 0x3C, FOEBLK2 + 0x1B8);
    for (int i = 0; i < 3; i++) put_f32(REC2 + 0x40 + (uint32_t) i * 4, 0.0f);
    put_f32(REC2 + 0x50, 5.0f); put_f32(REC2 + 0x54, 0.0f); put_f32(REC2 + 0x58, 0.0f);
    put_u32(FOEBLK2 + 0x1B8, 21);      // the target's scene word = the id
    put_u16(FOEBLK2 + 0x380, 1200);
    put_u16(FOEBLK2 + 0x382, 1200);
    auto sf_frames = [&]() { for (int i = 0; i < 4; i++) a->on_frame(); };
    oga::CueSnapshot ssnap{};

    sf_frames();   // primes both tracks (and speaks "Locked on.")
    a->cue_snapshot(&ssnap);           // trigger the record locate + drain
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(!ssnap.foe_beep && !ssnap.own_beep && !ssnap.focus_cue && !ssnap.dlink_cue,
          "health cues: a quiet fight raises nothing");

    // Own side: a plain hit beeps (pitched from the NEW fraction,
    // panned LEFT), with no speech.
    put_u16(ACTOR7 + 0x380, 190);      // 95%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(ssnap.own_beep && !ssnap.foe_beep && ssnap.own_beep_pan == -0.8f &&
              ssnap.own_beep_hz > 730.0f && ssnap.own_beep_hz < 765.0f,
          "health cues: a hit on the player beeps left at 95 percent pitch");
    CHECK(NSPOKEN == 0, "health cues: a plain hit says nothing");
    a->cue_snapshot(&ssnap);
    CHECK(!ssnap.own_beep, "health cues: the beep is consumed exactly once");
    // The milestone crossings speak, once each.
    put_u16(ACTOR7 + 0x380, 150);      // 75%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Your health 75 percent."),
          "health cues: crossing 75 speaks the milestone");
    put_u16(ACTOR7 + 0x380, 149);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 0, "health cues: drifting below a milestone does not repeat it");
    // Heal past milestone + 5 and the milestone re-arms.
    put_u16(ACTOR7 + 0x380, 160);      // 80%
    sf_frames();
    put_u16(ACTOR7 + 0x380, 150);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Your health 75 percent."),
          "health cues: healing past 80 re-arms the 75 milestone");
    // A plunge speaks only the LOWEST crossed milestone (the
    // low-HP warning, an older watch, fires alongside).
    put_u16(ACTOR7 + 0x380, 45);       // 22%: crosses 50 and 25
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
              strstr(SPOKEN[1], "Your health 25 percent."),
          "health cues: a plunge speaks only the lowest crossed milestone");
    put_u16(ACTOR7 + 0x380, 18);       // 9%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Your health nearly gone."),
          "health cues: crossing 10 speaks nearly gone");
    // Death raises nothing at all.
    put_u16(ACTOR7 + 0x380, 200);
    sf_frames();
    put_u16(ACTOR7 + 0x380, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 0, "health cues: death stays silent");
    put_u16(ACTOR7 + 0x380, 200);
    sf_frames();

    // Foe side, locked: hits beep RIGHT at the foe's pitch; the
    // milestone ladder speaks in order. (The death above cleared
    // the cached lock record — the first poll re-locates it and a
    // fresh set of frames primes the foe track at full HP.)
    sf_frames();
    a->cue_snapshot(&ssnap);
    sf_frames();
    put_u16(FOEBLK2 + 0x380, 1100);    // 91%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(ssnap.foe_beep && !ssnap.own_beep && ssnap.foe_beep_pan == 0.8f &&
              ssnap.foe_beep_hz > 1040.0f && ssnap.foe_beep_hz < 1100.0f,
          "health cues: a hit on the foe beeps right at 91 percent pitch");
    CHECK(NSPOKEN == 0, "health cues: a plain foe hit says nothing");
    put_u16(FOEBLK2 + 0x380, 900);     // 75%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe health 75 percent."),
          "health cues: foe crossing 75 speaks");
    put_u16(FOEBLK2 + 0x380, 500);     // 41%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe health 50 percent."),
          "health cues: foe crossing 50 speaks");
    put_u16(FOEBLK2 + 0x380, 200);     // 16%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe health 25 percent."),
          "health cues: foe crossing 25 speaks");
    put_u16(FOEBLK2 + 0x380, 100);     // 8%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe nearly gone."),
          "health cues: foe crossing 10 speaks nearly gone");
    // Heal to 58%: 50/25/10 re-arm (75 does not), so the next drop
    // past 50 speaks 50 again — and only 50.
    put_u16(FOEBLK2 + 0x380, 700);
    sf_frames();
    put_u16(FOEBLK2 + 0x380, 590);     // 49%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe health 50 percent."),
          "health cues: a healed foe re-arms the milestones it passed");
    a->cue_snapshot(&ssnap);
    CHECK(ssnap.foe_beep, "health cues: the re-arm drop beeps too");
    // The killing blow raises nothing.
    put_u16(FOEBLK2 + 0x380, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(NSPOKEN == 0 && !ssnap.foe_beep,
          "health cues: the killing blow stays silent");
    // A foe first tracked mid-fight starts fresh: milestones above
    // its current percent stay silent.
    put_u16(FOEBLK2 + 0x380, 480);     // 40%
    sf_frames();
    put_u16(FOEBLK2 + 0x380, 290);     // 24%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe health 25 percent."),
          "health cues: a foe joined at 40 percent speaks only 25 on the way down");
    // Unlocked: the last locked foe is still followed through its
    // block after the record stops validating.
    put_u16(ACTOR7 + 0x330, 1);        // unlock
    put_u32(REC2 + 0x38, 0);           // the record lets go of the actor
    sf_frames();
    put_u16(FOEBLK2 + 0x380, 100);     // 8%
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Foe nearly gone.") && ssnap.foe_beep,
          "health cues: the last locked foe is followed while unlocked");
    // Once the block stops validating (scene word gone), tracking ends.
    put_u32(FOEBLK2 + 0x1B8, 0);
    sf_frames();
    put_u16(FOEBLK2 + 0x380, 50);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(NSPOKEN == 0 && !ssnap.foe_beep,
          "health cues: a foe whose block stops validating is dropped");

    // The settings seam: speech mode brings the gauge lines back
    // and raises no earcons; sound mode is the default restored.
    const uint32_t GAUGES2 = 0x099A6000;
    put_u32(0x08B73BB8, GAUGES2);
    put_f32(GAUGES2 + 0x3C, 40.0f);
    put_f32(GAUGES2 + 0x44, 40.0f);
    put_u32(GAUGES2 + 0x50, 0);
    put_u32(GAUGES2 + 0x54, 0);
    put_u16(GAUGES2 + 0x80, 0);
    sf_frames();                       // the watch takes the new object
    oga::KhbbsHudGaugeCues(false);
    put_f32(GAUGES2 + 0x44, 100.0f);
    put_u32(GAUGES2 + 0x54, 0x10000);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Focus full.") && !ssnap.focus_cue,
          "health cues: speech mode speaks Focus full and raises no earcon");
    put_u32(GAUGES2 + 0x54, 0x10100);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "D-Link ready."),
          "health cues: speech mode speaks D-Link ready");
    oga::KhbbsHudGaugeCues(true);
    put_u32(GAUGES2 + 0x54, 0);
    sf_frames();
    put_u32(GAUGES2 + 0x54, 0x10000);
    NSPOKEN = 0; oga::AdapterSpeechReset();
    sf_frames();
    a->cue_snapshot(&ssnap);
    CHECK(NSPOKEN == 0 && ssnap.focus_cue,
          "health cues: sound mode restored, the earcon is back");
    put_u32(0x08B73BB8, 0);             // retire the gauges plant
    put_u16(ACTOR7 + 0x380, 0);         // retire the actor plant
    put_u16(ACTOR7 + 0x382, 0);
    put_u16(ACTOR7 + 0x330, 0);
    sf_frames();

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

    // ---- landmark Guide beyond the Hub (tables SURVEYED 2026-10-24) -------------
    // The surveyed rooms of Olympus Coliseum and Radiant Garden:
    // every position is the walked survey's (guide-progress;
    // per-room notes in games/kh-bbs/). Each room is asked from a
    // stand the survey actually stood on, first with no facing
    // (fresh attach) so the lines are distance-only, and two
    // rooms get a movement leg to pin the bearing math outside
    // the Hub. Unsurveyed rooms must stay silent.
    {
        auto guide_room = [&](unsigned w, unsigned r, float px, float pz) {
            RAM[OFF(0x08B61CA0)] = (uint8_t) w; RAM[OFF(0x08B61CA1)] = (uint8_t) r;
            RAM[OFF(0x08B759C0)] = (uint8_t) w; RAM[OFF(0x08B759C1)] = (uint8_t) r;
            a->detach();
            a->attach(&H);                          // fresh caches, no facing yet
            put_f32(NODE + 0x40, px); put_f32(NODE + 0x48, pz);
            NSPOKEN = 0; oga::AdapterSpeechReset();
            a->command(oga::Command::Guide);
        };

        guide_room(8, 2, -2.09f, 2.75f);            // Vestibule, at the save point
        CHECK(NSPOKEN == 6 && strstr(SPOKEN[0], "Save point, right here.") &&
                  strstr(SPOKEN[1], "Moogle Shop, 5 away.") &&
                  strstr(SPOKEN[2], "Large chest, 7 away."),
              "Vestibule guide: save point, Moogle and chest from the save stand");
        CHECK(strstr(SPOKEN[3], "Hercules, 7 away.") &&
                  strstr(SPOKEN[4], "Phil, 7 away.") &&
                  strstr(SPOKEN[5], "Coliseum Gates, 4 away."),
              "Vestibule guide: Hercules, Phil and the door back, named by destination");
        CHECK(!strstr(SPOKEN[1], "o'clock") && !strstr(SPOKEN[5], "o'clock"),
              "Vestibule guide: no facing yet, no bearings");
        a->on_frame();                              // anchor at the save stand
        put_f32(NODE + 0x48, 4.75f);                // a 2-unit leg toward the door (+z)
        a->on_frame();                              // facing (0, 1)
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::Guide);
        CHECK(strstr(SPOKEN[0], "Save point, 2 away, 6 o'clock.") &&
                  strstr(SPOKEN[5], "Coliseum Gates, 2 away, 2 o'clock."),
              "Vestibule guide: bearings ride the movement facing");

        guide_room(8, 1, 0.0f, -11.39f);            // Coliseum Gates, the entrance
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[0], "Small chest, 36 away.") &&
                  strstr(SPOKEN[1], "Vestibule, 4 away.") &&
                  strstr(SPOKEN[2], "Town Near Thebes, 32 away."),
              "Coliseum Gates guide: the reachable chest and both doors");

        guide_room(8, 5, 0.0f, -9.99f);             // Town Near Thebes, the entrance
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Coliseum Gates, 6 away."),
              "Thebes guide: the walked street holds only the door back");

        guide_room(6, 2, -5.64f, 3.16f);            // Entryway, at the save point
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[0], "Save point, right here.") &&
                  strstr(SPOKEN[1], "Outer Gardens, 12 away.") &&
                  strstr(SPOKEN[2], "Central Square, 17 away."),
              "Entryway guide: save point and both doors");

        guide_room(6, 3, 0.0f, 39.74f);             // Central Square, north entrance
        CHECK(NSPOKEN == 4 && strstr(SPOKEN[0], "Small chest, 42 away.") &&
                  strstr(SPOKEN[1], "Small chest, 66 away.") &&
                  strstr(SPOKEN[2], "Entryway, 4 away.") &&
                  strstr(SPOKEN[3], "Castle Town, 52 away."),
              "Central Square guide: both chests and the two proven doors");
        a->on_frame();                              // anchor at the entrance
        put_f32(NODE + 0x48, 36.74f);               // a 3-unit leg into the square (-z)
        a->on_frame();                              // facing (0, -1)
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::Guide);
        CHECK(strstr(SPOKEN[0], "Small chest, 40 away, 2 o'clock.") &&
                  strstr(SPOKEN[2], "Entryway, 7 away, 6 o'clock."),
              "Central Square guide: bearings from a -z facing");

        guide_room(6, 5, -8.99f, 20.00f);           // Castle Town, the west entrance
        CHECK(NSPOKEN == 6 && strstr(SPOKEN[0], "Save point, 31 away.") &&
                  strstr(SPOKEN[1], "Moogle Shop, 27 away.") &&
                  strstr(SPOKEN[2], "Large chest, 31 away."),
              "Castle Town guide: save point, Moogle and chest");
        CHECK(strstr(SPOKEN[3], "Merlin's House, 26 away.") &&
                  strstr(SPOKEN[4], "Central Square, 4 away.") &&
                  strstr(SPOKEN[5], "Fountain Court, 48 away."),
              "Castle Town guide: the house door and both gates, by destination");

        guide_room(6, 8, -3.84f, 4.18f);            // Merlin's House, the doorway
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[0], "Merlin, 6 away.") &&
                  strstr(SPOKEN[1], "Small chest, 3 away.") &&
                  strstr(SPOKEN[2], "Castle Town, 4 away."),
              "Merlin's House guide: Merlin, the chest and the door back");

        guide_room(6, 7, 42.00f, 32.24f);           // Fountain Court balcony
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Castle Town, 4 away."),
              "Fountain Court guide: the railed balcony names only its door");

        guide_room(6, 12, 0.0f, -119.24f);          // Outer Gardens, the far end
        CHECK(NSPOKEN == 4 && strstr(SPOKEN[0], "Small chest, 97 away.") &&
                  strstr(SPOKEN[1], "Small chest, 146 away.") &&
                  strstr(SPOKEN[2], "Purification Facility, 116 away.") &&
                  strstr(SPOKEN[3], "Entryway, 4 away."),
              "Outer Gardens guide: both chests and both doors the length of the garden");

        guide_room(6, 11, 45.00f, -17.99f);         // Purification Facility (unsurveyed)
        CHECK(NSPOKEN == 0, "Guide is silent in the unsurveyed Purification Facility");
        guide_room(1, 5, 29.11f, 41.58f);           // Mountain Path (unsurveyed)
        CHECK(NSPOKEN == 0, "Guide is silent on the unsurveyed Mountain Path");
        guide_room(8, 3, 0.0f, 0.0f);               // West Bracket (unsurveyed)
        CHECK(NSPOKEN == 0, "Guide is silent in an unsurveyed arena room");

        guide_room(8, 2, -2.09f, 2.75f);            // back to the Vestibule
        put_u32(0x08B5BEF8, 8);                     // camp opens over a surveyed room
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::Guide);
        CHECK(NSPOKEN == 0, "Guide is silent in camp in a surveyed room too");
        put_u32(0x08B5BEF8, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::Guide);
        CHECK(NSPOKEN == 6, "the room guide speaks again once camp closes");

        RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;   // restore the Hub
        RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 1;
    }

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

        // Home B's screen-3 block is the mode child itself
        // (the g10 section read it as a ghost before the
        // child was wired): the TOP gate stays shut for it
        // while the mode gate speaks it as the mode list.
        plant_ev(EVB, 3, 1, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ev_frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "New Event. Arena Mode, 2 of 4."),
              "mode menu: the screen-3 child announces as the mode list, not the top menu");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. New Event. Arena Mode, 2 of 4."),
              "mode menu: WhereAmI names the mode list and its row");

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

    // ---- the New Event mode list (VERIFIED 2026-10-25, bench khbbs-cb1) -----
    // The top menu's child: Versus Mode / Arena Mode /
    // Command Board / Rumble Racing, opening on Arena Mode,
    // wrapping both ways. It is the SAME widget family as the
    // top menu with screen word 3 (its count word reads 0;
    // the cursor is bounded by the four walked rows). The
    // widget floats: cb1 found the child at 0x0991B5B8 and,
    // after an Arena setup round trip, the top menu rebuilt
    // at 0x0991E278 — neither is a g10 home, and the old
    // two-home gate answered the field line over the rebuilt
    // menu. The scan (sticky cache, uniqueness required) is
    // what these plants exercise: a relocation home neither
    // section above uses, each witness knocked out, two
    // shaped widgets at once, and the stale widget under a
    // dead flow word.
    {
        static const uint32_t EVC = 0x0991E278;   // cb1: top menu after a setup trip
        static const uint32_t EVD = 0x0991B5B8;   // cb1: the mode child's home
        static const uint32_t EVFLOW = 0x08B5BEF8;
        put_u32(0x08BD194C, 0);   // camp witness B dead for this section
        a->detach();
        a->attach(&H);
        auto md_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        auto plant_md = [&](uint32_t base, uint16_t scr, uint16_t cur, uint16_t cnt) {
            put_u32(EVFLOW, 8);
            put_u16(base + 0x00, scr);
            put_u16(base + 0x04, 15);
            put_u16(base + 0x08, 0xDBFF);
            put_u16(base + 0x0A, 0xFFF8);
            put_u16(base + 0x0C, 0);
            put_u16(base + 0x0E, cur);
            put_u16(base + 0x12, cnt);
        };
        auto zero_md = [&](uint32_t base) { plant_md(base, 0, 0, 0); put_u32(EVFLOW, 8); };

        // The mode widget at the relocation home, opening on
        // Arena Mode: the arrival speaks once, after the
        // cursor settles (see the transient check below).
        plant_md(EVC, 3, 1, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "New Event. Arena Mode, 2 of 4."),
              "mode menu: arrival speaks the list and its opening row");
        md_frames(2);
        CHECK(NSPOKEN == 1, "mode menu: the arrival line does not repeat");
        CHECK(a->ready(), "mode menu: ready while the list is up");

        // Nav echoes read the live cursor, wraps included.
        plant_md(EVC, 3, 2, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board, 3 of 4"),
              "mode menu: nav speaks the landed row (Command Board)");
        plant_md(EVC, 3, 3, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Rumble Racing, 4 of 4"),
              "mode menu: nav speaks the landed row (Rumble Racing)");
        plant_md(EVC, 3, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuPrev);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Versus Mode, 1 of 4"),
              "mode menu: nav speaks the wrapped row (Versus Mode)");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. New Event. Versus Mode, 1 of 4."),
              "mode menu: WhereAmI names the list and the current row");

        // The cb3 transient: the real widget validates with
        // cursor 0 for ~4 frames before the game writes its
        // start row (Arena Mode). A few frames at the build
        // row must not announce; the settled row announces.
        zero_md(EVC);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(40);
        plant_md(EVC, 3, 0, 0);
        md_frames(3);
        CHECK(NSPOKEN == 0, "mode menu: the build-row transient does not announce");
        plant_md(EVC, 3, 1, 0);
        md_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "New Event. Arena Mode, 2 of 4."),
              "mode menu: the settled start row announces, not the transient");

        // The cb1 regression: after the child, the top menu
        // returns at a relocated home and must speak again.
        zero_md(EVC);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(40);
        plant_md(EVC, 2, 0, 3);
        md_frames(2);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. New Event, 1 of 3."),
              "mode menu: the top menu re-announces from its relocated home");
        zero_md(EVC);
        md_frames(40);

        // One witness at a time knocked out of the mode plant.
        plant_md(EVC, 3, 4, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "mode menu: an out-of-range cursor fails closed");
        plant_md(EVC, 3, 1, 0);
        put_u16(EVC + 0x08, 0xDBEF);   // the g10 decimal-slip value, on purpose
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "mode menu: a wrong signature word fails closed");
        zero_md(EVC);

        // Two shaped widgets at once: the locator cannot
        // choose, so both menus stay silent.
        plant_md(EVC, 3, 1, 0);
        plant_md(EVD, 2, 0, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(4);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "mode menu: two shaped widgets fail closed, neither speaks");

        // The stale widget: fully shaped, flow word down.
        zero_md(EVD);
        plant_md(EVC, 3, 1, 0);
        put_u32(EVFLOW, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        md_frames(40);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "mode menu: a lingering widget with the flow word down stays silent");

        // Retire the section's plants.
        zero_md(EVC); zero_md(EVD);
        put_u32(EVFLOW, 0);
    }

    // ---- the Arena Mode setup screen (VERIFIED 2026-10-25, benches cb1/cb3/cb4) --
    // Cross on Arena Mode replaces the mode list with the
    // event setup form: focus rows Battle / Players / OK on
    // an Archives-class list widget (header 0xDFFF/0xFFF8,
    // cursor +0x06, count +0x0A == 3), the battle index at
    // +0xD8 and the players count at +0x18, all under the
    // event flow word. The battle's name is walked from the
    // packed string table at 0x08E1CC30 (entry 0, "Day of
    // Reckoning", is the anchor) and Risk/Rounds are read
    // from the mission records at 0x08E17AA0 (id at +0x00,
    // constant 100/28 at +0x02/+0x03, Risk +0x06, Rounds
    // +0x0C) — the values below are cb4's thirteen
    // screenshots. The widget is planted at a relocation
    // home on purpose: the gate locates it by scan.
    {
        static const uint32_t SU = 0x0991E400;
        static const uint32_t SU2 = 0x0991C200;
        static const uint32_t EVFLOW = 0x08B5BEF8;
        static const uint32_t NAMES = 0x08E1CC30;
        static const uint32_t MISSIONS = 0x08E17AA0;
        put_u32(0x08BD194C, 0);   // camp witness B dead for this section
        a->detach();
        a->attach(&H);
        auto su_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        static const char* const BNAMES[13] = {
            "Day of Reckoning", "Wheels of Misfortune", "Risky Riches",
            "Weaver Fever", "Sinister Sentinel", "Dead Ringer",
            "Combined Threat", "Treasure Tussle", "Harsh Punishment",
            "A Time to Chill", "Copycat Crisis", "Keepers of the Arena",
            "Villains' Vendetta"
        };
        static const uint8_t BRISK[13] = { 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5 };
        static const uint8_t BROUNDS[13] = { 4, 4, 3, 4, 5, 5, 6, 3, 6, 8, 8, 9, 8 };
        {
            uint32_t p = NAMES;
            for (int i = 0; i < 13; i++) {
                put_ascii(p, BNAMES[i]);
                size_t len = strlen(BNAMES[i]);
                put_u8(p + (uint32_t) len, 0);
                p += (uint32_t) len + 1;
            }
            for (int i = 0; i < 13; i++) {
                uint32_t rec = MISSIONS + (uint32_t) i * 16;
                put_u16(rec + 0x00, (uint16_t) i);
                put_u8(rec + 0x02, 100); put_u8(rec + 0x03, 28);
                put_u8(rec + 0x06, BRISK[i]);
                put_u8(rec + 0x0C, BROUNDS[i]);
            }
        }
        auto plant_su = [&](uint32_t base, uint16_t focus, uint16_t idx) {
            put_u32(EVFLOW, 8);
            RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;   // the Hub
            put_u16(base + 0x00, 0xDFFF);
            put_u16(base + 0x02, 0xFFF8);
            put_u16(base + 0x06, focus);
            put_u16(base + 0x0A, 3);
            put_u16(base + 0xD8, idx);
            put_u16(base + 0x18, 1);
        };
        auto zero_su = [&](uint32_t base) {
            put_u16(base + 0x00, 0); put_u16(base + 0x02, 0);
        };

        // Two shaped widgets at once: the locator cannot
        // choose, so the screen stays silent.
        plant_su(SU, 0, 0);
        plant_su(SU2, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(16);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: two shaped widgets fail closed, the screen stays silent");
        zero_su(SU2);
        zero_su(SU);
        put_u32(EVFLOW, 0);
        su_frames(40);

        // Arrival on the Battle focus, opening battle.
        plant_su(SU, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Arena Mode setup. Battle. Day of Reckoning. Risk 1 of 5. Rounds 4."),
              "setup: arrival speaks the screen and the opening battle");
        su_frames(2);
        CHECK(NSPOKEN == 1, "setup: the arrival line does not repeat");
        CHECK(a->ready(), "setup: ready while the screen is up");

        // Focus echoes, wrap included.
        plant_su(SU, 1, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Players. 1."),
              "setup: nav speaks the Players row and its count");
        plant_su(SU, 2, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "OK."),
              "setup: nav speaks the OK row");
        plant_su(SU, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Battle. Day of Reckoning. Risk 1 of 5. Rounds 4."),
              "setup: nav wraps from OK back to the Battle row");

        // Cycling the battle speaks the landed battle's
        // name and facts (index 7 = Treasure Tussle).
        plant_su(SU, 0, 7);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Battle. Treasure Tussle. Risk 3 of 5. Rounds 3."),
              "setup: cycling speaks the landed battle with its risk and rounds");

        // WhereAmI on the screen.
        plant_su(SU, 0, 12);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. Arena Mode setup. Battle. Villains' Vendetta. Risk 5 of 5. Rounds 8."),
              "setup: WhereAmI names the screen and the current battle");

        // One witness at a time knocked out.
        plant_su(SU, 0, 0);
        put_u16(SU + 0x0A, 5);   // the Archives count, not the setup count
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: the Archives count fails closed");
        plant_su(SU, 3, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: an out-of-range focus fails closed");
        plant_su(SU, 0, 13);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: an out-of-range battle index fails closed");
        plant_su(SU, 0, 0);
        put_u32(EVFLOW, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(40);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: a lingering widget with the flow word down stays silent");
        su_frames(40);

        // The cb5b/cb9 regression: when an arena event ends,
        // the event module raises the flow word for a few
        // hundred frames INSIDE THE ARENA ROOM while the
        // stale setup widget still validates. The gate's Hub
        // term keeps that window silent.
        plant_su(SU, 2, 0);
        RAM[OFF(0x08B61CA1)] = 3;   // the arena room, not the Hub
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(16);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "setup: the event-end window in the arena room stays silent");
        RAM[OFF(0x08B61CA1)] = 1;
        zero_su(SU);
        put_u32(EVFLOW, 0);
        su_frames(40);

        // Degradation: a broken name anchor drops the name
        // but keeps the mission facts; a broken mission
        // witness drops the facts but keeps the name.
        put_u8(NAMES, 'X');   // "Xay of Reckoning"
        plant_su(SU, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        su_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Arena Mode setup. Battle. Risk 1 of 5. Rounds 4."),
              "setup: a broken name anchor degrades to the facts, never an invented name");
        put_u8(NAMES, 'D');
        zero_su(SU);
        su_frames(40);
        put_u8(MISSIONS + 0x02, 99);   // record 0's constant witness
        plant_su(SU, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Battle. Day of Reckoning."),
              "setup: a broken mission witness degrades to the name alone");
        put_u8(MISSIONS + 0x02, 100);

        // Retire the section's plants.
        zero_su(SU);
        put_u32(EVFLOW, 0);
        su_frames(2);
    }

    // ---- the arena event state (VERIFIED 2026-10-25, bench cb4) ----------------
    // After OK the flow leaves the menus: an info-text
    // pointer at 0x09F3A38C holds the hint the event flow
    // wants read — the "move to the middle" string while an
    // event is pending, the "press START" string while the
    // player stands ready on the pad, 0 when nothing is
    // pending. The watch is scoped to the Hub field and
    // speaks only when the string at the pointer matches
    // the proven text. The battle name comes from the
    // setup visit's latch (Dead Ringer, index 5, planted
    // and torn down first).
    {
        static const uint32_t SU = 0x0991E400;
        static const uint32_t EVFLOW = 0x08B5BEF8;
        static const uint32_t INFOPTR = 0x09F3A38C;
        static const uint32_t HINTPEND = 0x08E1BE70;
        static const uint32_t HINTREADY = 0x08E1BEB0;
        static const uint32_t EACTOR = 0x09976090;   // the field section's plant base
        put_u32(0x08BD194C, 0);   // camp witness B dead for this section
        a->detach();
        a->attach(&H);
        auto ae_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };

        // The setup visit that latches the battle.
        put_u32(EVFLOW, 8);
        RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;   // the Hub
        put_u16(SU + 0x00, 0xDFFF); put_u16(SU + 0x02, 0xFFF8);
        put_u16(SU + 0x06, 0); put_u16(SU + 0x0A, 3);
        put_u16(SU + 0xD8, 5); put_u16(SU + 0x18, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Dead Ringer"),
              "arena event: the setup visit latches the chosen battle");
        put_u16(SU + 0x00, 0); put_u16(SU + 0x02, 0);
        put_u32(EVFLOW, 0);
        ae_frames(40);

        // The Hub field: a plausible actor, world 15 room 1.
        RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;
        RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 1;
        put_u16(EACTOR + 0x382, 145);
        put_u16(EACTOR + 0x380, 145);
        put_u16(EACTOR + 0x226, 22);
        put_u16(EACTOR + 0x330, 1);
        put_u32(EACTOR + 0x234, 0x8068);
        put_u32(EACTOR + 0x220, 1);
        put_u32(EACTOR + 0x58, 0x09974C50);
        put_ascii(HINTPEND, "Move to the middle of the hub when you're ready, then press Cross.");
        put_ascii(HINTREADY, "Once everyone is ready, press the START button to begin!");
        put_u32(INFOPTR, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(4);
        CHECK(NSPOKEN == 0, "arena event: no pending event, no line");

        // The event is set.
        put_u32(INFOPTR, HINTPEND);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Event set: Dead Ringer. Move to the middle of the hub and press Cross when ready."),
              "arena event: setting the event speaks where to go, naming the battle");
        ae_frames(3);
        CHECK(NSPOKEN == 1, "arena event: the set line does not repeat");

        // Ready, cancelled, ready again, begun.
        put_u32(INFOPTR, HINTREADY);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ready. Press START to begin."),
              "arena event: pressing Ready on the pad speaks the Begin prompt");
        put_u32(INFOPTR, HINTPEND);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ready cancelled."),
              "arena event: leaving the ready state speaks the cancellation");
        put_u32(INFOPTR, HINTREADY);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ready. Press START to begin."),
              "arena event: readying again speaks again");
        put_u32(INFOPTR, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 0, "arena event: the event beginning stays silent (the fight speaks for itself)");

        // An unrelated info text, and the right pointer with
        // the wrong text, both stay silent.
        put_u32(INFOPTR, 0x08E1C609);   // some other text in the same table
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 0, "arena event: an unrelated info pointer stays silent");
        put_u8(HINTPEND, 'X');
        put_u32(INFOPTR, HINTPEND);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        ae_frames(3);
        CHECK(NSPOKEN == 0, "arena event: the pending pointer with corrupted text fails closed");
        put_u8(HINTPEND, 'M');

        // Retire the section's plants.
        put_u32(INFOPTR, 0);
        put_u16(EACTOR + 0x382, 0);
        RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
        RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;
        ae_frames(2);
    }

    // ---- rumble racing setup + the event-end summary
    // (VERIFIED 2026-10-25, benches cb11/cb12/cb9b) --------
    // The rumble form is the arena form's widget class
    // with row count 4 and its three values packed into
    // the word at +0xD8 (course index, laps - 1, players
    // - 2, 0xFF sentinel). Its visit latches the race for
    // the event-set line. The SUMMARY watch arms on
    // presence in the arena room (world 15 room 3) with
    // the flow word down and speaks "Event over." once
    // when the word rises — the summary screen's own
    // signature.
    {
        static const uint32_t RF = 0x0991C380;    // the rumble widget plant
        static const uint32_t SU2 = 0x0991C600;   // an arena-form plant
        static const uint32_t EVFLOW = 0x08B5BEF8;
        static const uint32_t INFOPTR = 0x09F3A38C;
        static const uint32_t HINTPEND = 0x08E1BE70;
        static const uint32_t EACTOR = 0x09976090;
        put_u32(0x08BD194C, 0);
        a->detach();
        a->attach(&H);
        auto rr_frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        auto plant_rw = [&](uint16_t focus, int course, int laps, int players) {
            put_u32(EVFLOW, 8);
            RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;
            put_u16(RF + 0x00, 0xDFFF); put_u16(RF + 0x02, 0xFFF8);
            put_u16(RF + 0x06, focus); put_u16(RF + 0x0A, 4);
            RAM[OFF(RF + 0xD8)] = (uint8_t) course;
            RAM[OFF(RF + 0xD9)] = (uint8_t) (laps - 1);
            RAM[OFF(RF + 0xDA)] = (uint8_t) (players - 2);
            RAM[OFF(RF + 0xDB)] = 0xFF;
        };
        auto zero_rw = [&] { for (uint32_t o = 0; o < 0x100; o += 2) put_u16(RF + o, 0); };

        // Arrival on the Course row, then the focus walk.
        plant_rw(0, 0, 5, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(14);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Rumble Racing setup. Course. Country Chase."),
              "rumble: arrival speaks the form and the course it opens on");
        rr_frames(40);
        CHECK(NSPOKEN == 1, "rumble: the arrival does not repeat");
        plant_rw(1, 0, 5, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Laps. 5."),
              "rumble: the laps row speaks its number");
        plant_rw(2, 0, 5, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Players. 6."),
              "rumble: the players row speaks its number");
        plant_rw(3, 0, 5, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "OK."),
              "rumble: the OK row speaks");
        plant_rw(0, 3, 7, 2);
        rr_frames(3);   // the frame watch latches the course for the event-set line
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Course. Castle Circuit."),
              "rumble: cycling the course speaks the new course");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Mirage Arena. Rumble Racing setup. Course. Castle Circuit."),
              "rumble: WhereAmI names the screen and the landed row");

        // Fail closed: a broken sentinel, an out-of-range
        // course, an out-of-range laps byte.
        plant_rw(0, 0, 5, 6);
        RAM[OFF(RF + 0xDB)] = 0;
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(14);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "rumble: a broken sentinel byte stays silent");
        plant_rw(0, 9, 5, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(14);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "rumble: an out-of-range course index stays silent");
        plant_rw(0, 0, 41, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(14);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "rumble: an out-of-range laps value stays silent");

        // Both setup forms shaped at once: each gate
        // excludes the other, so neither speaks.
        zero_rw();
        plant_rw(0, 0, 5, 6);
        put_u16(SU2 + 0x00, 0xDFFF); put_u16(SU2 + 0x02, 0xFFF8);
        put_u16(SU2 + 0x06, 0); put_u16(SU2 + 0x0A, 3);
        put_u16(SU2 + 0xD8, 0); put_u16(SU2 + 0x18, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(16);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 0, "rumble: with both setup forms shaped, both gates fail closed");
        put_u16(SU2 + 0x00, 0); put_u16(SU2 + 0x02, 0);

        // The race's event-set line: the rumble visit above
        // latched course 3 (Castle Circuit) last, so the
        // Hub field's pending hint names the race.
        zero_rw();
        put_u32(EVFLOW, 0);
        rr_frames(40);
        RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;
        RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 1;
        put_u16(EACTOR + 0x382, 145);
        put_u16(EACTOR + 0x380, 145);
        put_u16(EACTOR + 0x226, 22);
        put_u16(EACTOR + 0x330, 1);
        put_u32(EACTOR + 0x234, 0x8068);
        put_u32(EACTOR + 0x220, 1);
        put_u32(EACTOR + 0x58, 0x09974C50);
        put_ascii(HINTPEND, "Move to the middle of the hub when you're ready, then press Cross.");
        put_u32(INFOPTR, HINTPEND);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Event set: Rumble Racing, Castle Circuit. Move to the middle of the hub and press Cross when ready."),
              "rumble: setting the race speaks the event-set line naming the course");
        put_u32(INFOPTR, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(3);
        CHECK(NSPOKEN == 0, "rumble: the race beginning stays silent");

        // The event-end summary: presence in the arena
        // room arms the watch; the flow word rising speaks
        // once. Rising with no arm (a fresh attach straight
        // into the window) stays silent.
        RAM[OFF(0x08B61CA1)] = 3; RAM[OFF(0x08B759C1)] = 3;
        put_u32(EVFLOW, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(40);
        CHECK(NSPOKEN == 0, "event end: standing in the arena room is silent while the event runs");
        put_u32(EVFLOW, 8);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(3);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Event over."),
              "event end: the summary screen's arrival speaks once");
        rr_frames(10);
        CHECK(NSPOKEN == 1, "event end: the line does not repeat across the summary");
        RAM[OFF(0x08B61CA1)] = 1; RAM[OFF(0x08B759C1)] = 1;
        put_u32(EVFLOW, 0);
        rr_frames(3);
        RAM[OFF(0x08B61CA1)] = 3; RAM[OFF(0x08B759C1)] = 3;
        put_u32(EVFLOW, 8);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        rr_frames(3);
        CHECK(NSPOKEN == 0, "event end: the window with no armed event stays silent");

        // Retire the section's plants.
        put_u32(EVFLOW, 0);
        put_u16(EACTOR + 0x382, 0);
        RAM[OFF(0x08B61CA0)] = 0; RAM[OFF(0x08B61CA1)] = 0;
        RAM[OFF(0x08B759C0)] = 0; RAM[OFF(0x08B759C1)] = 0;
        rr_frames(2);
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
        CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Low health.") &&
                  strstr(SPOKEN[1], "Your health nearly gone."),
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

    // ---- shops: the Medal Shop buy flow (VERIFIED 2026-10-24, bench shop1-3) -------
    // SHOP.ELF's own structures, planted with the bench's real
    // layouts: the "SHP" stock table (found by signature scan), the
    // floating shop widget (tab/cursor/count + mirror, cross-checked
    // against the row count computed from the table through the def
    // table's family/sub bytes), the fixed phase words, the quantity
    // word, and the player block's currency words (munny +0x287DC,
    // medals +0x287E0). Item names resolve through the resident
    // message tables (category 0x2800) like the dialogue text.
    {
        auto frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        // Retire earlier sections' actors so the locator's
        // corroboration is unique to this section's plant.
        put_u16(0x0996C250 + 0x382, 0);
        put_u16(0x09948000 + 0x382, 0);
        // World words: Mirage Arena Hub (15/1) in both field copies
        // and both map-id copies.
        RAM[OFF(0x08B61CA0)] = 15; RAM[OFF(0x08B61CA1)] = 1;
        RAM[OFF(0x08B759C0)] = 15; RAM[OFF(0x08B759C1)] = 1;
        RAM[OFF(0x09925810)] = 15; RAM[OFF(0x09925811)] = 1;
        RAM[OFF(0x09927D24)] = 15; RAM[OFF(0x09927D25)] = 1;
        const uint32_t SACTOR = 0x09971000;
        put_u16(SACTOR + 0x380, 213); put_u16(SACTOR + 0x382, 213);
        put_u16(SACTOR + 0x226, 30);
        put_u16(SACTOR + 0x330, 1);
        put_u32(SACTOR + 0x234, 0x8068);
        put_u32(SACTOR + 0x220, 1);
        put_u32(SACTOR + 0x58, 0x099816A0);
        const uint32_t SNODE = 0x09972000;
        put_u32(SNODE + 0x04, 0x08B73DD0); put_u32(SNODE + 0x0C, 0x08B73DE0);
        put_u32(SNODE + 0x1C, 0x08B73DB4); put_u32(SNODE + 0x38, SACTOR);
        put_f32(SNODE + 0x40, 1.0f); put_f32(SNODE + 0x44, 0.0f); put_f32(SNODE + 0x48, 2.0f);
        const uint32_t BLOCK = 0x09F00000;
        put_u32(BLOCK + 0x287DC, 777);        // munny
        put_u32(BLOCK + 0x287E0, 5000);       // medals
        put_u16(BLOCK + 0x287E4, 50);         // level
        put_u16(BLOCK + 0x287E6, 213);        // max-HP mirror
        put_u32(BLOCK + 0x287F4, 0);          // character byte (at +0x287F7)
        // The command pool: two live Group Cure instances.
        const uint32_t POOL = 0x09973000;
        put_u32(0x08B75E70, POOL);
        put_u16(POOL + 0, 211);  put_u8(POOL + 3, 0);
        put_u16(POOL + 10, 211); put_u8(POOL + 13, 0);
        put_u16(POOL + 20, 100); put_u8(POOL + 23, 1);   // not an instance
        // Def-table entries (family/sub drive the tab rebuild).
        auto def = [&](uint16_t id, uint8_t fam, uint8_t sub, const char* nm, uint32_t na) {
            uint32_t d = 0x08B34924 + (uint32_t) id * 16;
            put_u8(d, fam); put_u8(d + 1, sub);
            put_ascii(na, nm); put_u32(d + 8, na);
        };
        def(211, 2, 12, "Group Cure", 0x09974000);
        def(100, 2, 1, "Zantetsuken", 0x09974020);
        def(288, 5, 9, "Bio Barrage", 0x09974040);
        // The item-name message table (category 0x2800).
        const uint32_t CTD3 = 0x08DF8AC0;
        put_u32(CTD3 + 0x00, 0x44544340); put_u32(CTD3 + 0x04, 1);
        put_u32(CTD3 + 0x08, 0x28000000);
        put_u32(CTD3 + 0x10, 0x20); put_u32(CTD3 + 0x14, 0x20 + 3 * 12); put_u32(CTD3 + 0x18, 0x200);
        put_ascii(CTD3 + 0x200, "Potion");
        put_ascii(CTD3 + 0x210, "Shimmering Crystal");
        put_ascii(CTD3 + 0x230, "Risky Ticket");
        put_u32(CTD3 + 0x20 + 0, 0x28000001);  put_u32(CTD3 + 0x20 + 4, 0x200);  put_u32(CTD3 + 0x20 + 8, 0);
        put_u32(CTD3 + 0x20 + 12, 0x28003200); put_u32(CTD3 + 0x20 + 16, 0x210); put_u32(CTD3 + 0x20 + 20, 1);
        put_u32(CTD3 + 0x20 + 24, 0x28001F06); put_u32(CTD3 + 0x20 + 28, 0x230); put_u32(CTD3 + 0x20 + 32, 2);
        // The stock table: 3 commands + 3 items.
        const uint32_t SHP = 0x0935C2FC;
        put_u32(SHP, 0x00504853); put_u16(SHP + 4, 1); put_u16(SHP + 6, 2); put_u32(SHP + 8, 6);
        auto rec = [&](int i, uint16_t id, uint8_t kind, uint16_t price) {
            uint32_t r = SHP + 12 + (uint32_t) i * 8;
            put_u16(r, id); put_u8(r + 2, kind); put_u16(r + 6, price);
        };
        rec(0, 211, 1, 180); rec(1, 100, 1, 1000); rec(2, 288, 1, 1500);
        rec(3, 0x0001, 0, 50); rec(4, 0x3200, 0, 300); rec(5, 0x1F06, 0, 150);
        const uint32_t WID = 0x0992D7C0;
        put_u32(WID + 0x2C, 0x09918AC0);      // the shop-widget back-pointer
        put_u16(WID + 0x04, 0); put_u16(WID + 0x34, 0);
        put_u16(WID + 0x36, 6); put_u16(WID + 0x3E, 6);

        // Let the dialogue census rebuild at the new world first
        // (it is what resolves the item names), then open the shop.
        put_u16(0x09918E8C, 0);               // shop still closed
        frames(30);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put_u16(0x09918E8C, 4); put_u16(0x09918F10, 10); put_u16(0x09918F14, 1);
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Medal Shop.") &&
                  strstr(SPOKEN[0], "Group Cure, 180 medals, 2 in stock, 1 of 6.") &&
                  strstr(SPOKEN[0], "You have 5000 medals."),
              "shop: arrival speaks the shop, the first row with its stock, and the balance");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 0, "shop: an idle list does not re-speak");

        put_u16(WID + 0x34, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Zantetsuken, 1000 medals, 0 in stock, 2 of 6."),
              "shop: nav speaks the landed command row with its pool stock");
        put_u16(WID + 0x34, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Potion, 50 medals, 4 of 6."),
              "shop: an item row speaks the name from the message tables");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Medal Shop. Potion, 50 medals, 4 of 6. You have 5000 medals."),
              "shop: WhereAmI inside the shop speaks the row and the balance");

        // Tab landings speak through the watch (L/R have no command).
        put_u16(WID + 0x04, 4); put_u16(WID + 0x34, 0);
        put_u16(WID + 0x36, 1); put_u16(WID + 0x3E, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Friendship. Group Cure, 180 medals, 2 in stock, 1 of 1."),
              "shop: a tab landing speaks the tab and its first row");
        put_u16(WID + 0x04, 2); put_u16(WID + 0x36, 0); put_u16(WID + 0x3E, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Magic. No items."),
              "shop: an empty tab speaks its name and the empty list");
        put_u16(WID + 0x04, 0); put_u16(WID + 0x34, 0);
        put_u16(WID + 0x36, 6); put_u16(WID + 0x3E, 6);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "All. Group Cure, 180 medals, 2 in stock, 1 of 6."),
              "shop: returning to All speaks its first row");

        // The buy flow: quantity step, quantity echo, confirmation
        // (starts on No; left/right flip the tracked choice).
        put_u16(0x09918E8C, 6); put_u16(0x09918F10, 10); put_u16(0x09918F14, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "How many? Group Cure, 180 medals, 2 in stock, 1 of 6."),
              "shop: the quantity step speaks its prompt with the row");
        put_u16(0x09918F14, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Quantity 3, 540 medals.") &&
                  strstr(SPOKEN[0], "You have 5000 medals."),
              "shop: a quantity change speaks the quantity, the total and the balance");
        put_u16(0x09918F10, 40);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Trade in these medals? No."),
              "shop: the confirmation speaks, starting on No");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Yes."),
              "shop: left flips the confirmation to Yes");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No."),
              "shop: left again flips it back to No");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Yes."),
              "shop: right flips it to Yes again");
        // Confirm: the game returns to the list and the medals drop.
        put_u32(BLOCK + 0x287E0, 4460);
        put_u16(0x09918E8C, 4); put_u16(0x09918F10, 10);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Bought Group Cure, quantity 3. You have 4460 medals."),
              "shop: a completed purchase speaks what was bought and the new balance");

        // Exit: the phase word leaves the shop and the gate drops.
        put_u16(0x09918E8C, 3);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN >= 1 && !strstr(SPOKEN[0], "Medal Shop."),
              "shop: after exit the shop no longer owns WhereAmI");

        // Negatives: each gate leg broken in turn keeps the shop silent.
        put_u16(0x09918E8C, 9329);            // phase garbage (camp-lineage value)
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Medal Shop."),
              "shop: a garbage phase word fails the gate closed");
        put_u16(0x09918E8C, 4);
        put_u16(WID + 0x36, 5); put_u16(WID + 0x3E, 5);   // count lies about the tab
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Medal Shop."),
              "shop: a widget count that disagrees with the stock table fails closed");
        put_u16(WID + 0x36, 6); put_u16(WID + 0x3E, 6);
        put_u32(SHP + 12 + 6 * 8, 7);          // break the table terminator
        put_u16(0x09918E8C, 3); frames(2);     // force a rescan
        put_u16(0x09918E8C, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Medal Shop."),
              "shop: a stock table without its terminator fails closed");
        put_u32(SHP + 12 + 6 * 8, 0);

        // (The munny shop's own section follows below: the type
        // word selects the table format strictly, so a type flip on
        // Medal-shaped data is a malformed table, not a Moogle
        // shop — the real thing has its own plants.)
        put_u16(0x09918E8C, 3);
        frames(2);
    }

    // ---- shops: the munny Command Shop (VERIFIED 2026-10-24, bench shop14-16) ----
    // The other shop family: a top level (phase 2) over Buy (4) and
    // Sell (5) lists, each with quantity (6/7) and confirm steps; the
    // stock table is the bare def-id list format (type 1), prices
    // come from the adapter's harvested table, four stocked defs are
    // never displayed, and sell rows are the pool's family-2
    // instances sorted by def id with computed prices.
    {
        auto frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        const uint32_t BLOCK = 0x09F00000, POOL = 0x09973000;
        put_u32(0x0935C2FC, 0);               // retire the Medal table
        put_u16(0x0992D7C0 + 0x36, 0); put_u16(0x0992D7C0 + 0x3E, 0);  // and its widget
        // (a lingering widget whose count matches another list's
        // would make the scan ambiguous — the gate fails closed on
        // that, so the plant must not leave one behind)
        auto def = [&](uint16_t id, uint8_t fam, uint8_t sub, const char* nm, uint32_t na) {
            uint32_t d = 0x08B34924 + (uint32_t) id * 16;
            put_u8(d, fam); put_u8(d + 1, sub);
            put_ascii(na, nm); put_u32(d + 8, na);
        };
        def(91, 2, 1, "Quick Blitz", 0x09974060);
        def(94, 2, 1, "Meteor Crash", 0x09974080);
        def(188, 2, 3, "Potion", 0x099740A0);
        def(243, 3, 4, "Slide", 0x099740C0);
        def(205, 4, 1, "Combo Plus", 0x099740E0);
        // Pool: the two Group Cure stacks become level 3; add two
        // Quick Blitz instances (LV1 with an ability, LV3 without)
        // and a 60-count Potion stack.
        put_u8(POOL + 2, 3); put_u8(POOL + 12, 3);
        put_u16(POOL + 30, 91);  put_u8(POOL + 32, 1); put_u8(POOL + 33, 0); put_u16(POOL + 36, 205);
        put_u16(POOL + 40, 91);  put_u8(POOL + 42, 3); put_u8(POOL + 43, 0); put_u16(POOL + 46, 0);
        put_u16(POOL + 50, 188); put_u8(POOL + 52, 60); put_u8(POOL + 53, 0);
        put_u16(POOL + 60, 243); put_u8(POOL + 62, 2); put_u8(POOL + 63, 0); put_u16(POOL + 66, 0);
        // The stock table: bare id list; 94 is stocked but hidden.
        const uint32_t CSHP = 0x0935D000;
        put_u32(CSHP, 0x00504853); put_u16(CSHP + 4, 1); put_u16(CSHP + 6, 1); put_u32(CSHP + 8, 5);
        put_u32(CSHP + 12, 91); put_u32(CSHP + 16, 94); put_u32(CSHP + 20, 100);
        put_u32(CSHP + 24, 188); put_u32(CSHP + 28, 243); put_u32(CSHP + 32, 0);
        // One widget per list.
        const uint32_t TOPW = 0x09944110, BUYW = 0x099446B0, SELLW = 0x09945190;
        put_u32(TOPW + 0x2C, 0x09918AC0); put_u32(BUYW + 0x2C, 0x09918AC0); put_u32(SELLW + 0x2C, 0x09918AC0);
        put_u16(TOPW + 0x34, 0); put_u16(TOPW + 0x36, 3); put_u16(TOPW + 0x3E, 3);
        put_u16(BUYW + 0x04, 0); put_u16(BUYW + 0x34, 0); put_u16(BUYW + 0x36, 4); put_u16(BUYW + 0x3E, 4);
        put_u16(SELLW + 0x04, 0); put_u16(SELLW + 0x34, 0); put_u16(SELLW + 0x36, 6); put_u16(SELLW + 0x3E, 6);

        // Top level arrival.
        put_u16(0x09918E8C, 2); put_u16(0x09918F10, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Moogle Shop. Buy, 1 of 3.") &&
                  strstr(SPOKEN[0], "You have 777 munny."),
              "command shop: the top level speaks the shop, the Buy row and the balance");
        put_u16(TOPW + 0x34, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Sell, 2 of 3."),
              "command shop: nav speaks the top-level rows");
        // Into Buy: the landing row carries the harvested price and
        // the pool stock; the hidden def is skipped in the numbering.
        put_u16(TOPW + 0x34, 0);
        put_u16(0x09918E8C, 4); put_u16(0x09918F10, 10);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Quick Blitz, 100 munny, 2 in stock, 1 of 4."),
              "command shop: the buy landing speaks price and stock from the pool");
        put_u16(BUYW + 0x34, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Zantetsuken, 1700 munny, 0 in stock, 2 of 4."),
              "command shop: a buy row speaks its harvested price");
        put_u16(BUYW + 0x34, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Potion, 30 munny, 60 in stock, 3 of 4."),
              "command shop: the hidden def is skipped and the item stack speaks its count");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Moogle Shop. Potion, 30 munny, 60 in stock, 3 of 4. You have 777 munny."),
              "command shop: WhereAmI speaks the buy row and the balance");
        // Buy quantity and the game's own confirm wording.
        put_u16(0x09918E8C, 6); put_u16(0x09918F10, 10); put_u16(0x09918F14, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "How many? Potion, 30 munny, 60 in stock, 3 of 4."),
              "command shop: the quantity step speaks its prompt with the row");
        put_u16(0x09918F10, 40);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Complete this purchase? No."),
              "command shop: the confirmation uses the game's wording, starting on No");
        // Back to the top, then into Sell.
        put_u16(0x09918E8C, 2); put_u16(0x09918F10, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Buy, 1 of 3."),
              "command shop: returning to the top level speaks its row");
        put_u16(0x09918E8C, 5); put_u16(0x09918F10, 10);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Quick Blitz, level 1, Combo Plus, 75 munny, 1 of 6."),
              "command shop: the sell landing speaks level, ability and the computed price");
        put_u16(SELLW + 0x34, 2);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Potion, 60 in stock, 3 of 6."),
              "command shop: a sell row for an item stack speaks its held count");
        put_u16(SELLW + 0x34, 5);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Slide, level 2, 125 munny, 6 of 6."),
              "command shop: a family-3 instance sells too, at its computed price");
        // Sell one Quick Blitz: quantity, echo, confirm, completion.
        put_u16(SELLW + 0x34, 0);
        put_u16(0x09918E8C, 7); put_u16(0x09918F10, 10); put_u16(0x09918F14, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "How many? Quick Blitz, level 1, Combo Plus, 75 munny, 1 of 6."),
              "command shop: the sell quantity step speaks its prompt with the row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Quantity 1, 75 munny.") &&
                  strstr(SPOKEN[0], "You have 777 munny."),
              "command shop: the sell quantity echo speaks the total and the balance");
        put_u16(0x09918F10, 40);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Complete this sale? No."),
              "command shop: the sale confirmation speaks, starting on No");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::MenuLeft);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Yes."),
              "command shop: left flips the sale confirmation to Yes");
        put_u32(BLOCK + 0x287DC, 852);
        put_u16(0x09918E8C, 5); put_u16(0x09918F10, 10);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Sold Quick Blitz. You have 852 munny."),
              "command shop: a completed sale speaks what was sold and the new balance");
        // A widget count that disagrees with the computed rows
        // fails the gate closed.
        put_u16(BUYW + 0x36, 9); put_u16(BUYW + 0x3E, 9);
        put_u16(0x09918E8C, 4); put_u16(0x09918F10, 10);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(8);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Moogle Shop."),
              "command shop: a buy widget count that disagrees with the stock fails closed");
        put_u16(BUYW + 0x36, 4); put_u16(BUYW + 0x3E, 4);
        put_u16(0x09918E8C, 3);
        frames(2);
    }

    // ---- the Command Board (2026-10-25, benches cb7d/cbplay) -------------------
    // Synthetic plants of the board module's verified structures:
    // the list-widget family (class word, count words, per-widget
    // table pointer, cursor pair), the setup widget, the Yes/No
    // dialog family, the liveness global and the player
    // records, all at lineage-true addresses. Turns are
    // estimated (no marker word exists): the tests drive the
    // estimator with record changes.
    {
        a->detach();
        a->attach(&H);
        auto frames = [&](int n) { for (int i = 0; i < n; i++) a->on_frame(); };
        const uint32_t CBSCR = 0x08B5BE50;   // module screen-object pointer
        const uint32_t CBLIVE = 0x08BC58FC;  // game-object liveness pointer
        const uint32_t CBTITLE = 0x09836220, CBSETUP = 0x09838DD0;
        const uint32_t CBDIALOG = 0x098364B0, CBTURN = 0x09867E40;
        const uint32_t CBHAND = 0x0986D210, CBOPT = 0x0985DF40, CBSTRIP = 0x09859DE0;
        const uint32_t CBREC = 0x09F2DBD0;   // Terra / Ventus / Aqua records
        auto cb_list = [&](uint32_t b, uint32_t cnt, uint32_t tab, uint32_t cur) {
            put_u32(b, 0x08B77D68);
            put_u32(b + 0x1C, cnt << 16);
            put_u32(b + 0x20, 0xFFFF0000u | cnt);
            put_u32(b + 0x24, tab);
            put_u32(b + 0x14, cur); put_u32(b + 0x18, cur);
        };
        auto cb_clear_list = [&](uint32_t b) { put_u32(b, 0); put_u32(b + 0x24, 0); };
        auto cb_dialog = [&](uint32_t choice) {
            put_u32(CBDIALOG + 0x1C, 0x00020000);
            put_u32(CBDIALOG + 0x20, 0xFFFF0002);
            put_u32(CBDIALOG + 0x24, 0x08BCD828);
            put_u32(CBDIALOG + 0x14, choice); put_u32(CBDIALOG + 0x18, choice);
        };
        auto cb_dialog_clear = [&]() { put_u32(CBDIALOG + 0x24, 0); };
        // Panel names through the def table (DefName's home).
        auto cb_def = [&](uint16_t id, const char* name, uint32_t strAddr) {
            put_ascii(strAddr, name);
            put_u32(0x08B34924 + (uint32_t) id * 16 + 8, strAddr);
        };
        cb_def(38, "Sweet Memory", 0x09F90000);
        cb_def(66, "Rising Rock 1", 0x09F90040);
        cb_def(23, "Cyclone", 0x09F90080);
        auto cb_record = [&](int i, uint32_t gp, uint8_t id, uint8_t humanByte,
                             uint8_t ord, uint16_t panel) {
            uint32_t r = CBREC + (uint32_t) i * 0x24;
            put_u32(r, gp);
            put_u8(r + 8, id); put_u8(r + 9, humanByte);
            put_u8(r + 10, humanByte == 0 ? 0xFF : (uint8_t) i);
            put_u8(r + 0x0B, ord);
            put_u16(r + 0x0E, panel);
            put_u16(r + 0x18, 100); put_u16(r + 0x1A, 0xFFFF);
            put_u16(r + 0x1C, 0xFFFF); put_u16(r + 0x1E, 0xFFFF); put_u16(r + 0x20, 0xFFFF);
        };
        auto cb_records = [&]() {
            cb_record(0, 1000, 3, 0, 1, 38);   // Terra, the human, order 2nd
            cb_record(1, 925, 1, 1, 0, 23);    // Ventus, order 1st
            cb_record(2, 1000, 2, 1, 2, 38);   // Aqua, order 3rd
        };
        put_u32(CBSCR, 0x09823C90);   // the module owns the screen

        // -- board title ------------------------------------------------------
        cb_list(CBTITLE, 3, 0x08BB1550, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. New Game, 1 of 3."),
              "board title: arrival speaks the menu and its opening row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put_u32(CBTITLE + 0x14, 2); put_u32(CBTITLE + 0x18, 2);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "How to Play, 3 of 3"),
              "board title: nav speaks the landed row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. How to Play, 3 of 3."),
              "board title: WhereAmI names the menu and the current row");
        CHECK(a->ready(), "board title: ready while the title is up");
        // A second identical widget makes the scan non-unique:
        // the title fails closed. (The first widget is retired
        // first so the sticky cache drops.)
        cb_clear_list(CBTITLE);
        frames(15);
        cb_list(CBTITLE, 3, 0x08BB1550, 0);
        cb_list(0x09840000, 3, 0x08BB1550, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Command Board."),
              "board title: a duplicate widget fails the scan closed");
        cb_clear_list(0x09840000);
        cb_clear_list(CBTITLE);
        frames(40);

        // -- board setup ------------------------------------------------------
        auto cb_setup = [&](uint32_t focus, uint32_t board, uint32_t goal) {
            put_u32(CBSETUP, 0x08B77D68);
            put_u32(CBSETUP + 0x30, 0x08BAF330);
            put_u32(CBSETUP + 0x34, 0x08BAF3B0);
            put_u32(CBSETUP + 0x38, 0x08BAF490);
            put_u32(CBSETUP + 0x20, focus);
            put_u32(CBSETUP + 0x3C, board); put_u32(CBSETUP + 0x40, board);
            put_u32(CBSETUP + 0x60, goal); put_u32(CBSETUP + 0x64, goal);
        };
        cb_setup(1, 0, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. Select a board. Keyblade Board. Goal 5000 GP."),
              "board setup: arrival speaks the board step and the first board");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        cb_setup(1, 1, 0);
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Royal Board. Goal 5000 GP."),
              "board setup: nav speaks the landed board with its goal");
        cb_setup(3, 1, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Select a goal. GP Goal: 10000."),
              "board setup: the goal step announces itself and the goal");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. Select a goal. GP Goal: 10000."),
              "board setup: WhereAmI answers the goal step");
        // The confirm dialog (focus 4) speaks its question.
        cb_setup(4, 1, 1);
        cb_dialog(0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Play on this board? Yes."),
              "board setup: the confirm dialog speaks its question and choice");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        cb_dialog(1);
        a->command(oga::Command::MenuRight);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No."),
              "board dialog: nav echoes the flipped choice");
        cb_dialog_clear();
        put_u32(CBSETUP, 0);   // the setup widget dies at creation
        frames(40);

        // -- the game: order, turns, summaries --------------------------------
        cb_records();
        put_u32(CBLIVE, 0x09835EE0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(45);
        CHECK(NSPOKEN == 0, "board game: creation alone speaks nothing yet");
        // The shuffle dialog speaks the order line with its question.
        cb_dialog(0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Playing order: Ventus. Terra. Aqua. Shuffle the playing order? Yes."),
              "board game: the shuffle dialog speaks the order and its question");
        cb_dialog_clear();
        frames(40);
        // The estimator: the first record to MOVE opens its
        // owner's turn. Ventus steps (panel 23 -> 29)...
        put_u16(CBREC + 0x24 + 0x0E, 29);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Ventus's turn."),
              "board game: the first mover's turn is called");
        // ...and buys (925 -> 850) on his own record: the turn
        // continuing is silent. The turn menu's arrival then
        // closes his turn: summary, call, menu line.
        put_u32(CBREC + 0x24, 850);
        frames(4);
        cb_list(CBTURN, 4, 0x08BBE088, 0);
        put_u32(CBTURN + 0x60, 0x09820000); put_u32(CBTURN + 0x70, 0x09820000);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 3 && strstr(SPOKEN[0], "Ventus spent 75 GP.") &&
              strstr(SPOKEN[1], "Your turn.") &&
              strstr(SPOKEN[2], "Command Board. Roll, 1 of 4."),
              "board game: the menu's arrival summarizes the opponent, calls your turn, names the menu");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put_u32(CBTURN + 0x14, 1); put_u32(CBTURN + 0x18, 1);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Hand, 2 of 4"),
              "board turn menu: nav speaks the landed row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. Your turn. Hand, 2 of 4."),
              "board turn menu: WhereAmI names the turn and the row");
        cb_clear_list(CBTURN);
        frames(40);
        // The Hand screen lists the special plays.
        cb_list(CBHAND, 5, 0x08BCEE60, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Hand. Stun, 1 of 5."),
              "board hand: arrival speaks the plays list and its first row");
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put_u32(CBHAND + 0x14, 4); put_u32(CBHAND + 0x18, 4);
        a->command(oga::Command::MenuNext);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Double Toll, 5 of 5"),
              "board hand: nav speaks the landed play");
        cb_clear_list(CBHAND);
        frames(40);
        // Options rows speak by name.
        cb_list(CBOPT, 5, 0x08BCF5A8, 4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Options. End Game, 5 of 5."),
              "board options: arrival speaks the list and the landed row");
        cb_clear_list(CBOPT);
        frames(40);
        // A landing on a command panel arms the purchase prompt.
        put_u16(CBREC + 0x0E, 66);   // Terra steps onto Rising Rock 1
        frames(6);
        cb_dialog(0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "You have landed on a Command Panel: Rising Rock 1. Place a command card on this panel? Yes."),
              "board landing: the purchase prompt names the panel and asks");
        cb_dialog_clear();
        frames(40);
        // The card strip opens; picking a card raises "Are you sure?".
        cb_list(CBSTRIP, 5, 0x08B8DB78, 1);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Select a command card. Card 2 of 5."),
              "board picker: arrival speaks the strip and the highlighted card");
        cb_dialog(0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(40);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Are you sure? Yes."),
              "board picker: the placement confirm speaks over the strip");
        cb_dialog_clear();
        cb_clear_list(CBSTRIP);
        frames(40);
        // Aqua's turn opens when her record first moves (she
        // is Terra's order-successor).
        put_u16(CBREC + 0x48 + 0x0E, 23);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Aqua's turn."),
              "board game: the next opponent's turn is called when she moves");
        // A toll on her turn: 100 from her to Terra. Her own
        // record is the current one, so the pair is silent
        // until her turn ends — when Ventus (her successor)
        // first moves.
        put_u32(CBREC + 0x48, 900);
        put_u32(CBREC, 1100);
        frames(4);
        put_u16(CBREC + 0x24 + 0x0E, 66);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(4);
        CHECK(NSPOKEN == 2 && strstr(SPOKEN[0], "Aqua paid 100 GP to Terra.") &&
              strstr(SPOKEN[1], "Ventus's turn."),
              "board game: a toll is summarized as a payment between players");
        // The human is Ventus's successor; the human's panel
        // settling back calls their turn in the estimate.
        put_u16(CBREC + 0x0E, 38);
        frames(4);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Command Board. Your turn. You are on Sweet Memory. Ventus: 850 GP. Terra: 1100 GP. Aqua: 900 GP."),
              "board query: the position line names the panel, the turn and the standings");
        // Fail closed: two human flags invalidate the records.
        put_u8(CBREC + 0x24 + 9, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(45);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Command Board."),
              "board game: records without exactly one human fail closed");
        put_u8(CBREC + 0x24 + 9, 1);
        // Fail closed: the liveness word down ends the game state.
        put_u32(CBLIVE, 0);
        NSPOKEN = 0; oga::AdapterSpeechReset();
        frames(45);
        a->command(oga::Command::WhereAmI);
        CHECK(NSPOKEN == 0 || !strstr(SPOKEN[0], "Command Board."),
              "board game: no liveness, no board speech");
        put_u32(CBSCR, 0);
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
