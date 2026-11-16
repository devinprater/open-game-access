/*
 * dissidia012_adapter.cpp — Dissidia 012 Final Fantasy (PSP, ULUS10566) adapter, first cut.
 *
 * STATUS: SKELETON. It attaches only on a fingerprinted build and speaks nothing yet.
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

static void SayLine(const char* s, oga::Priority pri)
{
    if (g_host->announce_q)
        oga::announce(g_host->announce_q, oga::Announcement{s, pri, "menu", nullptr, 0, 0, -1}, g_host->now_ms);
    else if (g_host->speak)
        g_host->speak(g_host->ctx, s, pri == oga::Priority::High);
}

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
static void Detach(void) { g_host = nullptr; g_lastTitle = -1; g_lastCfg = -1; g_lastSetupId = -1; g_lastSetupIdx = -1; g_lastMain = -1; }

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
    "Title menu speech (New Game, Load Game, Extras); everything else not wired yet",
    "Dissidia 012 Final Fantasy, USA (ULUS10566). Early build: the title menu "
    "speaks as you move. Everything past the title is not wired yet.",
};

} // namespace oga
