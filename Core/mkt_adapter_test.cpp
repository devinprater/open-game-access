/*
 * mkt_adapter_test.cpp — host test for the Mortal Kombat Trilogy (PlayStation) adapter, against
 * SYNTHETIC memory laid out the way the measured map says the real game's RAM is.
 *
 * No disc and no emulator: a 2 MiB fake of PlayStation main RAM (KSEG0 0x80000000), the
 * shipped map's own addresses, and a short script per case. That proves the adapter's logic
 * (screen detection, the menus, the round and the health language, the closeness cue, the
 * pause) and that every address it reads is the one in the map. It cannot prove the map
 * matches a real disc: the measuring is written up in
 * docs/reverse-engineering/mortal-kombat-trilogy.md, and the phone run is on the owner's list.
 */
#include "adapter.h"
#include "mkt_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace oga { extern const Adapter kMortalKombatTrilogy; }

// The registry (adapters.cpp) lists the Nintendo readers too; this focused test links neither,
// so it supplies inert stand-ins, as the other adapter tests do.
namespace oga {
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
static void s_frm() {}
static bool s_att(const Host*) { return false; }
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11", "FE", "YFEE", s_att, s_frm, s_cmd, s_rdy, s_det};
const Adapter kGameBoyAdvance         = {"gba", "GBA", "", s_att, s_frm, s_cmd, s_rdy, s_det};
}

static uint8_t RAM[0x200000];
static inline size_t OFF(uint32_t a) { return (size_t) (a & 0x1FFFFF); }

static char SPOKEN[128][192]; static int NSPOKEN = 0;
static char LOGGED[64][256];  static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 128) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 64) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }

static void w8 (uint32_t a, uint8_t v)  { RAM[OFF(a)] = v; }
static void w16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }
static void w32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }

static bool said(const char* needle)
{
    for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], needle)) return true;
    return false;
}
static int saidCount(const char* needle)
{
    int n = 0;
    for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], needle)) n++;
    return n;
}
static void clear(void) { NSPOKEN = 0; oga::AdapterSpeechReset(); }

static const oga::Adapter* A = &oga::kMortalKombatTrilogy;
static const oga::MktMap* M = nullptr;

static void frames(int n) { for (int i = 0; i < n; i++) A->on_frame(); }

static void setScreen(int s)
{
    w8(M->screen_a_addr, oga::kMktScreenIds[s].a);
    w8(M->screen_b_addr, oga::kMktScreenIds[s].b);
}

// Fighter objects live in two slots that swap between fights; x is a signed halfword at +0x12.
static const uint32_t kObjA = 0x800293E8, kObjB = 0x8002939C;
static void setX(uint32_t obj, int16_t x) { w16(obj + M->x_offset, (uint16_t) x); }

static void startFight(int p1id, int p2id, bool swapSlots)
{
    w8(M->fight_addr, M->fight_value);
    setScreen(oga::kMktScreenFight);
    w8(M->p1_id_addr, (uint8_t) p1id);
    w8(M->p2_id_addr, (uint8_t) p2id);
    w16(M->p1_health_addr, M->health_full);
    w16(M->p2_health_addr, M->health_full);
    w32(M->p1_wins_addr, 0);
    w32(M->p2_wins_addr, 0);
    w32(M->p1_obj_ptr_addr, swapSlots ? kObjB : kObjA);
    w32(M->p2_obj_ptr_addr, swapSlots ? kObjA : kObjB);
}

int main()
{
    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32; H.speak = spk; H.log = lg;
    M = oga::MktCurrentMap();

    int pass = 0, fail = 0;
    #define CHECK(c, m) do { if (c) { pass++; } else { fail++; printf("  FAIL: %s\n", m); } } while (0)

    // ---- identity ---------------------------------------------------------------------
    CHECK(strcmp(A->game_code, "SLUS00330") == 0, "game code SLUS00330");
    CHECK(strcmp(A->id, "mkt") == 0, "id is mkt");
    CHECK(oga::find_by_game_code("SLUS00330") == A, "the registry resolves the disc serial");
    CHECK(oga::find_by_game_code("SLUS0033") == nullptr, "a truncated serial matches nothing");
    CHECK(A->attach(&H), "attach accepts the host");
    CHECK(A->ready(), "ready from the first frame");

    // ---- the shipped map: every address measured, none left empty -------------------------
    CHECK(M->screen_a_addr && M->screen_b_addr && M->main_cursor_addr && M->options_cursor_addr &&
          M->konfigure_cursor_addr && M->select_cursor_addr && M->select_table_addr &&
          M->fight_addr && M->p1_health_addr && M->p2_health_addr && M->p1_wins_addr &&
          M->p2_wins_addr && M->p1_obj_ptr_addr && M->p2_obj_ptr_addr && M->pause_addr,
          "the shipped map has every measured address");
    CHECK(M->health_full == 166, "a full bar reads 166 (0xA6)");
    CHECK(M->gap_far > M->gap_near, "the arena gap runs near to far");

    // ---- an all-zero RAM says nothing ---------------------------------------------------------
    clear();
    frames(60);
    CHECK(NSPOKEN == 0, "all-zero RAM (boot) is silent");
    clear();
    A->command(oga::Command::WhereAmI);
    CHECK(said("Not in a fight or a menu I know"), "Where Am I on an unknown screen says so");

    // ---- the main menu ----------------------------------------------------------------------
    clear();
    setScreen(oga::kMktScreenMain); w8(M->main_cursor_addr, 0);
    frames(5);
    CHECK(NSPOKEN == 0, "a screen must hold before it speaks (a flicker is silent)");
    frames(20);
    CHECK(said("Main menu. Mortal Kombat."), "the main menu announces itself and the highlighted box");
    clear();
    w8(M->main_cursor_addr, 1); frames(1);
    CHECK(said("2 on 2 Kombat"), "cursor to box 2");
    clear();
    w8(M->main_cursor_addr, 2); frames(1);
    CHECK(said("8 Player Kombat"), "cursor to box 3");
    clear();
    w8(M->main_cursor_addr, 3); frames(1);
    CHECK(said("Options"), "cursor to box 4");
    clear();
    frames(10);
    CHECK(NSPOKEN == 0, "a cursor that did not move says nothing");
    clear();
    A->command(oga::Command::WhereAmI);
    CHECK(said("Main menu. Options."), "Where Am I on the main menu says the box");

    // ---- the options hub -----------------------------------------------------------------------
    clear();
    setScreen(oga::kMktScreenOptions); w8(M->options_cursor_addr, 0);
    frames(20);
    CHECK(said("Options. Game Konfigure."), "options hub announces itself");
    clear();
    w8(M->options_cursor_addr, 1); frames(1);
    CHECK(said("Kontrol Konfigure"), "hub icon 2");
    clear();
    w8(M->options_cursor_addr, 3); frames(1);
    CHECK(said("Sound and Music"), "hub icon 4 (the question-mark icon is skipped by the game)");
    clear();
    w8(M->options_cursor_addr, 4); frames(1);
    CHECK(said("Exit Kombat"), "hub icon 5");

    // ---- Game Konfigure ------------------------------------------------------------------------
    clear();
    w8(M->options_cursor_addr, 0); frames(1);
    clear();
    w8(M->konfigure_cursor_addr, 0); w8(M->difficulty_addr, 2);
    setScreen(oga::kMktScreenKonfigure);
    frames(20);
    CHECK(said("Game Konfigure. Difficulty, Medium."), "Game Konfigure from the hub reads the first row with its value");
    clear();
    w8(M->difficulty_addr, 3); frames(1);
    CHECK(said("Difficulty, Hard"), "changing the difficulty says the new value");
    clear();
    w8(M->konfigure_cursor_addr, 6); frames(1);
    CHECK(said("Shang Tsung's morphs"), "row 7");
    clear();
    w8(M->konfigure_cursor_addr, 11); frames(1);
    CHECK(said("Exit"), "the last row is Exit");

    // The same screen id on the "choose your destiny" ladder (no trip through the hub) is silent.
    clear();
    setScreen(oga::kMktScreenOther); frames(20);
    setScreen(oga::kMktScreenSelect); frames(20);
    clear();
    setScreen(oga::kMktScreenKonfigure); w8(M->konfigure_cursor_addr, 1);
    frames(30);
    w8(M->konfigure_cursor_addr, 2); frames(5);
    CHECK(NSPOKEN == 0, "the ladder's look-alike of the Konfigure id does not read Konfigure rows");

    // ---- character select -------------------------------------------------------------------------
    // The slot -> fighter table lives in RAM; plant the first four entries measured on the disc.
    static const uint16_t kTable[4] = { 23, 29, 8, 1 };
    for (int i = 0; i < 4; i++) w16(M->select_table_addr + 2u * (uint32_t) i, kTable[i]);
    setScreen(oga::kMktScreenOther); frames(20);
    clear();
    w8(M->select_cursor_addr, 0);
    setScreen(oga::kMktScreenSelect);
    frames(20);
    CHECK(said("Choose your fighter. Noob Saibot."), "character select announces the highlighted fighter by name");
    clear();
    w8(M->select_cursor_addr, 1); frames(1);
    CHECK(said("Baraka"), "slot 1 is Baraka");
    clear();
    w8(M->select_cursor_addr, 2); frames(1);
    CHECK(said("Cyrax"), "slot 2 is Cyrax");
    clear();
    w8(M->select_cursor_addr, 3); frames(1);
    CHECK(said("Sonya"), "slot 3 is Sonya");
    clear();
    w16(M->select_table_addr + 2u * 3, 22); frames(0);
    w8(M->select_cursor_addr, 0); frames(1);
    w8(M->select_cursor_addr, 3); frames(1);
    CHECK(said("Fighter 22"), "an id that is not known is spoken as a number, not guessed");
    clear();
    w8(M->select_cursor_addr, 200); frames(2);
    CHECK(NSPOKEN == 0, "a slot out of range says nothing");

    // ---- a fight: the round, who, health ------------------------------------------------------
    setScreen(oga::kMktScreenOther); w8(M->fight_addr, 0); frames(20);
    clear();
    startFight(4, 16, false);                         // Sub-Zero against Jade, slots as in the first fight
    setX(kObjA, 460); setX(kObjB, 521);
    frames(10);
    CHECK(NSPOKEN == 0, "nothing is said before the fight flag has held");
    frames(15);
    CHECK(said("Round 1. Sub-Zero versus Jade."), "the round starts with who is fighting");
    clear();
    frames(30);
    CHECK(NSPOKEN == 0, "the round line is said once");

    // health: hits, milestones, beeps
    oga::CueSnapshot cs;
    CHECK(A->cue_snapshot && A->cue_snapshot(&cs) && cs.battle && cs.locked, "the cue snapshot is a battle with a lock");
    clear();
    w16(M->p2_health_addr, 140); frames(1);
    CHECK(A->cue_snapshot(&cs) && cs.foe_beep && cs.foe_beep_pan > 0.0f, "a hit on the foe beeps on the right");
    CHECK(!A->cue_snapshot(&cs) || !cs.foe_beep, "a beep fires once per hit");
    CHECK(NSPOKEN == 0, "an ordinary hit beeps without speaking");
    w16(M->p2_health_addr, 120); frames(1);   // 72 percent
    CHECK(said("Foe health 75 percent"), "the foe's 75 percent milestone is spoken");
    clear();
    w16(M->p1_health_addr, 80); frames(1);    // 48 percent
    CHECK(A->cue_snapshot(&cs) && cs.own_beep && cs.own_beep_pan < 0.0f, "a hit on you beeps on the left");
    CHECK(said("Your health 50 percent"), "your 50 percent milestone is spoken");
    clear();
    w16(M->p1_health_addr, 10); frames(1);    // 6 percent
    CHECK(said("Your health nearly gone"), "nearly gone is spoken");
    clear();
    w16(M->p1_health_addr, 9); frames(1);
    CHECK(NSPOKEN == 0, "a latched milestone is not repeated");
    A->cue_snapshot(&cs);                      // drain the pending beeps
    clear();
    w16(M->p1_health_addr, 0xFFFF); frames(1);
    CHECK(NSPOKEN == 0 && (!A->cue_snapshot(&cs) || !cs.own_beep), "a garbage health read neither beeps nor speaks");
    w16(M->p1_health_addr, 9);

    // closeness and side, read through the slot pointers
    clear();
    w32(M->p1_health_addr - 0, 0); w16(M->p1_health_addr, 100);
    setX(kObjA, 460); setX(kObjB, 470);        // touching
    frames(1);
    CHECK(A->cue_snapshot(&cs) && cs.locked && cs.dist < 6.0f, "touching fighters give the fastest beacon");
    setX(kObjA, 100); setX(kObjB, 400);        // across the arena
    frames(1);
    CHECK(A->cue_snapshot(&cs) && cs.locked && cs.dist > 40.0f, "far apart gives the slowest beacon");
    // the foe jumps over: P2's x goes left of P1's
    clear();
    setX(kObjA, 300); setX(kObjB, 200);
    frames(3);
    CHECK(NSPOKEN == 0, "a side change must hold before it is said");
    frames(10);
    CHECK(said("Foe on your left."), "the foe crossing over is said");
    clear();
    A->command(oga::Command::WhereAmI);
    CHECK(said("Round 1.") && said("Your health") && said("on your left"), "Where Am I in a fight: round, health, side");

    // ---- round results ----------------------------------------------------------------------------
    clear();
    w16(M->p1_health_addr, 0);  frames(1);            // you are KO'd
    w32(M->p2_wins_addr, 1);    frames(1);
    CHECK(said("Foe wins the round. 0 to 1."), "the foe taking a round is said");
    clear();
    w16(M->p1_health_addr, M->health_full); w16(M->p2_health_addr, M->health_full); frames(1);
    CHECK(said("Round 2. Sub-Zero versus Jade."), "the refill after a KO announces the next round");
    clear();
    w32(M->p1_wins_addr, 1); frames(1);
    CHECK(said("You win the round. 1 to 1."), "you taking a round is said");
    clear();
    w16(M->p2_health_addr, 0); frames(1);
    w16(M->p1_health_addr, 0); w32(M->p2_wins_addr, 2); frames(1);
    CHECK(said("Jade wins the match."), "the foe's second round is the match, said with the foe's name");
    clear();
    w16(M->p1_health_addr, M->health_full); w16(M->p2_health_addr, M->health_full); frames(2);
    CHECK(saidCount("Round") == 0, "no next round is announced after the match is over");

    // ---- the pause ---------------------------------------------------------------------------------
    clear();
    w32(M->p1_wins_addr, 0); w32(M->p2_wins_addr, 0);
    w8(M->pause_addr, M->pause_value); frames(1);
    CHECK(said("Paused."), "pausing is said");
    clear();
    w16(M->p2_health_addr, 30); frames(3);
    CHECK(NSPOKEN == 0, "nothing under a pause speaks");
    CHECK(A->cue_snapshot(&cs) && !cs.locked, "the beacon is silent while paused");
    clear();
    A->command(oga::Command::MenuState);
    CHECK(said("Paused."), "the menu key says Paused");
    clear();
    w8(M->pause_addr, 0); frames(1);
    CHECK(said("Resumed."), "resuming is said");
    clear();
    w8(M->pause_addr, M->pause_value); frames(1);
    CHECK(said("Paused."), "pausing twice is two pauses, not a repeat");
    // the pause menu: Select opens Continue/Quit, Quit opens a Yes/No confirm (stack-slot highlights)
    clear();
    w32(M->pause_menu_addr, 0x801); frames(1);
    CHECK(said("Pause menu. Continue."), "the pause menu opening says its first row");
    clear();
    w32(M->pause_menu_addr, 0x800); frames(1);
    CHECK(said("Quit.") && !said("Pause menu"), "moving to Quit says the row only");
    clear();
    w32(M->pause_confirm_addr, 0x801); frames(1);
    CHECK(said("Quit game? No."), "the confirm opens on No");
    clear();
    w32(M->pause_confirm_addr, 0x800); frames(1);
    CHECK(said("Quit game? Yes."), "and moves to Yes");
    clear();
    A->command(oga::Command::MenuState);
    CHECK(said("Paused. Quit game? Yes."), "the menu key reads the confirm row");
    clear();
    w32(M->pause_confirm_addr, 0); w32(M->pause_menu_addr, 0x801); frames(1);
    CHECK(said("Continue."), "backing out of the confirm says the menu row again");
    clear();
    w32(M->pause_menu_addr, 0x1234); frames(2);
    CHECK(NSPOKEN == 0, "a stack value that is not a highlight says nothing");
    w32(M->pause_menu_addr, 0);
    w8(M->pause_addr, 0); frames(1);

    // ---- slots swap between fights -------------------------------------------------------------------
    setScreen(oga::kMktScreenOther); w8(M->fight_addr, 0); frames(20);
    clear();
    startFight(31, 7, true);                          // Johnny Cage against Sektor, objects in the other slots
    setX(kObjB, 432); setX(kObjA, 559);               // you (slot B) are on the left
    frames(25);
    CHECK(said("Round 1. Johnny Cage versus Sektor."), "the next fight names its own fighters");
    clear();
    A->command(oga::Command::WhereAmI);
    CHECK(said("on your right"), "with the objects in the other slots the foe is still on the right");
    CHECK(A->cue_snapshot(&cs) && cs.locked, "and the beacon still reads the pair through the pointers");

    // an empty slot (no object between fights) is not a position
    w32(M->p1_obj_ptr_addr, 0);
    clear();
    frames(3);
    CHECK(A->cue_snapshot(&cs) && !cs.locked, "a null object pointer is not a position");

    // ---- continue screen ------------------------------------------------------------------------------
    w8(M->fight_addr, 0);
    setScreen(oga::kMktScreenContinue); frames(2);   // the fight screen settled first
    clear();
    setScreen(oga::kMktScreenFight); w8(M->fight_addr, M->fight_value); frames(20);
    clear();
    w8(M->fight_addr, 0);
    setScreen(oga::kMktScreenContinue); frames(20);
    CHECK(said("Continue? Press start."), "the continue screen after a fight is said");
    clear();
    setScreen(oga::kMktScreenOther); frames(20);
    setScreen(oga::kMktScreenContinue); frames(20);
    CHECK(NSPOKEN == 0, "all-zero frames that do not follow a fight are not a continue screen");

    // ---- the commands that have nothing to cycle -------------------------------------------------------
    clear();
    A->command(oga::Command::NextEnemy);
    CHECK(said("Not mapped yet"), "NextEnemy refuses honestly (one foe, nothing to cycle)");
    clear();
    A->command(oga::Command::MenuNext);
    CHECK(NSPOKEN == 0, "menu echoes are ignored silently");

    // ---- the dump ---------------------------------------------------------------------------------------
    memcpy(&RAM[OFF(0x800F846A)], "MORTAL", 7);
    NLOGGED = 0; clear();
    A->command(oga::Command::DumpState);
    bool hitLogged = false, mapLogged = false;
    for (int i = 0; i < NLOGGED; i++) {
        if (strstr(LOGGED[i], "\"KOMBAT\"") == nullptr && strstr(LOGGED[i], "map:")) mapLogged = true;
        if (strstr(LOGGED[i], "scan done")) hitLogged = true;
    }
    CHECK(mapLogged && hitLogged, "the dump logs the map state and the scan");
    CHECK(said("Memory scan written"), "the dump says where it wrote");

    // ---- a map with nothing set switches everything off ------------------------------------------------
    static const oga::MktMap kEmpty = {};
    oga::MktSetMap(&kEmpty);
    M = oga::MktCurrentMap();
    clear();
    setScreen(oga::kMktScreenMain);                    // addresses are 0, so these wrote nothing useful
    w8(0x800283BC, 2); w8(0x80028408, 2);
    frames(40);
    CHECK(NSPOKEN == 0, "an unset map speaks nothing");
    A->command(oga::Command::WhereAmI);
    CHECK(said("Not mapped yet"), "an unset map says it is not mapped");
    CHECK(A->cue_snapshot && !A->cue_snapshot(&cs), "an unset map gives no cue");
    oga::MktSetMap(nullptr);
    M = oga::MktCurrentMap();
    CHECK(M->fight_addr == 0x80032478, "passing nullptr restores the shipped map");

    A->detach();
    printf("mkt-adapter: %s (%d checks)\n", fail ? "FAIL" : "PASS", pass + fail);
    return fail ? 1 : 0;
}
