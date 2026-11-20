/*
 * dissidia012_adapter.cpp — Dissidia 012 Final Fantasy (PSP, ULUS10566) adapter, first cut.
 *
 * STATUS: the title/setup/main menus, the field position query and the gateway
 * board reader (session 11) speak. Battle is not wired yet.
 * Every address below is STATIC (decrypted EBOOT, Ghidra project oga-ghidra-dissidia012)
 * and is NOT runtime-confirmed. Do not carry an address from dissidia_adapter.cpp
 * (ULUS10437) into this file: 012 loads at 0x08804040, has three overlays linked at
 * 0x089dd4c0, and its board and battle code was rewritten. See
 * docs/research/dissidia-012-start.md for the carry-over survey.
 *
 * Plumbing that survived byte-for-byte (masked-opcode match), so the original's
 * menu-manager READ PATH should port once the live addresses are re-found:
 *   menu selection validator   0x088d75f0   (orig FUN_00250538)
 *   menu event dispatcher      0x088cf550   (orig FUN_0024932c)
 *   menu node lookup           0x088da828   (orig FUN_0025468c)
 *   menu input consumer        0x088ea070   (orig FUN_00267cb8)
 *   pad read / input sink      0x08871e24 / 0x08871bd0
 *   event poster / SFX poster  0x088a9de8 / 0x0882fa10
 * In OVL_BATTLE_APP (only valid while that overlay is resident):
 *   talk-event manager init    0x08abe9f0   EX pickup poster 0x08a3abc4
 */
#include "adapter.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

namespace oga {
namespace dissidia012 {

static const Host* g_host = nullptr;

static uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

// Instruction words 1 and 2 of three resident EBOOT functions, read from the
// decrypted 012 EBOOT. All three must match: a different build, or a title that
// is not 012, fails closed and the registry falls through.
struct Fp { uint32_t addr, w1, w2; };
// Words 1 and 2, NOT word 0: PPSSPP overwrites a compiled function's first word with its
// own 0x68-prefixed marker, so word 0 changes at runtime (measured live on the pad read).
static const Fp kFingerprint[3] = {
    { 0x08871e24u, 1007028381u, 3301742380u },   // pad read
    { 0x088a9de8u, 2946826240u, 16797733u },   // event poster
    { 0x088d75f0u, 2947547136u, 2947612676u },   // menu selection validator
};

static bool BuildOk(void)
{
    for (const Fp& f : kFingerprint)
        if (u32(f.addr + 4) != f.w1 || u32(f.addr + 8) != f.w2) return false;
    return true;
}

static bool Attach(const Host* host)
{
    g_host = host;
    if (!BuildOk()) {
        if (host && host->log) host->log(host->ctx, "dissidia012: build fingerprint mismatch");
        g_host = nullptr;
        return false;
    }
    if (host && host->log) host->log(host->ctx, "dissidia012: attached (ULUS10566, static map only)");
    return true;
}

// ---- Title menu (RUNTIME-CONFIRMED headless, two boots, USA CSO) ----
// New Game / Load Game / Extras. Cursor u32 0..2 at 0x096AE20C: stepped Down x3 read
// 0,1,2,0 and Up read 2; Cross on 2 opened the Extras screen (screenshot). The neighbours
// below fingerprint the struct so a stale heap word is never spoken. Heap address, so a
// mismatch fails closed.
static const uint32_t TITLE_CURSOR = 0x096AE20Cu;
static const char* kTitleEntries[3] = { "New Game", "Load Game", "Extras" };

static int TitleIndex(void)
{
    if (!g_host) return -1;
    if (u32(TITLE_CURSOR - 4) != 65793u) return -1;
    if (u32(TITLE_CURSOR + 4) != 16447u) return -1;
    if (u32(TITLE_CURSOR + 8) != 0xFFFFFFFFu) return -1;
    uint32_t i = u32(TITLE_CURSOR);
    return i <= 2 ? (int) i : -1;
}

// ---- Data Config (New Game > first screen), RUNTIME-CONFIRMED headless on two boots ----
// "Create New Data" / "Transfer save data". Cursor u32 0..1 at 0x096AEA70; Down, Down, Up
// read 1, 0, 1. Fingerprint: word -4 == 8 and word +8 == 1 (the max index). Heap address,
// fails closed on mismatch. Names are the on-screen labels (screenshot-verified).
static const uint32_t CFG_CURSOR = 0x096AEA70u;
static const uint32_t SETUP_ID = 0x096AEC1Cu;
static const char* kCfgEntries[2] = { "Create New Data", "Transfer save data" };

static int ConfigIndex(void)
{
    if (!g_host || TitleIndex() >= 0) return -1;
    if (u32(CFG_CURSOR - 4) != 8u || u32(CFG_CURSOR + 8) != 1u) return -1;
    { uint32_t id = u32(SETUP_ID); if (id >= 10 && id <= 18) return -1; }
    uint32_t i = u32(CFG_CURSOR);
    return i <= 1 ? (int) i : -1;
}

static void SayGrouped(const char* s, const char* group, oga::Priority pri)
{
    if (g_host->announce_q)
        oga::announce(g_host->announce_q, oga::Announcement{s, pri, group, nullptr, 0, 0, -1}, g_host->now_ms);
    else if (g_host->speak)
        g_host->speak(g_host->ctx, s, pri == oga::Priority::High);
}

static void SayLine(const char* s, oga::Priority pri) { SayGrouped(s, "menu", pri); }
static void SayBoard(const char* s, oga::Priority pri) { SayGrouped(s, "board", pri); }

// ---- Setup screens after the nickname (RUNTIME-CONFIRMED headless, one boot, labels by screenshot) ----
// All share the cursor at 0x096AEA70 (max index at +8). Screen id u32 at 0x096AEC1C:
// 10 Play Plan (max 2), 12 Bonus Day (6), 14 Battle Mode (1), 16 Story Mode (2), 18 confirm (1).
// Cursor sequences measured: Plan 0,1,2,1,0; Bonus 0,1,2,1,0; Battle 0,1,0,1,0; Story 0,1,2,1,0.
// Confirm screen (id 18): same cursor, driven by Left/Right.
struct SetupScreen { uint32_t id, maxIdx; int n; const char* rows[7]; };
static const SetupScreen kSetup[5] = {
    { 10, 2, 3, { "Casual", "Average", "Hardcore" } },
    { 12, 6, 7, { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" } },
    { 14, 1, 2, { "Action-style", "RPG Mode" } },
    { 16, 2, 3, { "Standard", "Strong", "Moderate" } },
    // Confirm "Proceed with these settings?": moves with Left/Right (Up/Down do nothing). 0 YES, 1 NO.
    { 18, 1, 2, { "Yes", "No" } },
};

static const SetupScreen* SetupNow(int* idx)
{
    if (!g_host || TitleIndex() >= 0) return nullptr;
    if (u32(CFG_CURSOR - 4) != 8u) return nullptr;
    uint32_t id = u32(SETUP_ID);
    for (const SetupScreen& sc : kSetup) {
        if (sc.id != id || u32(CFG_CURSOR + 8) != sc.maxIdx) continue;
        uint32_t i = u32(CFG_CURSOR);
        if (i >= (uint32_t) sc.n) return nullptr;
        *idx = (int) i;
        return &sc;
    }
    return nullptr;
}

// ---- Main menu (RUNTIME-CONFIRMED headless from a savestate, ONE boot; labels by screenshot) ----
// Cursor u32 0..6 at 0x096AEBEC, mirrored at 0x096AEC14; tag word 8 at 0x096AEBE8. Seven rows, PP Catalog
// (greyed out on screen) still takes a cursor step: Down x7 read 0..6 then 0, Up read 6.
// Silent unless the setup screen id is 0 and the mirror agrees.
static const uint32_t MAIN_CURSOR = 0x096AEBECu, MAIN_MIRROR = 0x096AEC14u;
static const char* kMainEntries[7] = { "Story Mode", "Battle Mode", "Communications Mode",
                                       "PP Catalog", "Collection", "Data Config", "Options" };
static int MainIndex(void)
{
    if (!g_host || TitleIndex() >= 0) return -1;
    if (u32(MAIN_CURSOR - 4) != 8u || u32(SETUP_ID) != 0u) return -1;
    uint32_t i = u32(MAIN_CURSOR);
    if (i > 6 || u32(MAIN_MIRROR) != i) return -1;
    return (int) i;
}
static int g_lastMain = -1;
// Only speak a row that is actually available. PP Catalog (row 3) is greyed out on a fresh save and the
// cursor still lands on it; the flag that enables it is NOT found yet, so it stays silent. Fail quiet.
static bool MainShowing(int row) { return row != 3; }


// ---- Field (world map) reader: STATIC chain, runtime-verified on four snapshots ----
// 0x089D83D8 (fixed global) -> controller at a FIXED 0x08B83714 -> +0x6D0 = player character object.
// In cutscenes the controller still exists but +0x6D0 reads 0, so that is the refuse-to-speak test.
// Player position: floats at +0x30 (x), +0x34 (y), +0x38 (z). Current CELL: s32 at controller+0x38 / +0x40.
// FUN_08916fa0 in the game is exactly "return *(0x089d83d8 + 0x6d0)", which is why this is safe to trust.
static const uint32_t FIELD_CTRL_HOLDER = 0x089D83D8u;
static const uint32_t CTRL_PLAYER = 0x6D0u, CTRL_CELL_X = 0x38u, CTRL_CELL_Y = 0x40u;
static const uint32_t PL_OFF_X = 0x30u, PL_OFF_Y = 0x34u, PL_OFF_Z = 0x38u;

static uint32_t FieldPlayer(void)
{
    if (!g_host) return 0;
    uint32_t ctrl = u32(FIELD_CTRL_HOLDER);
    if (ctrl < 0x08800000u || ctrl >= 0x0A000000u) return 0;
    uint32_t pl = u32(ctrl + CTRL_PLAYER);
    if (pl < 0x08800000u || pl >= 0x0A000000u) return 0;
    return pl;
}

static float f32at(uint32_t a)
{
    uint32_t w = u32(a);
    float f;
    __builtin_memcpy(&f, &w, sizeof f);
    return f;
}

static bool FieldPose(float* x, float* y, float* z, int* cx, int* cy)
{
    uint32_t pl = FieldPlayer();
    if (!pl) return false;
    float fx = f32at(pl + PL_OFF_X), fy = f32at(pl + PL_OFF_Y), fz = f32at(pl + PL_OFF_Z);
    // Refuse garbage: NaN or absurd magnitudes mean we are not on the field.
    if (!(fx == fx && fy == fy && fz == fz)) return false;
    if (fx < -20000.0f || fx > 20000.0f || fz < -20000.0f || fz > 20000.0f) return false;
    uint32_t ctrl = u32(FIELD_CTRL_HOLDER);
    *x = fx; *y = fy; *z = fz;
    *cx = (int) u32(ctrl + CTRL_CELL_X);
    *cy = (int) u32(ctrl + CTRL_CELL_Y);
    return true;
}

static int g_lastCellX = INT32_MIN, g_lastCellY = INT32_MIN;

static void SayField(bool high)
{
    float x, y, z; int cx, cy;
    if (!FieldPose(&x, &y, &z, &cx, &cy)) {
        SayLine("Not on the field.", high ? oga::Priority::High : oga::Priority::Normal);
        return;
    }
    char line[96];
    snprintf(line, sizeof line, "On the field. Position %d, %d.", (int) x, (int) z);
    SayLine(line, high ? oga::Priority::High : oga::Priority::Normal);
    (void) cx; (void) cy;   // +0x38/+0x40 are NOT map cells (never changed over a wide roam) - not spoken
}


// ---- Gateway board reader (RUNTIME-CONFIRMED, sessions 8-14) ----
// Board mode is ctrl state 20 (controller chain as the field: *(0x089D83D8) -> ctrl,
// state u32 at ctrl+0x68) for boards entered from the field, and state 21 for boards
// entered from the chapter shell (session 13: Chapter 1's Crescent Lake board plays
// at 21). The chapter-select shell is ALSO state 21, so the state alone never turns
// the reader on: the board cells must agree with each other -- the board manager's
// current-record pointer (0x095D123C) must equal the record implied by the grid byte
// (0x08B82A24) -- during the entry transition the pointer is still 0 while the grid
// byte already holds its init value, and in the chapter shell the pointer is
// 0xFFFFFFFF (both measured). Anything else fails closed and silent.
//   current disc: grid id u32 at 0x08B82A24 (10*(row+2)+col on a virtual 10-wide grid)
//   disc records: 0x08B82A30 + 8*grid (exists flag + object pointer); the first
//     60 records were the session-10 characterisation, but sessions 25 and 35
//     measured live records at grids 60-64 (Encounters and Treason, Seasonless,
//     Chapter 3's first gateway, North Melmond, Southern Shrine), so every scan
//     below runs to grid 70.
//     the set of existing grids is the board's fingerprint (session 13: boards are
//     told apart by their disc-table fingerprint, not by an identity cell)
//   disc objects: 0x095CD570 + 0x40*grid, board X float at +0x00, Z at +0x08
//   Engage prompt target: focus pointer at 0x095CD410 -- the target disc's record while
//     a prompt is up, her own disc's record at rest. At rest it can lag one engagement
//     behind (session 10), so prompts are announced on the CHANGE to a prompt disc,
//     never level-triggered.
//   Scan Mode cursor: record pointer at 0x095CD41C, 0xFFFFFFFF when not scanning.
// A board whose fingerprint matches a known table (the prologue's Gateway to
// Departure, Chapter 1's Crescent Lake and Sage's Path) is announced from it. An unknown
// board is announced GENERICALLY from the live disc data -- current disc and
// coordinate-adjacent neighbours by grid id, no piece names, no prompts -- because
// piece presence is not in a cell and an unverified name would be an invention.
// Piece presence is not in a cell on known boards either: a consumed piece (chest
// looted, Kain recruited, a manikin defeated) is still named in arrival/scan lines
// until a presence cell is found. Prompts never suffer from this -- a consumed piece
// raises no prompt, so the focus pointer never targets it.
// KP and Gil ARE readable (sessions 13-14) and are announced on CHANGE only:
//   Gil: u32 at 0x08BBFA00, mirrored at 0x08BBFA28 (the pair must agree; verified
//     50 -> 64 -> 78 across Chapter 1 battles, and 50 on the prologue board).
//   KP (the gateway gauge, not the banked total): a static holder chain --
//     *(0x089D65A8) is the gauge struct's base in the game's pointer table (the
//     twin slot 0x089D85BC must agree), and the gauge is the u32 at base+0x260.
//     Verified across 124 RAM dumps in two lineages (0/1/held-1/2 through Chapter
//     1's battles, 0 on the prologue board) and live in the harness. Sanity caps
//     keep transition garbage (one measured dump reads 0xFFFFFF9E there) silent.
// Baselines are taken silently when a board is identified and kept across the
// gate breaks a battle causes, so a win's deltas announce on the return to the
// board; they reset when the visit ends or a different board is identified.
static const uint32_t BOARD_GRID_ID = 0x08B82A24u;
static const uint32_t BOARD_MGR_CUR = 0x095D123Cu;
static const uint32_t BOARD_RECORDS = 0x08B82A30u;
static const uint32_t BOARD_OBJECTS = 0x095CD570u;
static const uint32_t BOARD_FOCUS = 0x095CD410u;
static const uint32_t BOARD_SCAN_PTR = 0x095CD41Cu;
static const uint32_t CTRL_STATE_OFF = 0x68u;
static const uint32_t KP_HOLDER = 0x089D65A8u;
static const uint32_t KP_HOLDER2 = 0x089D85BCu;
static const uint32_t KP_OFF = 0x260u;
static const uint32_t GIL_A = 0x08BBFA00u;
static const uint32_t GIL_B = 0x08BBFA28u;

enum BoardPiece : int { kPieceNone = 0, kPieceChest, kPieceKain, kPieceManikin, kPieceStigma,
                        kPieceReaper, kPieceFlash, kPieceLaguna, kPieceDragoon,
                        kPieceDragoon2, kPieceFlash6, kPieceFlashLunar, kPieceSkyPirate,
                        kPieceTifa, kPieceUnknown,
                        kPieceFlash2Wod, kPiecePirate6Ocs, kPieceDragoon2Wod, kPieceGirl2Os, kPieceDragoon2Os, kPiecePirate2Wod,
                        kPieceHarlequin5Os, kPieceWraith1Os, kPieceFlash5Wod, kPieceVision1Os, kPieceGunner1Os, kPiecePhantom3Ocs,
                        kPieceDespot1Os, kPieceSummoner1Os, kPieceDragoon3Ocs, kPieceGirl15, kPiecePirate11, kPieceHarlequin15,
                        kPieceFlash15, kPiecePirate15, kPieceHero13, kPieceSummoner13, kPiecePhantom23, kPieceVision11Lunar,
                        kPiecePhantom11Lunar, kPieceHero11Lunar, kPieceDespot6Dreams, kPieceSummoner11Lunar, kPiecePhantom6Dreams, kPieceHero6Wod,
                        kPieceDragoon3, kPieceGunner3, kPieceDespot7, kPieceFlash7, kPieceGirl13Os, kPiecePirate13Wod,
                        kPieceDragoon6Crystal, kPieceGunner13Dreams, kPiecePirate5Lunar90, kPieceDragoon5Lunar90, kPieceReaper15Os90, kPieceHarlequin2Wod90,
                        kPieceFlash2Wod90, kPieceHarlequin7Lunar90, kPiecePirate11Ocs90, kPiecePirate7Wod90, kPieceFlash11Ocs90, kPieceWraith5_90,
                        kPieceDragoon4_90, kPieceGunner1_90, kPieceFlash5_90, kPieceFlash1_90, kPieceHero1Rift90, kPieceSummoner1Rift90,
                        kPieceDespot3Ocs90, kPieceFlash3Dreams90, kPiecePhantom3Dreams90, kPiecePhantom11Wod90, kPiecePirate11Lunar90, kPieceWraith11Lunar90,
                        kPieceFlash6Dreams90, kPieceDragoon8_90, kPieceFlash8_90, kPiecePirate13_90, kPieceHero13_90, kPieceDespot13_90,
                        kPieceTree7Wod90, kPieceFlash7Crystal90, kPieceDragoon7Crystal90, kPieceFlash3Ultimecia90, kPieceGunner6Crystal90, kPieceTree13Wod90,
                        kPieceFlash13Ultimecia90, kPieceTerra, kPieceVaan, kPieceJecht, kPieceZidane, kPieceFirion,
                        kPieceSummonstone, kPiecePotion, kPieceWarriorOfLight,
                        kPieceWarriorOfLight };
struct BoardPieceInfo { const char* name; const char* clause; const char* desc; const char* promptDesc; bool prompt; };
static const BoardPieceInfo kBoardPieces[96] = {
    { "", "", "", nullptr, false },
    { "Treasure Chest", "chest", "Contains helpful items", nullptr, true },
    { "Kain", "Kain", "Assist Character", nullptr, true },
    { "Enemy manikin", "enemy manikin", "", nullptr, false },
    { "Stigma of Chaos", "Stigma of Chaos", "Engaging this piece finishes the level", nullptr, true },
    // Chapter 1 battle pieces (session 13): the prompt adds the KP chance line the
    // board shows at its top-left; Scan Mode shows name, Lv and stage only.
    { "Capricious Reaper", "Capricious Reaper", "Lv 2. Battle stage: Old Chaos Shrine",
      "Lv 2. Battle stage: Old Chaos Shrine. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 2. Battle stage: Old Chaos Shrine",
      "Lv 2. Battle stage: Old Chaos Shrine. KP chance: Win battle", true },
    { "Laguna", "Laguna", "Assist Character", nullptr, true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 6. Battle stage: Order's Sanctuary",
      "Lv 6. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    // Sage's Path pieces (session 15's verified Scan pass; the battle prompts'
    // KP chance line is the one every measured battle prompt has shown).
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 2. Battle stage: Lunar Subterrane",
      "Lv 2. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 6. Battle stage: Order's Sanctuary",
      "Lv 6. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 2. Battle stage: Lunar Subterrane",
      "Lv 2. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 6. Battle stage: Order's Sanctuary",
      "Lv 6. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Tifa", "Tifa", "Assist Character", nullptr, true },
    { "???", "???", "Engage this piece to find its true form", nullptr, true },
    // Boards of Chapters 1-6 (sessions 21-64, consolidated): every entry is a
    // piece as the game presented it -- prompt or Scan text. The KP chance is
    // per-board, so a foe identical in name, level and stage still needs its
    // own entry when its board's chance differs (the 90-suffixed entries are
    // the "Win within 90 seconds" boards). A description with no stage line
    // means the record never stated that piece's stage; none was invented.
    { "Fleeting Flash", "Fleeting Flash", "Lv 2. Battle stage: World of Darkness",
      "Lv 2. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 6. Battle stage: Old Chaos Shrine",
      "Lv 6. Battle stage: Old Chaos Shrine. KP chance: Win battle", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 2. Battle stage: World of Darkness",
      "Lv 2. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Phantasmal Girl", "Phantasmal Girl", "Lv 2. Battle stage: Order's Sanctuary",
      "Lv 2. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 2. Battle stage: Order's Sanctuary",
      "Lv 2. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 2. Battle stage: World of Darkness",
      "Lv 2. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Phantasmal Harlequin", "Phantasmal Harlequin", "Lv 5. Battle stage: Order's Sanctuary",
      "Lv 5. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Counterfeit Wraith", "Counterfeit Wraith", "Lv 1. Battle stage: Order's Sanctuary",
      "Lv 1. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 5. Battle stage: World of Darkness",
      "Lv 5. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Ephemeral Vision", "Ephemeral Vision", "Lv 1. Battle stage: Order's Sanctuary",
      "Lv 1. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Transient Gunner", "Transient Gunner", "Lv 1. Battle stage: Order's Sanctuary",
      "Lv 1. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 3. Battle stage: Old Chaos Shrine",
      "Lv 3. Battle stage: Old Chaos Shrine. KP chance: Win battle", true },
    { "Imitation Despot", "Imitation Despot", "Lv 1. Battle stage: Order's Sanctuary",
      "Lv 1. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Ephemeral Summoner", "Ephemeral Summoner", "Lv 1. Battle stage: Order's Sanctuary",
      "Lv 1. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 3. Battle stage: Old Chaos Shrine",
      "Lv 3. Battle stage: Old Chaos Shrine. KP chance: Win battle", true },
    { "Phantasmal Girl", "Phantasmal Girl", "Lv 15",
      "Lv 15. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 11",
      "Lv 11. KP chance: Win battle", true },
    { "Phantasmal Harlequin", "Phantasmal Harlequin", "Lv 15",
      "Lv 15. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 15",
      "Lv 15. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 15",
      "Lv 15. KP chance: Win battle", true },
    { "False Hero", "False Hero", "Lv 13",
      "Lv 13. KP chance: Win battle", true },
    { "Ephemeral Summoner", "Ephemeral Summoner", "Lv 13",
      "Lv 13. KP chance: Win battle", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 23",
      "Lv 23. KP chance: Win battle", true },
    { "Ephemeral Vision", "Ephemeral Vision", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "False Hero", "False Hero", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "Imitation Despot", "Imitation Despot", "Lv 6. Battle stage: Dream's End",
      "Lv 6. Battle stage: Dream's End. KP chance: Win battle", true },
    { "Ephemeral Summoner", "Ephemeral Summoner", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win battle", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 6. Battle stage: Dream's End",
      "Lv 6. Battle stage: Dream's End. KP chance: Win battle", true },
    { "False Hero", "False Hero", "Lv 6. Battle stage: World of Darkness",
      "Lv 6. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 3",
      "Lv 3. KP chance: Win battle", true },
    { "Transient Gunner", "Transient Gunner", "Lv 3",
      "Lv 3. KP chance: Win battle", true },
    { "Imitation Despot", "Imitation Despot", "Lv 7",
      "Lv 7. KP chance: Win battle", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 7",
      "Lv 7. KP chance: Win battle", true },
    { "Phantasmal Girl", "Phantasmal Girl", "Lv 13. Battle stage: Order's Sanctuary",
      "Lv 13. Battle stage: Order's Sanctuary. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 13. Battle stage: World of Darkness",
      "Lv 13. Battle stage: World of Darkness. KP chance: Win battle", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 6. Battle stage: Crystal World",
      "Lv 6. Battle stage: Crystal World. KP chance: Win battle", true },
    { "Transient Gunner", "Transient Gunner", "Lv 13. Battle stage: Dream's End",
      "Lv 13. Battle stage: Dream's End. KP chance: Win battle", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 5. Battle stage: Lunar Subterrane",
      "Lv 5. Battle stage: Lunar Subterrane. KP chance: Win within 90 seconds", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 5. Battle stage: Lunar Subterrane",
      "Lv 5. Battle stage: Lunar Subterrane. KP chance: Win within 90 seconds", true },
    { "Capricious Reaper", "Capricious Reaper", "Lv 15. Battle stage: Order's Sanctuary",
      "Lv 15. Battle stage: Order's Sanctuary. KP chance: Win within 90 seconds", true },
    { "Phantasmal Harlequin", "Phantasmal Harlequin", "Lv 2. Battle stage: World of Darkness",
      "Lv 2. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 2. Battle stage: World of Darkness",
      "Lv 2. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Phantasmal Harlequin", "Phantasmal Harlequin", "Lv 7. Battle stage: Lunar Subterrane",
      "Lv 7. Battle stage: Lunar Subterrane. KP chance: Win within 90 seconds", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 11. Battle stage: Old Chaos Shrine",
      "Lv 11. Battle stage: Old Chaos Shrine. KP chance: Win within 90 seconds", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 7. Battle stage: World of Darkness",
      "Lv 7. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 11. Battle stage: Old Chaos Shrine",
      "Lv 11. Battle stage: Old Chaos Shrine. KP chance: Win within 90 seconds", true },
    { "Counterfeit Wraith", "Counterfeit Wraith", "Lv 5",
      "Lv 5. KP chance: Win within 90 seconds", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 4",
      "Lv 4. KP chance: Win within 90 seconds", true },
    { "Transient Gunner", "Transient Gunner", "Lv 1",
      "Lv 1. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 5",
      "Lv 5. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 1",
      "Lv 1. KP chance: Win within 90 seconds", true },
    { "False Hero", "False Hero", "Lv 1. Battle stage: The Rift",
      "Lv 1. Battle stage: The Rift. KP chance: Win within 90 seconds", true },
    { "Ephemeral Summoner", "Ephemeral Summoner", "Lv 1. Battle stage: The Rift",
      "Lv 1. Battle stage: The Rift. KP chance: Win within 90 seconds", true },
    { "Imitation Despot", "Imitation Despot", "Lv 3. Battle stage: Old Chaos Shrine",
      "Lv 3. Battle stage: Old Chaos Shrine. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 3. Battle stage: Dream's End",
      "Lv 3. Battle stage: Dream's End. KP chance: Win within 90 seconds", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 3. Battle stage: Dream's End",
      "Lv 3. Battle stage: Dream's End. KP chance: Win within 90 seconds", true },
    { "Ephemeral Phantom", "Ephemeral Phantom", "Lv 11. Battle stage: World of Darkness",
      "Lv 11. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win within 90 seconds", true },
    { "Counterfeit Wraith", "Counterfeit Wraith", "Lv 11. Battle stage: Lunar Subterrane",
      "Lv 11. Battle stage: Lunar Subterrane. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 6. Battle stage: Dream's End",
      "Lv 6. Battle stage: Dream's End. KP chance: Win within 90 seconds", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 8",
      "Lv 8. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 8",
      "Lv 8. KP chance: Win within 90 seconds", true },
    { "Idle Sky Pirate", "Idle Sky Pirate", "Lv 13",
      "Lv 13. KP chance: Win within 90 seconds", true },
    { "False Hero", "False Hero", "Lv 13",
      "Lv 13. KP chance: Win within 90 seconds", true },
    { "Imitation Despot", "Imitation Despot", "Lv 13",
      "Lv 13. KP chance: Win within 90 seconds", true },
    { "Fallacious Tree", "Fallacious Tree", "Lv 7. Battle stage: World of Darkness",
      "Lv 7. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 7. Battle stage: Crystal World",
      "Lv 7. Battle stage: Crystal World. KP chance: Win within 90 seconds", true },
    { "Delusory Dragoon", "Delusory Dragoon", "Lv 7. Battle stage: Crystal World",
      "Lv 7. Battle stage: Crystal World. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 3. Battle stage: Ultimecia's Castle",
      "Lv 3. Battle stage: Ultimecia's Castle. KP chance: Win within 90 seconds", true },
    { "Transient Gunner", "Transient Gunner", "Lv 6. Battle stage: Crystal World",
      "Lv 6. Battle stage: Crystal World. KP chance: Win within 90 seconds", true },
    { "Fallacious Tree", "Fallacious Tree", "Lv 13. Battle stage: World of Darkness",
      "Lv 13. Battle stage: World of Darkness. KP chance: Win within 90 seconds", true },
    { "Fleeting Flash", "Fleeting Flash", "Lv 13. Battle stage: Ultimecia's Castle",
      "Lv 13. Battle stage: Ultimecia's Castle. KP chance: Win within 90 seconds", true },
    { "Terra", "Terra", "Assist Character", nullptr, true },
    { "Vaan", "Vaan", "Assist Character", nullptr, true },
    { "Jecht", "Jecht", "Assist Character", nullptr, true },
    { "Zidane", "Zidane", "Assist Character", nullptr, true },
    { "Firion", "Firion", "Assist Character", nullptr, true },
    { "Summonstone", "Summonstone", "An unknown summon is sealed within", nullptr, true },
    { "Potion", "Potion", "", nullptr, true },
    { "Warrior of Light", "Warrior of Light", "", nullptr, true },
};
struct BoardDisc { int grid; const char* name; int x, z; int piece; };
// x/z are the disc objects' board coordinates (session 10): 4.0 per step, +x east
// (Right), +z south (Down). Neighbours are the discs one step away -- coordinate
// adjacency reproduces the session-9 link list exactly, including I1-J and E-J.
// A null name means the disc is spoken by grid id ("disc 32"): the later boards
// have no letter names, and inventing some would add nothing.
static const BoardDisc kBoardDiscs[14] = {
    { 1,  "T2", -14, -12, kPieceNone },
    { 11, "T1", -14, -8,  kPieceNone },
    { 20, "A",  -18, -4,  kPieceNone },
    { 21, "B",  -14, -4,  kPieceNone },
    { 22, "C",  -10, -4,  kPieceNone },
    { 23, "D",  -6,  -4,  kPieceChest },
    { 24, "E",  -2,  -4,  kPieceManikin },
    { 25, "F",  2,   -4,  kPieceNone },
    { 26, "G",  6,   -4,  kPieceNone },
    { 27, "H",  10,  -4,  kPieceStigma },
    { 33, "I1", -6,  0,   kPieceNone },
    { 34, "J",  -2,  0,   kPieceKain },
    { 43, "I2", -6,  4,   kPieceNone },
    { 53, "I3", -6,  8,   kPieceNone },
};
// Chapter 1's Crescent Lake Gateway (session 13, coordinates verified against the
// live disc objects in session 14). Entry disc is 20. The Dragoon on 52 appears
// only after the chest scene on 42 fires; before that the disc holds the chest
// alone, which has no prompt of its own while the guard stands or before it
// arrives -- the table names the guard, the piece the prompt actually raises.
static const BoardDisc kLakeDiscs[19] = {
    { 2,  nullptr, -10, -12, kPieceNone },
    { 12, nullptr, -10, -8,  kPieceNone },
    { 16, nullptr, 6,   -8,  kPieceNone },
    { 20, nullptr, -18, -4,  kPieceNone },
    { 22, nullptr, -10, -4,  kPieceReaper },
    { 23, nullptr, -6,  -4,  kPieceNone },
    { 25, nullptr, 2,   -4,  kPieceNone },
    { 26, nullptr, 6,   -4,  kPieceNone },
    { 30, nullptr, -18, 0,   kPieceNone },
    { 31, nullptr, -14, 0,   kPieceFlash },
    { 32, nullptr, -10, 0,   kPieceNone },
    { 33, nullptr, -6,  0,   kPieceFlash },
    { 34, nullptr, -2,  0,   kPieceNone },
    { 35, nullptr, 2,   0,   kPieceNone },
    { 36, nullptr, 6,   0,   kPieceLaguna },
    { 42, nullptr, -10, 4,   kPieceNone },
    { 45, nullptr, 2,   4,   kPieceNone },
    { 52, nullptr, -10, 8,   kPieceDragoon },
    { 55, nullptr, 2,   8,   kPieceStigma },
};
// Chapter 1's Sage's Path Gateway (session 15, full Scan pass; coordinates are
// the live disc objects'). Entry disc is 20. There is NO Stigma of Chaos on this
// board as entered -- the ??? piece on disc 7 is the presumptive boss gate -- so
// the definition's stigmaGrid is -1 and identification rests on the disc set plus
// the entry disc's coordinates. Chests stand on 2, 16 and 42; discs 2 and 42 sit
// behind Lv 6 battle pieces (12/3 and 32) and cannot be reached without fighting
// them, while 16 is on the open path.
static const BoardDisc kSagesDiscs[22] = {
    { 2,  nullptr, -10, -12, kPieceChest },
    { 3,  nullptr, -6,  -12, kPieceDragoon },
    { 4,  nullptr, -2,  -12, kPieceNone },
    { 6,  nullptr, 6,   -12, kPieceNone },
    { 7,  nullptr, 10,  -12, kPieceUnknown },
    { 12, nullptr, -10, -8,  kPieceSkyPirate },
    { 13, nullptr, -6,  -8,  kPieceNone },
    { 14, nullptr, -2,  -8,  kPieceNone },
    { 16, nullptr, 6,   -8,  kPieceChest },
    { 20, nullptr, -18, -4,  kPieceNone },
    { 21, nullptr, -14, -4,  kPieceNone },
    { 22, nullptr, -10, -4,  kPieceNone },
    { 23, nullptr, -6,  -4,  kPieceNone },
    { 24, nullptr, -2,  -4,  kPieceNone },
    { 25, nullptr, 2,   -4,  kPieceDragoon2 },
    { 26, nullptr, 6,   -4,  kPieceFlashLunar },
    { 32, nullptr, -10, 0,   kPieceFlash6 },
    { 33, nullptr, -6,  0,   kPieceNone },
    { 34, nullptr, -2,  0,   kPieceNone },
    { 35, nullptr, 2,   0,   kPieceTifa },
    { 42, nullptr, -10, 4,   kPieceChest },
    { 44, nullptr, -2,  4,   kPieceNone },
};
struct BoardDef { const BoardDisc* discs; int nDiscs; int entryGrid; int stigmaGrid; };
static const BoardDef kPrologueDef = { kBoardDiscs, 14, 21, 27 };
static const BoardDef kLakeDef = { kLakeDiscs, 19, 20, 55 };
static const BoardDef kSagesDef = { kSagesDiscs, 22, 20, -1 };
static const BoardDef* const kKnownBoards[3] = { &kPrologueDef, &kLakeDef, &kSagesDef };
// Generic fallback for an unrecognised board: position from the live disc data,
// no table, no pieces.
static const BoardDef kGenericDef = { nullptr, 0, -1, -1 };

static const BoardDisc* DiscForGrid(const BoardDef* def, int grid)
{
    if (!def) return nullptr;
    for (int i = 0; i < def->nDiscs; i++) if (def->discs[i].grid == grid) return &def->discs[i];
    return nullptr;
}
static const BoardDisc* DiscAt(const BoardDef* def, int x, int z)
{
    if (!def) return nullptr;
    for (int i = 0; i < def->nDiscs; i++) if (def->discs[i].x == x && def->discs[i].z == z) return &def->discs[i];
    return nullptr;
}
static int GridForRecord(uint32_t rec)
{
    if (rec < BOARD_RECORDS || rec >= BOARD_RECORDS + 70u * 8u) return -1;
    uint32_t off = rec - BOARD_RECORDS;
    return (off % 8u) ? -1 : (int) (off / 8u);
}
static int BoardCtrlState(void)
{
    if (!g_host) return -1;
    uint32_t ctrl = u32(FIELD_CTRL_HOLDER);
    if (ctrl < 0x08800000u || ctrl >= 0x0A000000u) return -1;
    return (int) u32(ctrl + CTRL_STATE_OFF);
}
static bool DiscExists(int grid)
{
    return grid >= 0 && grid < 70 && u32(BOARD_RECORDS + 8u * (uint32_t) grid) == 1u;
}
static bool DiscCoords(int grid, int* x, int* z)
{
    if (grid < 0 || grid >= 70) return false;
    uint32_t obj = BOARD_OBJECTS + 0x40u * (uint32_t) grid;
    float fx = f32at(obj), fz = f32at(obj + 8);
    if (!(fx == fx && fz == fz)) return false;
    if (fx < -400.0f || fx > 400.0f || fz < -400.0f || fz > 400.0f) return false;
    *x = (int) (fx < 0 ? fx - 0.5f : fx + 0.5f);
    *z = (int) (fz < 0 ? fz - 0.5f : fz + 0.5f);
    return true;
}
// Identify the loaded board by its disc-table fingerprint: the set of existing
// grids must equal a known board's disc set exactly, and the entry disc's live
// coordinates must match the table -- and the Stigma disc's too, on boards that
// have one (a same-shaped later board with different geometry must not inherit
// this board's piece names).
static const BoardDef* IdentifyBoard(void)
{
    for (const BoardDef* def : kKnownBoards) {
        bool ok = true;
        for (int i = 0; i < def->nDiscs && ok; i++) if (!DiscExists(def->discs[i].grid)) ok = false;
        if (!ok) continue;
        // No disc outside the definition's set may exist either. The scan covers
        // the characterised 0..59 range; a definition's own discs past 59 were
        // checked just above. Records past 59 were only ever measured as the
        // loaded board's own discs (sessions 25/35), never as another board's,
        // so an unlisted record there cannot smuggle a wrong table in.
        for (int g = 0; g < 60 && ok; g++)
            if (DiscExists(g) && !DiscForGrid(def, g)) ok = false;
        if (!ok) continue;
        int x, z;
        const BoardDisc* e = DiscForGrid(def, def->entryGrid);
        if (!e) continue;
        if (!DiscCoords(e->grid, &x, &z) || x != e->x || z != e->z) continue;
        // A board whose Stigma is not on it yet (Sage's Path at entry) carries
        // stigmaGrid -1: no Stigma coordinate check applies to it.
        if (def->stigmaGrid >= 0) {
            const BoardDisc* s = DiscForGrid(def, def->stigmaGrid);
            if (!s) continue;
            if (!DiscCoords(s->grid, &x, &z) || x != s->x || z != s->z) continue;
        }
        return def;
    }
    return nullptr;
}
// True only when the board cells agree with each other. A known board is named
// by its fingerprint; an unknown one falls back to the generic definition, and
// only when the current grid is a real disc.
static bool BoardCells(int* gridOut, const BoardDef** defOut)
{
    uint32_t g = u32(BOARD_GRID_ID);
    if (g >= 70u) return false;
    if (u32(BOARD_MGR_CUR) != BOARD_RECORDS + 8u * g) return false;
    const BoardDef* def = IdentifyBoard();
    if (def) {
        if (!DiscForGrid(def, (int) g)) return false;
    } else {
        if (!DiscExists((int) g)) return false;
        def = &kGenericDef;
    }
    *gridOut = (int) g;
    *defOut = def;
    return true;
}
static bool IsPromptDisc(const BoardDef* def, int grid)
{
    const BoardDisc* d = DiscForGrid(def, grid);
    return d && d->piece != kPieceNone && kBoardPieces[d->piece].prompt;
}
// The KP gauge through its holder chain; false when the chain or the value is
// not sane (a fresh boot's uninitialised table, or transition garbage).
static bool BoardKP(int* out)
{
    uint32_t base = u32(KP_HOLDER);
    if (base < 0x09000000u || base >= 0x0A000000u) return false;
    uint32_t twin = u32(KP_HOLDER2);
    if (twin != 0 && twin != base) return false;
    uint32_t v = u32(base + KP_OFF);
    if (v > 9999u) return false;
    *out = (int) v;
    return true;
}
static bool BoardGil(int* out)
{
    uint32_t a = u32(GIL_A), b = u32(GIL_B);
    if (a != b || a > 99999999u) return false;
    *out = (int) a;
    return true;
}

static void Appendf(char* buf, size_t n, size_t* off, const char* fmt, ...)
{
    if (*off >= n) return;
    va_list ap; va_start(ap, fmt);
    int w = vsnprintf(buf + *off, n - *off, fmt, ap);
    va_end(ap);
    if (w > 0) *off += (size_t) w;
}

// Session 9's arrival announceable: current disc identity + what each linked disc
// holds. E.g. "On C. Left: B, the entry disc. Right: chest on D. Up: nothing.
// Down: nothing." Hop sounds are the game's own cue and are NOT spoken.
// A disc's spoken name: the table name on the prologue board, "disc N" elsewhere.
static void DiscName(const BoardDef* def, int grid, char* buf, size_t n)
{
    const BoardDisc* d = DiscForGrid(def, grid);
    if (d && d->name) { snprintf(buf, n, "%s", d->name); return; }
    snprintf(buf, n, "disc %d", grid);
}

// Neighbour lookup: a known board answers from its table; the generic board
// answers from the live disc objects' coordinates.
static bool NeighbourDisc(const BoardDef* def, int grid, int dx, int dz, int* outGrid)
{
    if (def && def->discs) {
        const BoardDisc* cur = DiscForGrid(def, grid);
        if (!cur) return false;
        const BoardDisc* nd = DiscAt(def, cur->x + dx, cur->z + dz);
        if (!nd) return false;
        *outGrid = nd->grid;
        return true;
    }
    int cx, cz;
    if (!DiscCoords(grid, &cx, &cz)) return false;
    for (int g = 0; g < 70; g++) {
        if (g == grid || !DiscExists(g)) continue;
        int x, z;
        if (DiscCoords(g, &x, &z) && x == cx + dx && z == cz + dz) { *outGrid = g; return true; }
    }
    return false;
}

static void BoardArrival(const BoardDef* def, int grid, char* buf, size_t n)
{
    buf[0] = 0;
    char name[32];
    DiscName(def, grid, name, sizeof name);
    size_t off = 0;
    if (def && grid == def->entryGrid) Appendf(buf, n, &off, "On %s, the entry disc.", name);
    else Appendf(buf, n, &off, "On %s.", name);
    const BoardDisc* d = DiscForGrid(def, grid);
    if (d && d->piece != kPieceNone)
        Appendf(buf, n, &off, " %s here.", kBoardPieces[d->piece].name);
    static const struct { int dx, dz; const char* label; } kDirs[4] = {
        { -4, 0, "Left" }, { 4, 0, "Right" }, { 0, -4, "Up" }, { 0, 4, "Down" },
    };
    for (const auto& dir : kDirs) {
        int ng = -1;
        if (!NeighbourDisc(def, grid, dir.dx, dir.dz, &ng)) {
            Appendf(buf, n, &off, " %s: nothing.", dir.label);
            continue;
        }
        char nname[32];
        DiscName(def, ng, nname, sizeof nname);
        const BoardDisc* m = DiscForGrid(def, ng);
        if (m && m->piece != kPieceNone)
            Appendf(buf, n, &off, " %s: %s on %s.", dir.label, kBoardPieces[m->piece].clause, nname);
        else if (def && ng == def->entryGrid)
            Appendf(buf, n, &off, " %s: %s, the entry disc.", dir.label, nname);
        else
            Appendf(buf, n, &off, " %s: %s, empty.", dir.label, nname);
    }
}

static void BoardPromptLine(const BoardDef* def, int grid, char* buf, size_t n)
{
    const BoardDisc* d = DiscForGrid(def, grid);
    buf[0] = 0;
    if (!d || !IsPromptDisc(def, grid)) return;
    // The prompt's own text: name, description, Engage / Cancel (sessions 9/13/14).
    const BoardPieceInfo& p = kBoardPieces[d->piece];
    snprintf(buf, n, "%s — %s. Engage / Cancel", p.name, p.promptDesc ? p.promptDesc : p.desc);
}

static void BoardScanLine(const BoardDef* def, int grid, char* buf, size_t n)
{
    buf[0] = 0;
    char name[32];
    DiscName(def, grid, name, sizeof name);
    const BoardDisc* d = DiscForGrid(def, grid);
    if (!d || d->piece == kPieceNone) snprintf(buf, n, "Scan: %s.", name);
    else if (kBoardPieces[d->piece].desc[0])
        snprintf(buf, n, "Scan: %s. %s — %s.", name, kBoardPieces[d->piece].name, kBoardPieces[d->piece].desc);
    else snprintf(buf, n, "Scan: %s. %s.", name, kBoardPieces[d->piece].name);
}

static int g_boardGrid = -1;      // last announced current disc
static int g_boardPrompt = -1;    // disc whose Engage prompt is announced-open
static int g_boardScan = -2;      // disc under the scan cursor (-2 = not scanning)
static bool g_boardStigmaSeen = false;  // the Stigma prompt was the last prompt announced
static bool g_boardWasLive = false;
static int g_boardState = -1;           // ctrl state the board went live at (20 or 21)
static const BoardDef* g_boardDef = nullptr;  // board identified for this visit
static int g_boardKP = -1, g_boardGil = -1;   // KP/Gil baselines (-1 = not yet read)

static void BoardReset(void)
{
    g_boardGrid = -1; g_boardPrompt = -1; g_boardScan = -2; g_boardStigmaSeen = false;
    g_boardState = -1; g_boardDef = nullptr; g_boardKP = -1; g_boardGil = -1;
}

// KP/Gil change announcements: change-triggered only, spoken at Normal priority.
// KP and Gil have no game sound cue, so speaking their changes follows the
// sound-first rule. Baselines are taken silently when a board is identified and
// kept across the breaks a battle causes, so a win's deltas announce when board
// play resumes; insane readings (transition garbage) never move a baseline.
static void BoardMoneyWatch(bool fresh)
{
    int kp = -1, gil = -1;
    bool haveKP = BoardKP(&kp), haveGil = BoardGil(&gil);
    if (fresh) {
        g_boardKP = haveKP ? kp : -1;
        g_boardGil = haveGil ? gil : -1;
        return;
    }
    if (haveKP) {
        if (g_boardKP >= 0 && kp != g_boardKP) {
            char line[64];
            snprintf(line, sizeof line, "KP: %d.", kp);
            SayBoard(line, oga::Priority::Normal);
        }
        g_boardKP = kp;
    }
    if (haveGil) {
        if (g_boardGil >= 0 && gil != g_boardGil) {
            char line[64];
            snprintf(line, sizeof line, "Gil: %d.", gil);
            SayBoard(line, oga::Priority::Normal);
        }
        g_boardGil = gil;
    }
}

// Returns true when the board owned this frame (the menu watches stay out of it).
static bool BoardFrame(void)
{
    if (!g_host) return false;
    int st = BoardCtrlState();
    // A board that went live at state 20 exits into the story shell at state 21
    // (session 9), whatever the cells do on the way out: a Stigma engagement
    // finishes the level; Pause -> Exit Gateway lands in the same shell without
    // finishing. The newly unlocked chapter's NAME is not in a known cell, so the
    // landing is announced generically rather than invented.
    if (g_boardWasLive && g_boardState == 20 && st == 21) {
        g_boardWasLive = false;
        SayBoard(g_boardStigmaSeen ? "Level finished. Story Mode chapter select."
                                   : "Exited the gateway. Story Mode chapter select.",
                 oga::Priority::Normal);
        BoardReset();
        return false;
    }
    int grid = -1;
    const BoardDef* def = nullptr;
    if ((st == 20 || st == 21) && BoardCells(&grid, &def)) {
        bool fresh = !g_boardWasLive || def != g_boardDef;
        g_boardWasLive = true;
        if (fresh) {
            g_boardDef = def;
            g_boardState = st;
            g_boardGrid = -1; g_boardPrompt = -1; g_boardScan = -2; g_boardStigmaSeen = false;
        }
        if (grid != g_boardGrid) {
            g_boardGrid = grid;
            g_boardStigmaSeen = false;   // she moved on; a stale Stigma prompt no longer counts
            char line[384];
            BoardArrival(def, grid, line, sizeof line);
            if (line[0]) SayBoard(line, oga::Priority::Normal);
        }
        int fg = GridForRecord(u32(BOARD_FOCUS));
        if (fg >= 0 && fg != grid && IsPromptDisc(def, fg)) {
            if (g_boardPrompt != fg) {
                g_boardPrompt = fg;
                g_boardStigmaSeen = (fg == def->stigmaGrid);
                char line[192];
                BoardPromptLine(def, fg, line, sizeof line);
                if (line[0]) SayBoard(line, oga::Priority::Normal);
            }
        } else if (fg == grid) {
            g_boardPrompt = -1;
        }
        uint32_t sp = u32(BOARD_SCAN_PTR);
        int sg = (sp == 0xFFFFFFFFu) ? -2 : GridForRecord(sp);
        if (sg >= 0) {
            if (sg != g_boardScan) {
                g_boardScan = sg;
                char line[192];
                BoardScanLine(def, sg, line, sizeof line);
                if (line[0]) SayBoard(line, oga::Priority::Normal);
            }
        } else g_boardScan = -2;
        BoardMoneyWatch(fresh);
        return true;
    }
    if (g_boardWasLive && st != 20 && st != 21) {
        // Board play ended outside the board states: a state-21 board's Stigma
        // exit lands on the chapter's field segment (session 14 measured
        // 21 -> 0 -> 3). The landing's name is not in a cell, so only the finish
        // itself is announced. Pause -> Exit from a state-21 board keeps state 21
        // in the shell and is NOT detected here -- it stays silent (documented).
        g_boardWasLive = false;
        SayBoard(g_boardStigmaSeen ? "Level finished." : "Exited the gateway.",
                 oga::Priority::Normal);
        BoardReset();
        return false;
    }
    if (!g_boardWasLive) BoardReset();
    // Otherwise a board state whose cells disagree -- the entry transition, a
    // battle inside a state-21 board, or the shell after Pause -> Exit: the
    // reader stays silent and keeps its baselines for the return to the board.
    return false;
}

static int g_lastTitle = -1;
static int g_lastSetupId = -1, g_lastSetupIdx = -1;
static int g_lastCfg = -1;

static void SayTitle(int i, oga::Priority pri)
{
    const char* s = kTitleEntries[i];
    if (g_host->announce_q)
        oga::announce(g_host->announce_q, oga::Announcement{s, pri, "menu", nullptr, 0, 0, -1}, g_host->now_ms);
    else if (g_host->speak)
        g_host->speak(g_host->ctx, s, pri == oga::Priority::High);
}

static void OnFrame(void)
{
    if (BoardFrame()) return;         // gateway board mode owns its frames
    if (FieldPlayer()) return;        // on the field no menu watch applies
    int t = TitleIndex();
    if (t != g_lastTitle) {
        g_lastTitle = t;
        if (t >= 0) SayTitle(t, oga::Priority::Normal);
    }
    int si = -1;
    const SetupScreen* sc = SetupNow(&si);
    int sid = sc ? (int) sc->id : -1;
    if (sid != g_lastSetupId || si != g_lastSetupIdx) {
        g_lastSetupId = sid; g_lastSetupIdx = si;
        if (sc) SayLine(sc->rows[si], oga::Priority::Normal);
    }
    int m = sc ? -1 : MainIndex();
    if (m != g_lastMain) {
        g_lastMain = m;
        if (m >= 0 && MainShowing(m)) SayLine(kMainEntries[m], oga::Priority::Normal);
    }
    int c = (sc || m >= 0) ? -1 : ConfigIndex();
    if (c != g_lastCfg) {
        g_lastCfg = c;
        if (c >= 0) SayLine(kCfgEntries[c], oga::Priority::Normal);
    }
}

static void Command(oga::Command cmd)
{
    if (cmd == oga::Command::WhereAmI || cmd == oga::Command::MenuState) {
        int bg = -1;
        const BoardDef* bdef = nullptr;
        int bst = BoardCtrlState();
        if ((bst == 20 || bst == 21) && BoardCells(&bg, &bdef)) {
            char bl[384];
            BoardArrival(bdef, bg, bl, sizeof bl);
            if (bl[0]) { SayBoard(bl, oga::Priority::High); return; }
        }
        int t = TitleIndex();
        if (t >= 0) { SayTitle(t, oga::Priority::High); return; }
        int si = -1;
        const SetupScreen* sc = SetupNow(&si);
        if (sc) { SayLine(sc->rows[si], oga::Priority::High); return; }
        if (FieldPlayer()) { SayField(true); return; }
        int m = MainIndex();
        if (m >= 0) { if (MainShowing(m)) SayLine(kMainEntries[m], oga::Priority::High); return; }
        int c = ConfigIndex();
        if (c >= 0) { SayLine(kCfgEntries[c], oga::Priority::High); return; }
        SayLine("Not on the field.", oga::Priority::High);
    }
}
static bool Ready(void) { return g_host && BuildOk(); }
bool FieldLiveForTest(void) { return FieldPlayer() != 0; }
int TitleIndexForTest(void) { return TitleIndex(); }
static void Detach(void) { g_host = nullptr; g_lastTitle = -1; g_lastCfg = -1; g_lastSetupId = -1; g_lastSetupIdx = -1; g_lastMain = -1; BoardReset(); g_boardWasLive = false; }

} // namespace dissidia012

extern const Adapter kDissidia012;  // extern first: namespace-scope const is otherwise internal
const Adapter kDissidia012 = {
    "dissidia012",
    "Dissidia 012 Final Fantasy",
    "ULUS10566",
    dissidia012::Attach,
    dissidia012::OnFrame,
    dissidia012::Command,
    dissidia012::Ready,
    dissidia012::Detach,
    nullptr,
    "Menus, field position, and gateway boards (discs, Engage prompts, Scan cursor, KP and Gil changes)",
    "Dissidia 012 Final Fantasy, USA (ULUS10566). The title, setup and main menus "
    "speak as you move; Where Am I reads the field position, or the current disc and "
    "its neighbours on a gateway board. On the board, disc changes, Engage prompts, "
    "the Scan cursor and KP/Gil changes are announced; hop sounds are the game's own "
    "and stay unspoken. Boards the reader cannot identify are announced generically "
    "by disc position only.",
};

} // namespace oga
