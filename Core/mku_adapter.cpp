/*
 * mku_adapter.cpp — Mortal Kombat: Unchained (PSP, ULUS10102), as an Open Game
 * Access adapter.
 *
 * WHY THIS EXISTS: Unchained is the PSP port of Mortal Kombat: Deception —
 * Arcade/Versus/Practice/Endurance ladders, Konquest (a story mode played
 * through menus and dialog), Chess Kombat and Puzzle Kombat. Almost all of it
 * is text-driven, which makes it a strong candidate for the Dissidia
 * treatment. This file is the SCAFFOLD that treatment starts from.
 *
 * STATE OF KNOWLEDGE (2026-10-16): ONE structure is verified — the main
 * menu (items, cursor, screen flag; see the main-menu section below for
 * the addresses, the gates, and how they were confirmed against the
 * screen, twice, on independent boots). Everything else — the Kombat
 * submenu, character select, fight state, Konquest text — is still
 * unmapped, and this project does not ship inferred addresses (see the
 * DBZ adapter's one-stride warning for what a plausible-but-wrong base
 * costs). What exists here:
 *
 *   * the registry identity (the core reads the disc ID from PARAM.SFO and
 *     exact-matches it — see adapters.cpp on why PSP matching is full-string),
 *   * honest speech for every command (verified menu speech on the main
 *     menu, a plain "not mapped" line everywhere else),
 *   * DumpState: an anchor-string scan of PSP user RAM, the first RE tool
 *     for this game. The main menu's own strings ("KOMBAT", "KONQUEST",
 *     "CHESS", ...) have to live somewhere in RAM while the menu is up;
 *     their addresses are the thread the menu mapping gets pulled from.
 *
 * The method and the target list live in
 * docs/reverse-engineering/mortal-kombat-unchained.md.
 *
 * ⛔ READ-ONLY. Like every other adapter here, this only inspects memory and
 * drives the real console's buttons when the player asks. It never writes RAM.
 */
#include "adapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

const Host* g_host = nullptr;

// ---- PSP memory window ------------------------------------------------------
// PPSSPP maps 32 MB of main RAM at 0x08000000; the user partition an adapter
// may see starts at 0x08800000. The Host contract bounds-checks reads —
// anything outside Main RAM returns 0 rather than faulting — so scanning the
// whole window is safe; it is a debug command, not a per-frame path.
constexpr uint32_t kUserRamBase = 0x08800000;
constexpr uint32_t kUserRamEnd  = 0x0A000000;

// The strings the first mapping pass hunts. Menu titles are the game's own
// words, in the game's own spelling: KOMBAT (the title and every mode name
// carries it), KONQUEST, CHESS, PUZZLE, ARCADE, VERSUS, PRACTICE, ENDURANCE,
// OPTIONS. A hit is a CANDIDATE text buffer, not a proven menu — candidates
// get confirmed against the screen before anything speaks from them.
const char* const kAnchors[] = {
    "KOMBAT", "KONQUEST", "CHESS", "PUZZLE", "ARCADE",
    "VERSUS", "PRACTICE", "ENDURANCE", "OPTIONS",
};
constexpr int kAnchorCount = (int) (sizeof(kAnchors) / sizeof(kAnchors[0]));
constexpr int kMaxLoggedPerAnchor = 8;   // log the first few; count them all

uint8_t u8(uint32_t a) { return g_host ? g_host->read8(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

// Speech routed through the announcement queue with a per-site group and
// priority — the same Say() the other migrated adapters use. Null-queue
// hosts fall back to the direct wire, which keeps the host tests'
// synchronous stubs working unchanged.
void Say(const char* s, const char* group, oga::Priority pri)
{
    // High = the player asked, so it always speaks (announcement-queue.md:
    // "High items bypass dedup"). It is still noted, so an automatic
    // repeat of the same line right after stays quiet.
    if (!oga::AdapterNoteSpoken(s) && pri != oga::Priority::High) return;
    if (g_host && g_host->announce_q)
        oga::announce(g_host->announce_q,
                      oga::Announcement{s, pri, group, nullptr, 0, 0, -1},
                      g_host->now_ms);
    else if (g_host && g_host->speak)
        g_host->speak(g_host->ctx, s, pri == oga::Priority::High);
}
void Log(const char* s)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, s);
}

// ---- the anchor scan (DumpState) ---------------------------------------------

bool MatchAscii(uint32_t addr, const char* anchor)
{
    for (int i = 0; anchor[i]; i++)
        if (u8(addr + (uint32_t) i) != (uint8_t) anchor[i]) return false;
    return true;
}

bool MatchUtf16Le(uint32_t addr, const char* anchor)
{
    for (int i = 0; anchor[i]; i++) {
        if (u8(addr + (uint32_t) i * 2) != (uint8_t) anchor[i]) return false;
        if (u8(addr + (uint32_t) i * 2 + 1) != 0) return false;
    }
    return true;
}

// Forward declarations: the Konquest state readers live further down
// (with the sections that prove them); DumpState reports them.
bool InKonquest(void);
bool KonquestPos(float* x, float* y, float* z);
bool RoamFacing(float* dx, float* dz);

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[mku] Mortal Kombat: Unchained (ULUS10102) anchor scan, user RAM 0x%08X-0x%08X",
             kUserRamBase, kUserRamEnd);
    Log(line);

    if (InKonquest()) {
        float x, y, z, dx, dz;
        if (KonquestPos(&x, &y, &z)) {
            if (RoamFacing(&dx, &dz))
                snprintf(line, sizeof(line),
                         "[mku] konquest pos=(%.2f, %.2f, %.2f) facing=(%.2f, %.2f) (movement-derived)",
                         (double) x, (double) y, (double) z,
                         (double) dx, (double) dz);
            else
                snprintf(line, sizeof(line),
                         "[mku] konquest pos=(%.2f, %.2f, %.2f) facing=none tracked",
                         (double) x, (double) y, (double) z);
            Log(line);
        }
    }

    // One pass over the window; at each byte, try every anchor in both
    // encodings. First-byte dispatch keeps the common case to one compare.
    int total = 0;
    int counts[kAnchorCount][2];
    int logged[kAnchorCount][2];
    for (int a = 0; a < kAnchorCount; a++)
        counts[a][0] = counts[a][1] = logged[a][0] = logged[a][1] = 0;

    for (uint32_t addr = kUserRamBase; addr < kUserRamEnd; addr++) {
        uint8_t c = u8(addr);
        if (c < 0x41 || c > 0x5A) continue;      // anchors start A-Z
        for (int a = 0; a < kAnchorCount; a++) {
            if (c != (uint8_t) kAnchors[a][0]) continue;
            if (MatchAscii(addr, kAnchors[a])) {
                counts[a][0]++; total++;
                if (logged[a][0] < kMaxLoggedPerAnchor) {
                    logged[a][0]++;
                    snprintf(line, sizeof(line), "[mku] \"%s\" ascii @ 0x%08X",
                             kAnchors[a], addr);
                    Log(line);
                }
            }
            if (u8(addr + 1) == 0 && MatchUtf16Le(addr, kAnchors[a])) {
                counts[a][1]++; total++;
                if (logged[a][1] < kMaxLoggedPerAnchor) {
                    logged[a][1]++;
                    snprintf(line, sizeof(line), "[mku] \"%s\" utf16 @ 0x%08X",
                             kAnchors[a], addr);
                    Log(line);
                }
            }
        }
    }

    for (int a = 0; a < kAnchorCount; a++) {
        if (!counts[a][0] && !counts[a][1]) continue;
        snprintf(line, sizeof(line), "[mku] \"%s\": %d ascii, %d utf16",
                 kAnchors[a], counts[a][0], counts[a][1]);
        Log(line);
    }
    snprintf(line, sizeof(line), "[mku] scan done: %d hits", total);
    Log(line);
}

// ---- the main menu (VERIFIED 2026-10-16, ULUS10102 v1.03, PPSSPP) ----------
//
// The game's front door, in the game's own order and spelling — read off
// screenshots, not inferred from the string pool:
//   KOMBAT, CHESS KOMBAT, PUZZLE KOMBAT, KONQUEST, MK WIRELESS,
//   THE KRYPT, KONTENT, PROFILES, OPTIONS. The cursor wraps at both ends.
//
// The gate — three checks, all measured (full method and the retraction
// history in docs/reverse-engineering/mortal-kombat-unchained.md,
// "Cursor hunt 2" and "Gate repair"):
//   * kMainMenuCursorAddr — the 1-based selection (1..9), the main
//     menu's own variable. It PERSISTS on other screens (it is where
//     the menu will resume), so alone it proves nothing about which
//     screen is up. It sits in the heap, NOT the static image; it read
//     identically across independent boots (PPSSPP's allocation is
//     deterministic for this front-end path), but a heap address is a
//     measured fact about this build on this core, not a promise — the
//     decomp route to a static anchor (the code that writes it) is
//     still owed.
//   * kMainMenuHolderAddr — the menu's current-item pointer. It must
//     hold exactly the pool object the cursor names (objects are
//     kMenuObjStride apart, each carrying kObjMagic at +4). In a fight
//     the cell is garbage and the pool is gone; on character select
//     likewise. This is the "the menu's objects are live and current"
//     check.
//   * NOT the Kombat submenu: a child screen leaves the main menu's
//     state untouched underneath it (holder still on the entered item,
//     cursor still on it), so presence is not enough — the submenu's
//     own current-item pointer (kKombatSubHolderAddr) is null on every
//     other screen sampled and holds one of its three objects only
//     while the submenu is on top. Top screen wins.
// ⛔ An earlier version of this gate used a "screen flag" word at
// 0x08BA94A8 that sampled 1 on the menu and 0 in a fight — on three
// dumps. With all 20 menu dumps counted it flickers (0 in 8 of them,
// and 1 once mid-fight): it is a phase/strobe word, not a flag. Small
// samples certify nothing; the checks above are the ones that survived
// every dump on file.
// ---- Konquest mode flag (VERIFIED 2026-10-17, hunts 14-17) -------------------
//
// Two BSS words read 1 in every Konquest capture (cinematic, dialogue,
// tutorial bouts) and 0 on the menus and in arcade fights, across two
// independent boots. The flag gates the whole adapter: Konquest bouts
// run on the arcade fight engine but the shared struct's fighter-id
// fields do NOT hold character-select indices there (2/0 through the
// Shujinko tutorial), and the front-end's pool state goes stale in
// Konquest — the Kombat submenu's holder keeps the Arcade object from
// an earlier visit (a cold boot that skipped the submenu reads 0
// there), and during the opening cinematic the main menu's holder
// still names the Konquest item. So every front-end and fight gate
// below excludes Konquest on this flag, and Konquest gets its own
// surfaces as they are verified.
constexpr uint32_t kKonquestFlagAAddr = 0x08AF69C8;
constexpr uint32_t kKonquestFlagBAddr = 0x08B123DC;   // bout sub-state, see below

// Flag A is the mode itself. Flag B is NOT: it reads 1 inside a Konquest bout
// (the dojo drills) and 0 in free-roam, which was measured in the village
// (2026-10-19) while A stayed 1 and the compass HUD was live. Requiring both
// therefore declared free-roam "not Konquest": the dialogue and objective
// channels went silent exactly where the objectives matter, and the front-end
// gates lost their Konquest exclusion in the same state. Gate on A alone and
// treat B as what it is — whether a bout is running.
bool InKonquest(void)
{
    return u32(kKonquestFlagAAddr) == 1;
}

constexpr uint32_t kMainMenuCursorAddr = 0x09E61750;
constexpr uint32_t kMainMenuHolderAddr = 0x0911F868;
constexpr uint32_t kMainMenuPoolTop    = 0x0911F700;
constexpr uint32_t kMenuObjStride      = 0xCE0;
constexpr uint32_t kObjMagic           = 0x4F424A20;   // " OBJ"
constexpr uint32_t kKombatSubHolderAddr = 0x09118348;
const char* const kMainMenuItems[] = {
    "Kombat", "Chess Kombat", "Puzzle Kombat", "Konquest", "MK Wireless",
    "The Krypt", "Kontent", "Profiles", "Options",
};
constexpr int kMainMenuCount = 9;

// The front end is sitting on one of the nine main-menu rows: the index is in
// range and the current-item pointer names exactly the pool object that index
// implies, magic intact. True on the main menu AND on any child screen opened
// from it, because the parent's state stays live underneath — so this is a
// "the front end owns the screen" test, not "the main menu is visible".
bool FrontEndRow(void)
{
    uint32_t c = u32(kMainMenuCursorAddr);
    if (c < 1 || c > (uint32_t) kMainMenuCount) return false;
    uint32_t obj = kMainMenuPoolTop - (c - 1) * kMenuObjStride;
    if (u32(kMainMenuHolderAddr) != obj) return false;
    return u32(obj + 4) == kObjMagic;
}

bool OnMainMenu(void)
{
    if (InKonquest()) return false;   // stale pool state (see flag note)
    if (!FrontEndRow()) return false;
    // A child screen opened from the main menu means the menu itself is not
    // what the player is looking at; the child pointer is the tell.
    uint32_t sub = u32(kKombatSubHolderAddr);
    if (sub == 0x091181E0 || sub == 0x09117500 || sub == 0x09116820) return false;
    return true;
}

// ---- the Kombat submenu (VERIFIED 2026-10-16 night) -------------------------
//
// KOMBAT's child list: ARCADE, ENDURANCE, PRACTICE (wraps). There is no
// Versus here — on PSP, versus play lives under MK WIRELESS. This menu
// keeps NO numeric index (the main menu's index word does not move while
// it is up); selection is the front-end's current-item pointer at
// kKombatSubHolderAddr stepping DOWN through its three pool objects
// (same kMenuObjStride family as the main menu). The holder cell is the
// screen's presence tell: it reads 0 on the main menu, in a fight, and
// on character select, and holds one of its objects only while this
// list is on top — which is also what gate v3's third clause checks.
// (An earlier bench note claimed no button exits this list; that was
// a wrong-button artifact — TRIANGLE is the front-end's back button,
// verified 2026-10-17. The gate reads state, not inputs, either way.)
const char* const kKombatSubItems[] = { "Arcade", "Endurance", "Practice" };
constexpr uint32_t kKombatSubObjs[] = { 0x091181E0, 0x09117500, 0x09116820 };
constexpr int kKombatSubCount = 3;

// 0-based submenu selection, or -1 when the submenu is not on top.
int KombatSubIndex(void)
{
    uint32_t sub = u32(kKombatSubHolderAddr);
    for (int i = 0; i < kKombatSubCount; i++) {
        if (sub == kKombatSubObjs[i])
            return u32(sub + 4) == kObjMagic ? i : -1;
    }
    return -1;
}

bool OnKombatSub(void)
{
    if (InKonquest()) return false;   // the holder goes stale there
    // The holder also survives into the attract demo and into a fight: the
    // pointer keeps naming its object and the OBJ magic still checks out, so
    // the submenu alone would report "Arcade, 1 of 3" over a demo fight
    // (measured). The main-menu index reads 1..9 only while the front end owns
    // the screen, and reads 0 in the attract demo, so use it for liveness.
    // ⛔ The child-screen cell is REUSED by other submenus. Measured live
    // 2026-10-18: on the Options screen the cell holds 0x091181E0 — the same
    // live object this list uses — with the cursor on item 9, and the old
    // gate spoke "Kombat menu. Arcade, 1 of 3" over the Options tabs. Only the
    // KOMBAT item opens this list, and the parent index reads 1 while it is
    // up, so that index is what separates the two screens.
    if (u32(kMainMenuCursorAddr) != 1) return false;
    return KombatSubIndex() >= 0;
}

void KombatSubRow(char* out, size_t cap)
{
    int i = KombatSubIndex();
    snprintf(out, cap, "%s, %d of %d", kKombatSubItems[i], i + 1, kKombatSubCount);
}

// "<item>, <n> of 9" for the current selection; empty when off-menu.
void MainMenuRow(char* out, size_t cap)
{
    uint32_t c = u32(kMainMenuCursorAddr);
    snprintf(out, cap, "%s, %u of %d", kMainMenuItems[c - 1], c, kMainMenuCount);
}

// ---- the Options screen (VERIFIED 2026-10-18, live) -------------------------
//
// The Options list is three category tabs — GAMEPLAY, AUDIO, CONTROLS — and
// each tab opens its own settings list. Measured live, ULUS10102 v1.03:
//   * the tab selection is the SAME child-screen cell the Kombat submenu
//     uses (kKombatSubHolderAddr) with the SAME three objects, reused:
//     0x091181E0 (Gameplay), 0x09117500 (Audio), 0x09116820 (Controls), each
//     carrying kObjMagic at +4. The PARENT index reads 9, which is what
//     separates Options from the Kombat list (see the gate note there).
//   * inside a tab, the row selection is a current-item pointer at
//     kOptionsRowHolderAddr stepping DOWN through that tab's own object run,
//     every object carrying kObjMagic at +4:
//       GAMEPLAY: top 0x09106040, stride 0x1160, 8 rows, wraps.
//       AUDIO:    top 0x0910B400, stride 0x10B0, 5 rows, wraps.
//     Both tops are heap addresses measured on this build, like the main-menu
//     pool — the gates below fail closed if a later boot puts the run
//     elsewhere, rather than speaking the wrong row.
//   * CONTROLS opens a button-mapping screen (a PSP diagram with ATTACK 1 /
//     ATTACK 2 style prompts) whose selection is a different widget: the row
//     cell read 0 there, so that tab stays unmapped and says so.
// What is NOT mapped: the VALUE of any row (volumes, difficulty words, round
// counts). Only the row names and the tab names are spoken.
constexpr uint32_t kOptionsParentIndex = 9;
constexpr uint32_t kOptionsRowHolderAddr = 0x09119CE8;
constexpr uint32_t kOptionsTabObjs[3] = { 0x091181E0, 0x09117500, 0x09116820 };
const char* const kOptionsTabNames[3] = { "Gameplay", "Audio", "Controls" };

struct RowList {
    uint32_t top;
    uint32_t stride;
    int count;
    const char* const* names;
};

const char* const kGameplayRowNames[8] = {
    "Kombat CPU", "Chess CPU", "Puzzle CPU", "Rounds to Win",
    "Puzzle Rounds", "Round Time", "Death Traps", "Violence",
};
const char* const kAudioRowNames[5] = {
    "Game Music", "Environment", "Announcer", "Effects", "Speech",
};
const RowList kRowLists[2] = {
    { 0x09106040, 0x1160, 8, kGameplayRowNames },
    { 0x0910B400, 0x10B0, 5, kAudioRowNames },
};
const char* const kRowListTabs[2] = { "Gameplay", "Audio" };

// 0..2 while the tab list is up, else -1.
int OptionsTabIndex(void)
{
    if (InKonquest()) return -1;
    if (u32(kMainMenuCursorAddr) != kOptionsParentIndex) return -1;
    uint32_t v = u32(kKombatSubHolderAddr);
    for (int i = 0; i < 3; i++)
        if (v == kOptionsTabObjs[i]) return u32(v + 4) == kObjMagic ? i : -1;
    return -1;
}

bool OnOptionsTabs(void) { return OptionsTabIndex() >= 0; }

// Which measured row list the holder names, or nullptr. Fills *idx with the
// 0-based row.
const RowList* OptionsRowList(int* idx)
{
    uint32_t v = u32(kOptionsRowHolderAddr);
    if (InKonquest() || v == 0) return nullptr;
    for (int i = 0; i < 2; i++) {
        const RowList& r = kRowLists[i];
        if (v > r.top) continue;
        uint32_t d = r.top - v;
        if (d % r.stride != 0) continue;
        uint32_t row = d / r.stride;
        if ((int) row >= r.count) continue;
        if (u32(v + 4) != kObjMagic) continue;
        *idx = (int) row;
        return &r;
    }
    return nullptr;
}

bool OnOptionsRows(void)
{
    int idx;
    return OptionsRowList(&idx) != nullptr;
}

void OptionsRow(char* out, size_t cap, bool with_section)
{
    int idx = 0;
    const RowList* r = OptionsRowList(&idx);
    if (!r) { snprintf(out, cap, ""); return; }
    int list = (r == &kRowLists[0]) ? 0 : 1;
    if (with_section)
        snprintf(out, cap, "Options, %s. %s, %d of %d", kRowListTabs[list],
                 r->names[idx], idx + 1, r->count);
    else
        snprintf(out, cap, "%s, %d of %d", r->names[idx], idx + 1, r->count);
}

// "Options. Audio, 2 of 3." for a tab; empty when the tabs are not up.
void OptionsTabRow(char* out, size_t cap)
{
    int i = OptionsTabIndex();
    if (i < 0) { snprintf(out, cap, ""); return; }
    snprintf(out, cap, "Options. %s, %d of 3", kOptionsTabNames[i], i + 1);
}

// ---- character select (VERIFIED 2026-10-16 night, ULUS10102 v1.03) ----------
//
// The Arcade fighter grid: 10 columns x 3 rows, row-major, the cursor
// CLAMPS at all four edges (no wrap, no row spill). Every name below was
// read off the screen's own name panel, cell by cell (hunts 8–10 in the
// RE notes) — the portraits were NOT trusted for identification, and
// indeed the first cell is Kenshi, not the Jax his portrait suggests.
//   Row 1: Kenshi, Jade, Scorpion, Mileena, Goro, Kitana, Baraka,
//          Sub-Zero, Havik, Sindel
//   Row 2: Raiden, Li Mei, Kabal, Ermac, Blaze, Jax, Nightwolf,
//          Bo' Rai Cho, Noob-Smoke, Tanya
//   Row 3: Shujinko, Hotaru, Ashrah, Dairou, Shao Kahn, Frost, Kobra,
//          Darrius, Kira, Liu Kang
// The cursor starts on Scorpion (index 2).
//
// State, same gate family as the menus: the index lives in the STATIC
// image at kCharCursorAddr (0-based linear, row-major) and persists as
// "last hovered fighter" on other screens, so it is gated by the
// current-object pointer at kCharHolderAddr holding exactly the
// character object the index names — records are kCharObjStride apart
// counting DOWN from kCharObjBase (the table is stored in reverse),
// each carrying kObjMagic at +4. The gate formula matched all 33 dumps
// of the mapping walk and a cold-boot session with zero mismatches.
// (Two traps from that walk, for the next screen: several words in the
// same statics are MOVE COUNTERS that impersonate an index perfectly
// while a walk is monotonic — only the direction changes expose them.)
constexpr uint32_t kCharCursorAddr = 0x08C1B3B0;
constexpr uint32_t kCharHolderAddr = 0x09F9C1A8;
constexpr uint32_t kCharObjBase    = 0x09112080;
constexpr uint32_t kCharObjStride  = 0x490;
constexpr int kCharCount = 30;
constexpr int kCharCols  = 10;
const char* const kCharNames[] = {
    "Kenshi", "Jade", "Scorpion", "Mileena", "Goro", "Kitana", "Baraka",
    "Sub-Zero", "Havik", "Sindel",
    "Raiden", "Li Mei", "Kabal", "Ermac", "Blaze", "Jax", "Nightwolf",
    "Bo' Rai Cho", "Noob-Smoke", "Tanya",
    "Shujinko", "Hotaru", "Ashrah", "Dairou", "Shao Kahn", "Frost",
    "Kobra", "Darrius", "Kira", "Liu Kang",
};

// Is this address one of the engine's character objects? Every entry in the
// fighter table carries the ` OBJ` magic at +4.
static bool CharObjectAt(uint32_t addr)
{
    return addr >= kUserRamBase && addr + 8 <= kUserRamEnd &&
           (addr & 3) == 0 && u32(addr + 4) == kObjMagic;
}

bool OnCharSelect(void)
{
    if (InKonquest()) return false;   // front-end state is stale there
    uint32_t idx = u32(kCharCursorAddr);
    if (idx >= (uint32_t) kCharCount) return false;

    // Arcade's table is the STATIC one, and this is the gate all 33 mapping
    // dumps certified.
    uint32_t obj = kCharObjBase - idx * kCharObjStride;
    if (u32(kCharHolderAddr) == obj)
        return u32(obj + 4) == kObjMagic;

    // ⛔ PRACTICE OPENS A SECOND FIGHTER TABLE IN THE HEAP. Measured on the
    // live host 2026-10-21: cursor 2 (Scorpion, the screen's own highlight) and
    // a holder of 0x090E05A0 — three RIGHTs later, cursor 5 and holder
    // 0x090DF7F0, exactly three 0x490 strides lower. That is the same cursor
    // formula around a different base, about 0x30000 below the static table.
    // The base is derived from the holder, not pinned: the heap address can
    // move between boot paths. It still has to satisfy BOTH ends of the run
    // (the held object and the index-0 object), each with the engine magic.
    uint32_t h = u32(kCharHolderAddr);
    if (h < 0x09000000u || h >= 0x09120000u) return false;
    if (!CharObjectAt(h)) return false;
    return CharObjectAt(h + idx * kCharObjStride);
}

// "<name>, row R, column C" for the hovered fighter; on-grid only.
void CharRow(char* out, size_t cap)
{
    uint32_t idx = u32(kCharCursorAddr);
    snprintf(out, cap, "%s, row %u, column %u", kCharNames[idx],
             idx / kCharCols + 1, idx % kCharCols + 1);
}

void SayCharRow(void)
{
    char row[96];
    CharRow(row, sizeof(row));
    Say(row, "menu", oga::Priority::High);
}

void SayMainMenuRow(void)
{
    char row[96];
    MainMenuRow(row, sizeof(row));
    Say(row, "menu", oga::Priority::High);
}

// ---- Konquest dialogue (VERIFIED 2026-10-17, hunts 15-17) -------------------
//
// The current dialogue line is rewritten into a heap buffer at a
// FIXED address: 0x09178568 in two independent boots (a savestate
// lineage and a cold boot through profile activation — the path into
// Konquest allocates deterministically). The text-box struct's twelve
// words before the string carry a fixed signature (geometry constants
// 0x30/0xE9/0x24/0x180, a -1 word, the static text-descriptor pointer
// 0x08AFD1C0); an offline scan for that signature finds exactly one
// match per RAM dump, at this address, across four lines and both
// boots. Speech trusts the buffer only while the signature verifies,
// so a future allocation shift fails closed instead of speaking
// garbage; the signature scan is the documented relocation route.
// The box is UP only while a scene-state pointer at buffer+0x7C8 is
// non-null AND the struct's color word reads the up tint
// (0xC8C8C8FF; it reads 0xFF4848FF when the box is down) — when the
// box closes the buffer keeps the last line as stale text, so both
// signals are required. Lines are spoken once they have been stable
// for kDlgStableFrames frames (the box may fill progressively), and
// the last-spoken record clears when the box closes, so a line shown
// again speaks again.
constexpr uint32_t kDlgBufferAddr = 0x09178568;
constexpr uint32_t kDlgColorAddr = 0x09178554;   // buffer - 0x14
constexpr uint32_t kDlgColorUp = 0xC8C8C8FF;
// The red instruction line (2026-10-19, measured in the dojo tutorial): the
// SAME buffer and the same signed struct carry the current objective, but the
// colour word reads 0xFF4848FF and no text box is drawn — the line is red text
// on the HUD. The old check treated that as "box down" and stayed silent, so a
// blind player never heard "step toward me three times". Objectives speak as
// their own channel and do not collide with the dialogue line.
constexpr uint32_t kDlgColorObjective = 0xFF4848FF;
constexpr uint32_t kDlgPtrAddr = 0x09178D30;     // buffer + 0x7C8
constexpr int kDlgStableFrames = 30;

char g_dlgLast[224] = "";
char g_dlgPending[224] = "";
int g_dlgStable = 0;

bool DlgStructOk(void)
{
    const uint32_t base = kDlgBufferAddr - 48;
    static const struct { int idx; uint32_t val; } kSig[] = {
        {0, 0x30}, {1, 0xE9}, {2, 0x24}, {3, 0x180}, {6, 0xFFFFFFFF},
        {8, 0x6}, {9, 0x0}, {10, 0x08AFD1C0}, {11, 0x0},
    };
    for (size_t i = 0; i < sizeof(kSig) / sizeof(kSig[0]); i++)
        if (u32(base + (uint32_t) kSig[i].idx * 4) != kSig[i].val) return false;
    return true;
}

// Copy the buffer's current line into out; false unless it is
// non-empty, NUL-terminated within the cap, and printable ASCII.
bool DlgReadLine(char* out, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        uint8_t c = u8(kDlgBufferAddr + (uint32_t) n);
        if (c == 0) break;
        if (c < 32 || c > 126) return false;
        out[n++] = (char) c;
    }
    if (n == 0 || u8(kDlgBufferAddr + (uint32_t) n) != 0) return false;
    out[n] = 0;
    return true;
}

bool DialogueUp(void)
{
    return InKonquest() && DlgStructOk() && u32(kDlgPtrAddr) != 0 &&
           u32(kDlgColorAddr) == kDlgColorUp;
}

bool ObjectiveUp(void)
{
    return InKonquest() && DlgStructOk() && u32(kDlgPtrAddr) != 0 &&
           u32(kDlgColorAddr) == kDlgColorObjective;
}

// Per-frame dialogue watch: speak a line once it settles, and only
// once per showing.
void DlgOnFrame(void)
{
    char line[224];
    bool objective = ObjectiveUp();
    if ((DialogueUp() || objective) && DlgReadLine(line, sizeof(line))) {
        if (strcmp(line, g_dlgPending) != 0) {
            strncpy(g_dlgPending, line, sizeof(g_dlgPending) - 1);
            g_dlgPending[sizeof(g_dlgPending) - 1] = 0;
            g_dlgStable = 0;
        } else if (++g_dlgStable >= kDlgStableFrames) {
            if (strcmp(line, g_dlgLast) != 0) {
                char spoken[256];
                if (objective)
                    snprintf(spoken, sizeof(spoken), "Objective. %s", line);
                else
                    snprintf(spoken, sizeof(spoken), "%s", line);
                Say(spoken, objective ? "objective" : "dialogue",
                    oga::Priority::Normal);
                strncpy(g_dlgLast, line, sizeof(g_dlgLast) - 1);
                g_dlgLast[sizeof(g_dlgLast) - 1] = 0;
            }
        }
    } else {
        g_dlgPending[0] = 0;
        g_dlgStable = 0;
        g_dlgLast[0] = 0;   // box closed: a re-shown line speaks again
    }
}

// ---- fight state (VERIFIED 2026-10-16 night, hunt11) ------------------------
//
// One BSS fight struct holds the whole match. The two fighter blocks sit
// 0x6C apart with the same layout (id, HP float, wins at +0x34 across the
// stride); the round counter and the seconds timer follow. Every word
// below was verified against paused screenshots across eight captures:
// full health, two attack bursts, an idle stretch, a KO ("SCORPION
// WINS!", timer frozen at 45), and round 2's reset.
//   P1 HP [1, 1, 1, 0.904, 0.904, 0.904, 1, 1]  (untouched until the CPU
//   landed hits during the idle stretch — matches the bar)
//   P2 HP [1, 0.741, 0.333, 0.333, 0.333, 0 at the KO, 1, 1]
//   timer [58, 54, 51, 48, 48, 45, 60, 60] — the displayed seconds at
//   every capture, reset included. (A twin lives in the static data
//   segment at 0x08C1613C; the BSS word sits beside the rest of the
//   struct, so it is the one the adapter reads.)
// The on-screen bar drains with a lag behind the true HP word (the
// classic ghost drain), which is why early pixel-fraction estimates ran
// a few points high — the RAM words are the ground truth speech uses.
// Fighter ids are the character-select indices (2 = Scorpion, 4 = Goro
// in the verification fight), so names come from kCharNames.
constexpr uint32_t kP1FighterIdAddr = 0x08D929B4;
constexpr uint32_t kP1HpAddr = 0x08D929B8;        // float, 0..1
constexpr uint32_t kP1WinsAddr = 0x08D929EC;
constexpr uint32_t kP2HpAddr = 0x08D92A24;        // float, 0..1
constexpr uint32_t kP2FighterIdAddr = 0x08D92A2C;
constexpr uint32_t kP2WinsAddr = 0x08D92A58;
constexpr uint32_t kRoundAddr = 0x08D92AE0;
constexpr uint32_t kTimerAddr = 0x08D92AF0;       // seconds remaining

float f32(uint32_t addr)
{
    uint32_t bits = u32(addr);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

bool InFight(void)
{
    if (InKonquest()) return false;   // Konquest bouts: same engine,
                                      // different id semantics (above)
    // ⛔ THE FIGHT STRUCT LINGERS ON CHARACTER SELECT. Measured live: the grid
    // was up with Shao Kahn highlighted, and the last match's round/timer/HP
    // still passed every check below, so WhereAmI read that dead match's fighter
    // ids (Goro and Scorpion) over the grid. The fighter table is a positive
    // signal for "not in a match" and must win over the lingering words.
    if (OnCharSelect()) return false;
    // The fight struct's words LINGER after a match and on front-end screens:
    // measured live 2026-10-18 on the Options tabs, round read 1, the timer 72
    // and both HP words 1.0 while the menu was up, which passed every check
    // below and made the adapter speak a fight over a settings screen. A live
    // front-end row is the positive signal that overrides them.
    if (FrontEndRow()) return false;
    uint32_t round = u32(kRoundAddr);
    if (round < 1 || round > 3) return false;
    if (u32(kTimerAddr) > 99) return false;
    if (u32(kP1FighterIdAddr) >= (uint32_t) kCharCount ||
        u32(kP2FighterIdAddr) >= (uint32_t) kCharCount) return false;
    float h1 = f32(kP1HpAddr), h2 = f32(kP2HpAddr);
    if (!(h1 >= 0.0f && h1 <= 1.0f) || !(h2 >= 0.0f && h2 <= 1.0f)) return false;
    if (h1 == 0.0f && h2 == 0.0f) return false;  // nobody standing: not a live match
    return true;
}

// "Scorpion 90 percent, Goro 33 percent. 48 seconds left. You lead 1 to 0."
void FightLine(char* out, size_t cap)
{
    const char* n1 = kCharNames[u32(kP1FighterIdAddr)];
    const char* n2 = kCharNames[u32(kP2FighterIdAddr)];
    int p1 = (int) (f32(kP1HpAddr) * 100.0f + 0.5f);
    int p2 = (int) (f32(kP2HpAddr) * 100.0f + 0.5f);
    uint32_t w1 = u32(kP1WinsAddr), w2 = u32(kP2WinsAddr);
    char lead[64];
    if (w1 == w2)
        snprintf(lead, sizeof(lead), "Rounds even at %u.", w1);
    else if (w1 > w2)
        snprintf(lead, sizeof(lead), "You lead %u to %u.", w1, w2);
    else
        snprintf(lead, sizeof(lead), "You trail %u to %u.", w1, w2);
    snprintf(out, cap, "%s %d percent, %s %d percent. %u seconds left. %s",
             n1, p1, n2, p2, u32(kTimerAddr), lead);
}

// ---- Konquest bout speech (VERIFIED 2026-10-23, bout hunts) ------------------
//
// Konquest bouts — the dojo lessons, scripted encounters, free-roam fights —
// run on the versus fight engine over the SAME BSS fight struct, and the HP
// floats are live and true there (tutorial strikes lesson, 2026-10-17 and
// re-measured 2026-10-23: Square took the opponent 1.000 -> 0.912, exactly
// the documented hit). What the struct does NOT carry in Konquest is the
// versus metadata: the fighter-id fields hold Konquest ids (P1 is 2 =
// Shujinko in every Konquest capture; P2 is 0 = Bo' Rai Cho through the
// tutorial), and the round/timer/win words are per-phase machinery — the
// round word counted lessons (3, 4, 6 across the tutorial), the timer is a
// phase clock whose expiry zeroes P1's HP and retries the exercise, and
// the win words hold lesson tallies. So bout speech names the fighters
// the Konquest way and speaks ONLY the health line: no round, no timer,
// no lead. (The versus line keeps all three; its words mean what they say
// there.)
//
// The gate needs three signals, because each alone lies somewhere:
//   * InKonquest() (flag A) — the mode. Versus never sets it in a clean
//     session.
//   * Flag B (0x08B123DC) — the fight engine is live: 1 in versus fights
//     and in every Konquest arena scene, 0 in the menus, in versus pause,
//     and in Konquest free-roam (village measurement 2026-10-19,
//     re-measured 2026-10-23: B = 0 with the fight struct holding stale
//     words from the last lesson, opponent HP 0.000 included).
//   * Struct sanity — both HP floats inside [0, 1] and P1's id == 2.
// Even all three together hold during Konquest's opening cinematic and
// its dojo dialogue phases, where the struct sits frozen at full health.
// So the bout LATCHES: it opens only when the engine proves itself in
// motion — the phase timer moves or a health word changes while the raw
// gate holds — and closes when the raw gate drops. Cinematics and talk
// phases never trip it; a real bout trips it on its first timer tick or
// its first landed hit.
//
// Speech: on the latch, "Fight." plus the health line; a side
// reaching zero speaks its outcome once per exchange (the latch
// re-arms when that side's health rises again — the tutorial
// refills between phases). Mid-bout HEALTH is no longer re-spoken:
// the sound-first pass (below) replaced the throttled health line
// with per-hit beeps and milestone-only callouts. P1 reaching zero
// speaks only with time left on the phase clock: at zero the
// engine zeroes P1 itself when an exercise's time expires, which
// is a failed drill, not a knockout, and the game's own "try
// again" dialogue covers it. WhereAmI in a latched bout speaks the
// fight line ahead of the objective and dialogue lines
// (they still auto-speak as they appear); unlatched Konquest WhereAmI
// is unchanged.
constexpr uint32_t kKonquestPlayerId = 2;   // Shujinko, in Konquest ids

// Konquest opponent names by Konquest fighter id. Only the tutorial's
// id is proven; anything else speaks as "opponent" rather than guess.
const char* KonquestFoeName(uint32_t id)
{
    switch (id) {
        case 0: return "Bo' Rai Cho";
        default: return nullptr;
    }
}

bool BoutStructSane(void)
{
    if (u32(kP1FighterIdAddr) != kKonquestPlayerId) return false;
    float h1 = f32(kP1HpAddr), h2 = f32(kP2HpAddr);
    return h1 >= 0.0f && h1 <= 1.0f && h2 >= 0.0f && h2 <= 1.0f;
}

// Raw gate only — the latch in BoutOnFrame decides speech.
bool InBoutRaw(void)
{
    return InKonquest() && u32(kKonquestFlagBAddr) == 1 && BoutStructSane();
}

// "Shujinko 100 percent, Bo' Rai Cho 91 percent."
void BoutLine(char* out, size_t cap)
{
    const char* foe = KonquestFoeName(u32(kP2FighterIdAddr));
    int p1 = (int) (f32(kP1HpAddr) * 100.0f + 0.5f);
    int p2 = (int) (f32(kP2HpAddr) * 100.0f + 0.5f);
    if (foe)
        snprintf(out, cap, "Shujinko %d percent, %s %d percent.", p1, foe, p2);
    else
        snprintf(out, cap, "Shujinko %d percent, opponent %d percent.", p1, p2);
}

bool g_boutLive = false;          // the latch: engine motion proven
int g_boutFrame = 0;              // frames since the watch started
uint32_t g_boutTimer = 0xFFFFFFFF;
float g_boutHp1 = -1.0f, g_boutHp2 = -1.0f;   // last words under the raw gate
bool g_boutFoeDown = false, g_boutPlayerDown = false;

bool InKonquestBout(void) { return g_boutLive; }

void BoutOnFrame(void)
{
    g_boutFrame++;
    if (!InBoutRaw()) {
        g_boutLive = false;
        g_boutTimer = 0xFFFFFFFF;
        g_boutHp1 = g_boutHp2 = -1.0f;
        g_boutFoeDown = g_boutPlayerDown = false;
        return;
    }
    uint32_t t = u32(kTimerAddr);
    float h1 = f32(kP1HpAddr), h2 = f32(kP2HpAddr);
    if (!g_boutLive) {
        bool motion = g_boutTimer != 0xFFFFFFFF &&
                      (t != g_boutTimer || h1 != g_boutHp1 || h2 != g_boutHp2);
        g_boutTimer = t; g_boutHp1 = h1; g_boutHp2 = h2;
        if (!motion) return;
        g_boutLive = true;
        char line[192], bout[128];
        BoutLine(bout, sizeof(bout));
        snprintf(line, sizeof(line), "Fight. %s", bout);
        Say(line, "fight", oga::Priority::Normal);
        return;
    }
    g_boutTimer = t; g_boutHp1 = h1; g_boutHp2 = h2;
    if (h2 > 0.0f) g_boutFoeDown = false;      // a new exchange re-arms
    if (h1 > 0.0f) g_boutPlayerDown = false;
    if (h2 == 0.0f && !g_boutFoeDown) {
        g_boutFoeDown = true;
        const char* foe = KonquestFoeName(u32(kP2FighterIdAddr));
        char line[96];
        snprintf(line, sizeof(line), "%s defeated.", foe ? foe : "Opponent");
        Say(line, "fight", oga::Priority::Normal);
        return;
    }
    if (h1 == 0.0f && !g_boutPlayerDown) {
        g_boutPlayerDown = true;
        if (t > 0) {   // a knockout, not an expired exercise clock
            Say("Shujinko defeated.", "fight", oga::Priority::Normal);
        }
        return;
    }
    // No mid-bout health line any more: WatchMkuHealthCues (below)
    // owns health feedback now — beeps per hit, percents only at
    // the milestone crossings.
}

// ---- Sound-first fight health cues (2026-10-25, the owner's rule) ----------
// Never speak what the game already cues with a sound. The bench
// audio audit (two captured fights, ~/mku-scan/snd1 + snd2, peaks
// measured per frame against each room's own baseline) found:
//   * every hit lands with a clear impact well above the room's
//     bed (versus: tight-window median 0.53 foe / 0.45 player
//     against a near-silent restored-state floor; Konquest dojo:
//     0.26–0.45 against an ambient bed peaking 0.19) — but an
//     impact says nothing about HOW MUCH health is left;
//   * the KO blow is the loudest event of a versus fight (0.59)
//     with a slow-motion fall behind it, and the round intro is
//     sounded a beat after the round turns over (0.45–0.49 event)
//     — the game's own presentation owns both moments;
//   * the Konquest bout's entry instant has NO distinct sound
//     (0.11 in a bed peaking 0.19) and its per-phase refills are
//     silent (0.19, the bed itself).
// So, exactly as in the BBS pass: a hit raises a one-shot beep
// through the cue snapshot whose PITCH carries the new health
// fraction, and speech keeps only the milestone crossings, which
// the game does not mark at all. The milestones are Mortal Kombat
// 1's own official accessibility set — 75 / 50 / 25 / nearly gone
// at 10 — the same set the BBS pass adopted.
//
//   foe health: beep on every hit that drops P2; spoken percent
//     only at the crossings ("Foe health N percent." / "Foe
//     nearly gone.").
//   own health: the mirror for P1 ("Your health N percent." /
//     "Your health nearly gone.").
//   Exact numbers stay where they always were: the WhereAmI fight
//   line (versus and latched bouts alike).
//
// KEPT AS SPEECH, by the BBS precedent (a sound that cannot carry
// the information does not retire the line): the Konquest bout
// ENTRY line — it fires at the latch, names the foe and both
// healths before the action starts, and its instant is provably
// unsounded — and the bout OUTCOME lines ("X defeated." /
// "Shujinko defeated."): a KO thud names nobody, and in Konquest
// the defeat IS the lesson signal. Versus never had entry or
// outcome speech (its intros and wins are the game's own
// announced presentation) and gains none.
//
// PITCH IS LOGARITHMIC (perceived pitch is roughly log frequency),
// foe 1200→300 Hz (bright tick), own 800→200 Hz (duller thud
// register), panned to the owner's sides: FOE RIGHT (+0.8),
// PLAYER LEFT (−0.8) — the BBS family's exact parameters, so a
// player moving between games hears one health language. The HP
// words here are floats on 0..1, so the fraction is direct and
// the percent is the fight line's own rounding.
//
// THE TRACKED FOE is P2 — this game's fights are 1-on-1, so the
// lock-on/last-hit distinction the BBS watch needs does not
// arise: the foe in the struct is the only foe there is. In
// versus, P1 is the reader's side (the FightLine convention:
// "You lead"). Milestones latch per fighter (mode + fighter id):
// priming is silent with at-or-below milestones pre-latched;
// healing back to a milestone + 5 re-arms it (the tutorial's
// per-phase refills exercise exactly this, and stay silent
// doing it — the audit's refill measurement is why a refill is
// not an announcement any more). A drop to 0 raises NOTHING from
// this watch: a death is the game's own moment, and Konquest's
// outcome lines above own the defeat. Events stash only while a
// fight is live (InFight for versus, the bout latch for
// Konquest) — the same fail-closed shape as BBS's field gate.
//
// There is no HUD-gauge split to seam in this game (no gauge-full
// lines exist), so there is no MkuHudGaugeCues analog; the beeps
// sit behind the app's battle-cues switch like every other
// adapter's, via the snapshot bits. The app synth does not voice
// this adapter's events yet — poke_cue_health reads the same
// snapshot fields as BBS's, but the app-side polling/recipes for
// this game are the separate app queue item (see the RE doc).

bool  g_sfPendFoeBeep = false, g_sfPendOwnBeep = false;
float g_sfPendFoeHz = 0.0f, g_sfPendOwnHz = 0.0f;
struct MkuHealthTrack {
    uint32_t who = 0;       // mode-tagged fighter id; 0 = none
    float prev = -1.0f;     // last frame's HP fraction; -1 = unprimed
    bool m75 = false, m50 = false, m25 = false, m10 = false;
};
MkuHealthTrack g_sfOwnTrack, g_sfFoeTrack;
constexpr int   kSfMilestoneRearm = 5;   // percent points above a milestone
constexpr float kSfFoeHiHz = 1200.0f, kSfFoeLoHz = 300.0f;
constexpr float kSfOwnHiHz = 800.0f,  kSfOwnLoHz = 200.0f;
constexpr float kSfFoePan = 0.8f, kSfOwnPan = -0.8f;

int SfPct(float frac)
{
    int p = (int) (frac * 100.0f + 0.5f);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// Pitch from a health fraction, log-mapped between the family's
// endpoints (the BBS HealthPitch shape): fraction 1 = hi, 0 = lo.
float SfPitch(float lo, float hi, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return lo * powf(hi / lo, frac);
}

// One frame of milestone/beep tracking for one side. foeSide picks
// the family (pitch range, wording, speech priority); the pan is
// applied by the snapshot, from the same constants.
void SfTrackSide(MkuHealthTrack& t, uint32_t who, float cur, bool foeSide)
{
    if (!who) return;
    if (cur < 0.0f) cur = 0.0f;
    if (cur > 1.0f) cur = 1.0f;
    if (t.who != who) {
        // A new fighter (or the first sight): prime silently —
        // milestones at or below the current percent are already
        // passed and must not speak for a fight joined late.
        t.who = who; t.prev = cur;
        int pct = SfPct(cur);
        t.m75 = pct <= 75; t.m50 = pct <= 50;
        t.m25 = pct <= 25; t.m10 = pct <= 10;
        return;
    }
    int pct = SfPct(cur);
    if (cur < t.prev) {
        if (cur > 0.0f) {   // a hit landed (death is the game's own moment)
            if (foeSide) {
                g_sfPendFoeBeep = true;
                g_sfPendFoeHz = SfPitch(kSfFoeLoHz, kSfFoeHiHz, cur);
            } else {
                g_sfPendOwnBeep = true;
                g_sfPendOwnHz = SfPitch(kSfOwnLoHz, kSfOwnHiHz, cur);
            }
            int crossed = 0;   // latch every crossing; speak only the lowest
            if (pct <= 75) { if (!t.m75) crossed = 75; t.m75 = true; }
            if (pct <= 50) { if (!t.m50) crossed = 50; t.m50 = true; }
            if (pct <= 25) { if (!t.m25) crossed = 25; t.m25 = true; }
            if (pct <= 10) { if (!t.m10) crossed = 10; t.m10 = true; }
            if (crossed) {
                char line[64];
                if (crossed == 10)
                    snprintf(line, sizeof(line),
                             foeSide ? "Foe nearly gone."
                                     : "Your health nearly gone.");
                else
                    snprintf(line, sizeof(line),
                             foeSide ? "Foe health %d percent."
                                     : "Your health %d percent.", crossed);
                Say(line, "health",
                    foeSide ? oga::Priority::Low : oga::Priority::Normal);
            }
        }
    } else if (cur > t.prev) {
        // Healing re-arms each milestone once the percent is back
        // above it by the margin (milestone + 5).
        if (pct >= 75 + kSfMilestoneRearm) t.m75 = false;
        if (pct >= 50 + kSfMilestoneRearm) t.m50 = false;
        if (pct >= 25 + kSfMilestoneRearm) t.m25 = false;
        if (pct >= 10 + kSfMilestoneRearm) t.m10 = false;
    }
    t.prev = cur;
}

void WatchMkuHealthCues(void)
{
    if (InFight()) {
        // Versus: P1 is the reader's side, P2 the foe.
        SfTrackSide(g_sfOwnTrack, 1 + u32(kP1FighterIdAddr),
                    f32(kP1HpAddr), false);
        SfTrackSide(g_sfFoeTrack, 1 + u32(kP2FighterIdAddr),
                    f32(kP2HpAddr), true);
        if (f32(kP1HpAddr) <= 0.0f) g_sfOwnTrack = MkuHealthTrack{};
        if (f32(kP2HpAddr) <= 0.0f) g_sfFoeTrack = MkuHealthTrack{};
        return;
    }
    if (g_boutLive) {
        // Konquest bout: same struct, Konquest ids — tagged apart
        // from the versus ids so the tracks never share a latch.
        SfTrackSide(g_sfOwnTrack, 0x101 + kKonquestPlayerId,
                    f32(kP1HpAddr), false);
        SfTrackSide(g_sfFoeTrack, 0x101 + u32(kP2FighterIdAddr),
                    f32(kP2HpAddr), true);
        if (f32(kP1HpAddr) <= 0.0f) g_sfOwnTrack = MkuHealthTrack{};
        if (f32(kP2HpAddr) <= 0.0f) g_sfFoeTrack = MkuHealthTrack{};
        return;
    }
    // No live fight: forget the tracks; the next fight primes fresh.
    g_sfOwnTrack = MkuHealthTrack{};
    g_sfFoeTrack = MkuHealthTrack{};
}

void SfReset(void)
{
    g_sfPendFoeBeep = g_sfPendOwnBeep = false;
    g_sfPendFoeHz = g_sfPendOwnHz = 0.0f;
    g_sfOwnTrack = MkuHealthTrack{};
    g_sfFoeTrack = MkuHealthTrack{};
}

// The cue feed (adapter.h CueSnapshot): silent, read-only, polled
// every frame by the host's cue synth. This game has no lock-on,
// so the beacon fields stay empty (battle with no lock = the
// synth's quiet state); the sound-first one-shot health events
// ride the trailing fields exactly as BBS's do — the watches stash
// them during on_frame, and the first poll inside a live fight
// copies them out and clears them, so each hit sounds exactly
// once. A poll outside a fight is refused and consumes nothing.
static bool mku_cue_snapshot(CueSnapshot* out)
{
    if (!out) return false;
    out->battle = false;
    out->locked = false;
    out->is_core = false;
    out->dist = 0.0f;
    out->core_gain = false;
    out->foe_beep = false; out->foe_beep_hz = 0.0f; out->foe_beep_pan = 0.0f;
    out->own_beep = false; out->own_beep_hz = 0.0f; out->own_beep_pan = 0.0f;
    out->focus_cue = false; out->dlink_cue = false;
    if (!g_host) return false;
    if (!InFight() && !g_boutLive) return false;
    out->battle = true;
    out->foe_beep = g_sfPendFoeBeep;
    out->foe_beep_hz = g_sfPendFoeHz;
    out->foe_beep_pan = g_sfPendFoeBeep ? kSfFoePan : 0.0f;
    out->own_beep = g_sfPendOwnBeep;
    out->own_beep_hz = g_sfPendOwnHz;
    out->own_beep_pan = g_sfPendOwnBeep ? kSfOwnPan : 0.0f;
    g_sfPendFoeBeep = g_sfPendOwnBeep = false;
    g_sfPendFoeHz = g_sfPendOwnHz = 0.0f;
    return true;
}


// ---- Konquest free-roam position (VERIFIED 2026-10-23, pos hunts) ----------
//
// Shujinko's position in Konquest is a float vec3 in the Konquest
// world-state block: x at 0x08AED478, y (height) at +4, z at +8 — the
// same block that holds the sector word at 0x08AED5B4. (An earlier
// session noted "a float at 0x08AED480 twitches with movement": that
// is this vec3's z.) Proven on the bench (probes mku-pos/pos2/pos3,
// ~/mku-scan/pos2-pos4: 21 dumps across four independent boots):
//   * idle-stable to millimetres over 300+ frames, in two boots;
//   * every walking leg moves it smoothly and by the walked amount —
//     a sample 60 frames into a leg caught it in transit, a 480-frame
//     leg moved it ~25 units, and a leg into the village fence moved
//     it ~0.45 and then held it against the fence;
//   * pure camera rotation (L or R held 240 frames) moved it at most
//     ~0.2 (collision settle), so it is the player's feet, not a
//     camera or view value — the heap entity records near 0x08CE5000
//     that shadow it DO drift under rotation and when a new sector
//     streams in, which is what killed the 0x08CC5530 cluster in the
//     2026-10-17 hunt; this static-block vec3 does neither;
//   * the same address reads the right spot in every lineage on
//     file: the village spawn (-56.0, 154.2, hunt32's J0), Apep's
//     post (-53.85, 162.96 — a separate boot's checkpoint matches to
//     three decimals), and the dojo bout spot, frozen with the fight
//     (-58.0, 170.0).
// Axis frame: x is the east-west axis (two B-to-A sector crossings
// both rode on x dropping ~10 while z moved in opposite directions);
// z is the other ground axis, but which z direction is "north" is
// NOT proven (no row-boundary crossing on file), so no compass words
// are spoken from these numbers yet.
//
// Facing: no native yaw word survived. Across the pos2/pos3/pos4
// dump sets, every angle-shaped candidate (wrapped or unwrapped
// radians, degrees, turns, sin/cos pairs) failed at least one leg of
// the ground truth: three displacement-measured headings, two pure
// camera rotations, and an A-B-A heading cycle. So facing is
// movement-derived, following the Birth by Sleep pattern: the
// direction of the player's own recent travel, tracked below and
// exposed via RoamFacing for the navigation work to come. It is tracked, not
// spoken: a clock bearing needs a landmark to point at, and a
// compass word needs the unproven z calibration.
//
// Gate: InKonquest() and flag B == 0 (B is the fight engine: 1 in
// bouts and in the opening cinematic, 0 in free-roam — see the bout
// section) and the vec3 plausible. Dialogue and objective lines keep
// their channels and are checked before this in WhereAmI, so the
// position line answers only where nothing else is on screen.
constexpr uint32_t kKonquestPosAddr = 0x08AED478;   // f32 x, +4 y, +8 z

bool KonquestPos(float* x, float* y, float* z)
{
    float px = f32(kKonquestPosAddr);
    float py = f32(kKonquestPosAddr + 4);
    float pz = f32(kKonquestPosAddr + 8);
    if (!(px > -1000.0f && px < 1000.0f)) return false;   // also rejects NaN
    if (!(py > -50.0f && py < 100.0f)) return false;
    if (!(pz > -1000.0f && pz < 1000.0f)) return false;
    *x = px; *y = py; *z = pz;
    return true;
}

bool InFreeRoam(void)
{
    if (!InKonquest()) return false;
    if (u32(kKonquestFlagBAddr) != 0) return false;   // a bout/cinematic owns it
    float x, y, z;
    return KonquestPos(&x, &y, &z);
}

// "Position -54, 163." — the ground-plane pair, Birth by Sleep style.
void RoamPositionClause(char* out, size_t cap)
{
    float x, y, z;
    if (!KonquestPos(&x, &y, &z)) {
        snprintf(out, cap, "Position unknown.");
        return;
    }
    int ix = (int) (x + (x >= 0 ? 0.5f : -0.5f));
    int iz = (int) (z + (z >= 0 ? 0.5f : -0.5f));
    snprintf(out, cap, "Position %d, %d.", ix, iz);
}

// Movement-derived facing: a short window of recent positions (the
// last ~2 s) is kept while in free-roam; whenever the player has
// covered >= 0.5 units across that window, the facing is the window's
// displacement — the direction of travel. (Bench trajectory,
// mku-traj: the long street leg runs diagonally for ~2 s, then slides
// due -z along the west wall for the last ~2 s; the tracker reports
// the slide, which is the direction he is actually heading at rest —
// not the leg's average.) It persists after the player stops and
// clears when free-roam is left. Never a guessed direction: before
// the first tracked movement there is no facing.
constexpr int kRoamHistLen = 121;             // ~2 s of travel
float g_roamHistX[kRoamHistLen], g_roamHistZ[kRoamHistLen];
int g_roamHistHead = 0;                       // next write slot
int g_roamHistCount = 0;
float g_roamFaceDX = 0.0f, g_roamFaceDZ = 0.0f;
bool g_roamFaceValid = false;

bool RoamFacing(float* dx, float* dz)
{
    if (!g_roamFaceValid) return false;
    float len = sqrtf(g_roamFaceDX * g_roamFaceDX + g_roamFaceDZ * g_roamFaceDZ);
    if (!(len >= 0.5f)) return false;
    *dx = g_roamFaceDX / len;
    *dz = g_roamFaceDZ / len;
    return true;
}

void RoamOnFrame(void)
{
    float x, y, z;
    if (!InFreeRoam() || !KonquestPos(&x, &y, &z)) {
        g_roamHistCount = 0;
        g_roamFaceValid = false;
        return;
    }
    g_roamHistX[g_roamHistHead] = x;
    g_roamHistZ[g_roamHistHead] = z;
    g_roamHistHead = (g_roamHistHead + 1) % kRoamHistLen;
    if (g_roamHistCount < kRoamHistLen) g_roamHistCount++;
    if (g_roamHistCount == kRoamHistLen) {
        // The oldest sample sits at the head (the next write slot).
        float dx = x - g_roamHistX[g_roamHistHead];
        float dz = z - g_roamHistZ[g_roamHistHead];
        if (dx * dx + dz * dz >= 0.25f) {
            g_roamFaceDX = dx; g_roamFaceDZ = dz;
            g_roamFaceValid = true;
        }
    }
}

// ---- Konquest village landmarks — the Guide query (WIRED 2026-10-23) --------
//
// The Earthrealm village's named spots, in the position vec3's own
// frame. Sources, per entry:
//   * Apep's post (-53.49, 162.96): the placed post entity's own
//     record (apep-decomp: entity at 0x0916BBA0 reads position
//     (-53.49, 2.01, 162.96)); the player checkpoint beside it reads
//     (-53.85, 162.96) in pos3, pos4 and live10, Apep in frame.
//   * Village spawn (-55.99, 154.19): hunt32's J0 dump, an
//     independent lineage's spawn reading; every Konquest entry
//     lands there.
//   * Bo' Rai Cho's hall (-58.00, 170.00): the dojo bout lineage's
//     vec3, frozen with the fight, and the dojoprobe's navigation
//     target for the yard the hall fronts.
//   * The west fence (-63.47, 158.06): the trajectory probe's wall
//     contact at x = -63.469 and live10's street-leg endpoint — the
//     fence line the west street runs into.
// Only the village has a table; other realms' spots are unmeasured,
// so the guide speaks only here.
//
// The query mirrors Birth by Sleep's Hub Guide: one Say per
// landmark in table order, "right here" inside 1.5 units, distance
// only while no facing is tracked (the player has not moved yet —
// never a guessed direction), else distance plus a clock bearing
// from the movement-derived facing: 12 straight ahead, 3 to the
// right, 6 behind, 9 to the left, from the signed angle between the
// facing leg and the landmark leg. The left/right sign of that
// angle in this game's ground frame is not implied by the axes (x
// east-west is proven; z's cardinal run is not) — it was calibrated
// live (~/mku-scan/guide2, RE doc "Village landmark guide"): on a
// straight street walk toward +z just east of the hall, the formula
// puts the hall ahead-left (11 o'clock) and its long tiled-roof
// building fills the left of the trailing-camera frame; the
// mirrored sign would have put it on the right. Gate: free-roam
// only, and dialogue, objective and bout speech keep priority —
// the guide fails silent everywhere else, exactly like the Hub
// Guide outside its room.
struct VillageMark { const char* name; float x; float z; };
static const VillageMark kVillageMarks[] = {
    {"Apep's post", -53.49f, 162.96f},
    {"Village spawn", -55.99f, 154.19f},
    {"Bo' Rai Cho's hall", -58.00f, 170.00f},
    {"The west fence", -63.47f, 158.06f},
};
constexpr int kVillageMarkCount =
    (int) (sizeof(kVillageMarks) / sizeof(kVillageMarks[0]));

void VillageGuideLine(char* out, size_t cap, const VillageMark& m,
                      float px, float pz, bool haveFacing,
                      float fdx, float fdz)
{
    float dx = m.x - px, dz = m.z - pz;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 1.5f) {
        snprintf(out, cap, "%s, right here.", m.name);
        return;
    }
    if (!haveFacing) {
        snprintf(out, cap, "%s, %u away.", m.name,
                 (unsigned) (dist + 0.5f));
        return;
    }
    // Signed angle from the facing leg to the landmark leg, in
    // degrees: 0 ahead, positive to the right — the Hub Guide's
    // words, with the sign calibrated for this game's frame in the
    // live guide run (see the section note above).
    float inv = 1.0f / dist;
    float sinA = fdz * (dx * inv) - fdx * (dz * inv);
    float cosA = fdx * (dx * inv) + fdz * (dz * inv);
    float rel = atan2f(sinA, cosA) * 57.2957795f;
    if (rel < 0.0f) rel += 360.0f;
    int clock = (int) (rel / 30.0f + 0.5f);
    if (clock == 0) clock = 12;
    if (clock == 12)
        snprintf(out, cap, "%s, %u away, straight ahead.", m.name,
                 (unsigned) (dist + 0.5f));
    else
        snprintf(out, cap, "%s, %u away, %d o'clock.", m.name,
                 (unsigned) (dist + 0.5f), clock);
}

void CmdVillageGuide(void)
{
    if (!InFreeRoam()) return;          // Konquest roam only (A on, B off)
    if (InKonquestBout()) return;       // a bout owns speech
    if (DialogueUp() || ObjectiveUp()) return;   // those channels win
    float px, py, pz;
    if (!KonquestPos(&px, &py, &pz)) return;
    // The table is the VILLAGE's: inside this generous box around
    // the measured spots (fence line x ≈ -64, spawn z ≈ 154, hall
    // wall z ≈ 176, sector samples out to z ≈ 141) the bearings are
    // the village's own. Outside it — another realm's roam, whose
    // spots are unmeasured — stay silent rather than read village
    // directions from foreign coordinates.
    if (px < -75.0f || px > -40.0f || pz < 130.0f || pz > 190.0f) return;
    float fdx = 0.0f, fdz = 0.0f;
    bool facing = RoamFacing(&fdx, &fdz);
    for (int i = 0; i < kVillageMarkCount; i++) {
        char line[128];
        VillageGuideLine(line, sizeof(line), kVillageMarks[i], px, pz,
                         facing, fdx, fdz);
        Say(line, "guide", oga::Priority::High);
    }
}

// ---- the pause menu (VERIFIED 2026-10-17, hunts 12-13) ---------------------
//
// Items: RESUME, MOVES LIST, PLAYER SELECT, MAIN MENU (wraps both
// ends). Selection is the front-end's current-item pointer stepping
// -0xAF0 per item through four ` OBJ` pool objects carrying the shared
// class word at +0x0C — the same pattern as the other menus, with one
// hard difference: the whole pause block is allocated per match in the
// heap. Two independent boots put it 0x2E60 apart, and no static
// pointer to the holder cell exists anywhere (checked). So the adapter
// relocates it instead of hardcoding it:
//   1. OnFrame watches the round timer: while InFight() holds, a timer
//      unchanged for kPauseFreezeFrames frames means the game is
//      frozen — live play ticks it every 60 frames.
//   2. Once frozen, scan the engine-pool window for holder candidates:
//      cells whose value heads (at any position) a run of four
//      magic+class objects at -0xAF0 steps, excluding cells inside
//      the run's own objects (their link fields point into the run in
//      numbers large enough to overflow a small candidate cap — and
//      an ascending scan that stops at the cap can exclude the true
//      holder entirely, which live4's first relocation did).
//   3. The holder is the ONE candidate whose value steps by a menu
//      step (+-0xAF0, or -+0x20D0 on a wrap) when the player taps
//      up/down — and steps FIRST. The pause widget keeps trailing
//      copies of the selection in a small record ring nearby (hunt13:
//      cells at top+0xFAC's block write the previous object a few
//      frames after the holder moves); a naive one-tap match confirms
//      a trailing copy and every row speaks one tap late (live4's
//      first run did exactly that). So OnFrame polls the candidates
//      and stamps the frame each one's value last changed; the
//      confirming tap picks the stepper whose change came earliest.
//      The confirmed address is cached for the rest of the match and
//      re-validated on every use.
// Honest edges, recorded: round intros and win banners also freeze the
// timer, so a WhereAmI in the last second of a long banner, in a match
// where the player paused earlier, can briefly say "Paused". (The
// QUIT GAME dialog this menu's MAIN MENU row opens is mapped too —
// see the dialog section below; an earlier note here called it
// unmapped, from a hunt that only ever found the UI's focus-list
// pointers, which turn out to track tap parity, not the selection.)
constexpr uint32_t kPauseStride = 0xAF0;
constexpr uint32_t kMenuClassWord = 0x08BAF3C0;
constexpr uint32_t kPoolScanLo = 0x09000000;
constexpr uint32_t kPoolScanHi = 0x09200000;
constexpr int kPauseFreezeFrames = 240;
constexpr int kPauseCount = 4;
const char* const kPauseItems[] = { "Resume", "Moves List", "Player Select",
                                    "Main Menu" };

uint32_t g_pauseHolder = 0;   // holder cell address, once confirmed
uint32_t g_pauseTop = 0;      // the RESUME object's address
constexpr int kMaxPauseCand = 192;
uint32_t g_candAddr[kMaxPauseCand];
uint32_t g_candVal[kMaxPauseCand];       // live value, polled per frame
uint32_t g_candAtNav[kMaxPauseCand];     // value at the previous nav command
int g_candChangeFrame[kMaxPauseCand];    // frame g_candVal last changed
int g_candCount = -1;         // -1: not scanned yet this freeze
int g_frozenFrames = 0;
uint32_t g_lastTimer = 0xFFFFFFFF;
int g_frameNo = 0;
int g_lastNavFrame = -1;

bool PauseObjOk(uint32_t obj)
{
    return u32(obj + 4) == kObjMagic && u32(obj + 0x0C) == kMenuClassWord;
}

// If v points at any object of a complete four-object pause run, store
// the run's top (RESUME) object in *top and return true.
bool PauseRunTop(uint32_t v, uint32_t* top)
{
    if (v < kPoolScanLo || v >= kPoolScanHi) return false;
    uint32_t t = v;
    for (int i = 0; i < kPauseCount - 1 && PauseObjOk(t + kPauseStride); i++)
        t += kPauseStride;
    for (int k = 0; k < kPauseCount; k++)
        if (!PauseObjOk(t - (uint32_t) k * kPauseStride)) return false;
    *top = t;
    return true;
}

void PauseScan(void)
{
    g_candCount = 0;
    for (uint32_t a = kPoolScanLo; a < kPoolScanHi; a += 4) {
        uint32_t top;
        if (!PauseRunTop(u32(a), &top)) continue;
        // Skip cells inside the run's own objects: those are the
        // objects' link fields, not holders. (The real holder rides
        // just past the top object, at top+0xFAC in both boots seen.)
        // The cap matters too: the window holds well over 24 pointing
        // cells, and an ascending scan that stops early can cut the
        // true holder out of the set entirely — live4's first
        // relocation did exactly that and confirmed a trailing copy.
        if (a >= top - 3 * kPauseStride && a < top + kPauseStride) continue;
        if (g_candCount < kMaxPauseCand) {
            g_candAddr[g_candCount] = a;
            g_candVal[g_candCount] = u32(a);
            g_candAtNav[g_candCount] = u32(a);
            g_candChangeFrame[g_candCount] = g_frameNo;
            g_candCount++;
        }
    }
    g_lastNavFrame = g_frameNo;
}

// One nav tap while frozen: among the candidates that stepped by a
// menu step in the tap's direction since the previous nav command,
// the holder is the one whose value changed EARLIEST (trailing copies
// follow it by a few frames). A tie leaves the holder unconfirmed —
// the next tap separates them. dir: +1 next, -1 prev.
bool PauseConfirmStep(int dir)
{
    int best = -1, bestFrame = 0;
    uint32_t bestTop = 0;
    bool tie = false;
    for (int i = 0; i < g_candCount; i++) {
        uint32_t now = g_candVal[i];
        int32_t delta = (int32_t) (now - g_candAtNav[i]);
        bool step = (dir > 0)
            ? (delta == -(int32_t) kPauseStride ||
               delta == 3 * (int32_t) kPauseStride)
            : (delta == (int32_t) kPauseStride ||
               delta == -3 * (int32_t) kPauseStride);
        uint32_t top;
        if (!step || g_candChangeFrame[i] <= g_lastNavFrame ||
            !PauseRunTop(now, &top))
            continue;
        if (best < 0 || g_candChangeFrame[i] < bestFrame) {
            best = i;
            bestFrame = g_candChangeFrame[i];
            bestTop = top;
            tie = false;
        } else if (g_candChangeFrame[i] == bestFrame) {
            tie = true;
        }
    }
    for (int i = 0; i < g_candCount; i++) g_candAtNav[i] = g_candVal[i];
    g_lastNavFrame = g_frameNo;
    if (best >= 0 && !tie) {
        g_pauseHolder = g_candAddr[best];
        g_pauseTop = bestTop;
        return true;
    }
    return false;
}

bool OnPause(void)
{
    if (!InFight() || g_frozenFrames < kPauseFreezeFrames || !g_pauseHolder)
        return false;
    uint32_t v = u32(g_pauseHolder);
    if (v > g_pauseTop || (g_pauseTop - v) % kPauseStride != 0 ||
        (g_pauseTop - v) / kPauseStride >= (uint32_t) kPauseCount)
        return false;
    return PauseObjOk(v);
}

int PauseIndex(void)
{
    return (int) ((g_pauseTop - u32(g_pauseHolder)) / kPauseStride);
}

void PauseRow(char* out, size_t cap)
{
    int i = PauseIndex();
    snprintf(out, cap, "%s, %d of %d", kPauseItems[i], i + 1, kPauseCount);
}

// ---- the QUIT GAME dialog ---------------------------------------------------
//
// Choosing MAIN MENU on the pause menu opens a confirmation dialog:
// title "QUIT GAME", question "Are you sure?", options NO (left, the
// default) and YES (right). Left/right move the highlight and wrap at
// both ends; up/down do not move it. All bench-verified on two
// independent boots (hunt13 and hunt-quit2), screenshots against RAM
// at every step.
//
// Presence: a static "current context" cell at 0x08D9742C (BSS, just
// past the fight struct) names the UI layer's current context object.
// While the plain pause menu is up the cell is constant for the whole
// pause; opening the dialog moves it to the pause value + 0x98, and
// confirming NO restores the pause value. The pause value itself is a
// heap address that differs per boot, so the watch samples it whenever
// the pause menu is up without the dialog, and the dialog gate is the
// +0x98 step from that sample. (The UI focus-list cells at
// 0x08B12964/68 cannot gate presence — they keep their dialog values
// after the dialog closes — and the focus pointer cannot carry the
// selection either: it flips on EVERY direction tap, up/down
// included, while the highlight only moves on left/right. Verified:
// one UP tap flipped the pointer and the screenshot still showed NO.)
//
// Selection: the dialog's own object — same ` OBJ` family as the
// pause items — sits at a fixed offset below the pause run's top
// object, and its +0x28 field points at the selected option's
// widget: pause-top-0x3AE0 for NO, pause-top-0x45C0 for YES. Every
// offset matched exactly on both boots. The field keeps its last
// value after the dialog closes, so it is only read under the
// presence gate, and the gate fails closed on any other value.
//
// One trap the settled dumps hid: the context cell BLINKS. Watched
// frame by frame over an open dialog it holds the dialog value for
// ~10-frame runs, then shows another layer's context (or 0) for
// exactly one frame, on a ~26-frame cadence — it names the layer
// being serviced that frame, and the top layer dominates. So the
// dialog state is LATCHED: entry needs one frame at sample+0x98
// with a valid selection field, exit needs the pause value back
// for 3 consecutive frames, and the pause sample itself is only
// trusted after the same value repeats on consecutive frames.
constexpr uint32_t kScreenCtxAddr = 0x08D9742C;
constexpr uint32_t kQuitCtxStep = 0x98;
constexpr uint32_t kQuitSelOff = 0x2C28;  // selection field = pause top - this
constexpr uint32_t kQuitNoOff = 0x3AE0;   // NO widget  = pause top - this
constexpr uint32_t kQuitYesOff = 0x45C0;  // YES widget = pause top - this
constexpr int kQuitCount = 2;
const char* const kQuitItems[] = { "No", "Yes" };

uint32_t g_pauseCtx = 0;      // context-cell value of the plain pause menu
uint32_t g_ctxPrev = 0;       // context-cell value on the previous frame
int g_dlgCloseFrames = 0;     // consecutive frames back at the pause value
bool g_dlgUp = false;         // the dialog is latched open

// 0 = No, 1 = Yes, -1 = the field holds neither widget (fail closed).
int QuitDialogIndex(void)
{
    if (!g_pauseTop) return -1;
    uint32_t v = u32(g_pauseTop - kQuitSelOff);
    if (v == g_pauseTop - kQuitNoOff) return 0;
    if (v == g_pauseTop - kQuitYesOff) return 1;
    return -1;
}

// Per-frame upkeep of the context sample and the dialog latch; see
// the blink note above. Runs before the screen chain in MenuWatch.
void TrackPauseCtx(void)
{
    uint32_t ctx = u32(kScreenCtxAddr);
    if (!OnPause()) {
        g_dlgUp = false;
        g_dlgCloseFrames = 0;
    } else if (g_dlgUp) {
        // Latched: blink frames are ignored; only a sustained return
        // to the pause menu's own context value closes the dialog.
        if (ctx == g_pauseCtx) {
            if (++g_dlgCloseFrames >= 3) {
                g_dlgUp = false;
                g_dlgCloseFrames = 0;
            }
        } else {
            g_dlgCloseFrames = 0;
        }
    } else if (g_pauseCtx != 0 && ctx == g_pauseCtx + kQuitCtxStep &&
               QuitDialogIndex() >= 0) {
        g_dlgUp = true;
    } else if (ctx == g_ctxPrev && ctx != g_pauseCtx + kQuitCtxStep) {
        // A value that repeats across frames is a real layer value,
        // not a blink; and it is never the dialog's own step value,
        // so the dialog can never poison the pause sample.
        g_pauseCtx = ctx;
    }
    g_ctxPrev = ctx;
}

bool OnQuitDialog(void)
{
    return g_dlgUp && OnPause();
}

void QuitDialogRow(char* out, size_t cap)
{
    int i = QuitDialogIndex();
    if (i < 0) {
        out[0] = 0;
        return;
    }
    snprintf(out, cap, "%s, %d of %d", kQuitItems[i], i + 1, kQuitCount);
}

bool InFight(void);

// ---- the menu watch ---------------------------------------------------------
//
// ⛔ THE PAD EDGE REACHES THE GAME ON THE NEXT FRAME, SO SPEAKING FROM THE
// COMMAND IS ALWAYS ONE ROW BEHIND. The host forwards MenuNext on touch-down
// and the adapter answered immediately, before `poke_frame` had let the game
// move the cursor. Live, the row called "Options" was the Kombat row: Cross
// opened the Kombat submenu. A player could not trust a single announcement.
//
// The watch runs AFTER the frame, so it sees the settled cursor. It also speaks
// when the SCREEN changes, which is the thing a player needs and no row query
// can give: entering the main menu, the Kombat submenu, Options, character
// select or the pause menu is announced once, with its first row.
void CmdWhereAmI(void);
void MainMenuRow(char* out, size_t cap);
void KombatSubRow(char* out, size_t cap);
void OptionsTabRow(char* out, size_t cap);
void OptionsRow(char* out, size_t cap, bool with_section);
void CharRow(char* out, size_t cap);
void PauseRow(char* out, size_t cap);

static int g_uiScreen = 0;
static char g_uiRow[128] = "";
static int g_unmappedFrames = 0;

static void MenuWatch(void)
{
    int screen = 0;
    char row[128];
    row[0] = 0;

    TrackPauseCtx();
    if (OnMainMenu()) {
        screen = 1;
        MainMenuRow(row, sizeof(row));
    } else if (OnKombatSub()) {
        screen = 2;
        KombatSubRow(row, sizeof(row));
    } else if (OnOptionsTabs()) {
        screen = 3;
        OptionsTabRow(row, sizeof(row));
    } else if (OnOptionsRows()) {
        screen = 4;
        OptionsRow(row, sizeof(row), false);
    } else if (OnCharSelect()) {
        screen = 5;
        CharRow(row, sizeof(row));
    } else if (OnQuitDialog()) {
        screen = 7;
        QuitDialogRow(row, sizeof(row));
    } else if (OnPause()) {
        screen = 6;
        PauseRow(row, sizeof(row));
    }

    if (screen == 0) {
        g_uiScreen = 0;
        g_uiRow[0] = 0;
        // A fight is not an unmapped screen. Logging it every two seconds both
        // floods the log and adds a file write mid-match.
        if (InFight()) return;
        // One diagnostic per unmapped stretch: cursor/holder/expected/magic are
        // what a new screen's gate has to explain, and guessing them wastes a boot.
        if (++g_unmappedFrames % 120 == 0) {
            uint32_t ci = u32(kCharCursorAddr);
            uint32_t ch = u32(kCharHolderAddr);
            uint32_t exp = ci < (uint32_t) kCharCount
                               ? kCharObjBase - ci * kCharObjStride : 0;
            char line[192];
            snprintf(line, sizeof(line),
                     "[mku] unmapped: char cursor=%u holder=0x%08X expected=0x%08X magic=0x%08X "
                     "fight p1=%u p2=%u round=%u timer=%u hp=%.2f/%.2f",
                     ci, ch, exp, ci < (uint32_t) kCharCount ? u32(exp + 4) : 0,
                     u32(kP1FighterIdAddr), u32(kP2FighterIdAddr),
                     u32(kRoundAddr), u32(kTimerAddr),
                     (double) f32(kP1HpAddr), (double) f32(kP2HpAddr));
            Log(line);
        }
        return;
    }
    // A tracked screen resets the diagnostic so the next untracked one logs again.
    {
        static bool *reset = nullptr; (void) reset;
    }
    g_unmappedFrames = 0;
    if (screen != g_uiScreen) {
        g_uiScreen = screen;
        snprintf(g_uiRow, sizeof(g_uiRow), "%s", row);
        CmdWhereAmI();       // screen name + row, spoken once on entry
        return;
    }
    if (strcmp(row, g_uiRow) != 0) {
        snprintf(g_uiRow, sizeof(g_uiRow), "%s", row);
        Say(row, "menu", oga::Priority::High);
    }
}


void MkuOnFrame(void)
{
    g_frameNo++;
    DlgOnFrame();
    BoutOnFrame();
    WatchMkuHealthCues();
    RoamOnFrame();
    if (!InFight()) {
        g_frozenFrames = 0;
        g_lastTimer = 0xFFFFFFFF;
        g_pauseHolder = 0;
        g_pauseTop = 0;
        g_pauseCtx = 0;
        g_candCount = -1;
        MenuWatch();
        return;
    }
    uint32_t t = u32(kTimerAddr);
    if (t == g_lastTimer) {
        if (g_frozenFrames < 100000) g_frozenFrames++;
    } else {
        g_frozenFrames = 0;
        g_lastTimer = t;
        g_pauseCtx = 0;   // the freeze ended; the context sample dies with it
        g_dlgUp = false;
        g_dlgCloseFrames = 0;
    }
    if (g_frozenFrames >= kPauseFreezeFrames && g_candCount < 0 &&
        !g_pauseHolder)
        PauseScan();
    // Poll the candidates so the confirming tap can tell the holder
    // (steps first) from its trailing copies (steps a few frames on).
    if (g_candCount > 0 && !g_pauseHolder) {
        for (int i = 0; i < g_candCount; i++) {
            uint32_t v = u32(g_candAddr[i]);
            if (v != g_candVal[i]) {
                g_candVal[i] = v;
                g_candChangeFrame[i] = g_frameNo;
            }
        }
    }
    MenuWatch();
}

// ---- commands ---------------------------------------------------------------

void CmdWhereAmI(void)
{
    if (OnMainMenu()) {
        char line[128];
        char row[96];
        MainMenuRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Main menu. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnKombatSub()) {
        char line[128];
        char row[96];
        KombatSubRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Kombat menu. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnOptionsTabs()) {
        char line[128];
        char row[96];
        OptionsTabRow(row, sizeof(row));
        snprintf(line, sizeof(line), "%s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnOptionsRows()) {
        char line[192];
        char row[160];
        OptionsRow(row, sizeof(row), false);
        snprintf(line, sizeof(line), "Options, settings. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnCharSelect()) {
        char line[128];
        char row[96];
        CharRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Character select. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnQuitDialog()) {
        char line[160];
        char row[96];
        QuitDialogRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Quit game. Are you sure? %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnPause()) {
        char line[128];
        char row[96];
        PauseRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Paused. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (InKonquest()) {
        char line[224];
        if (InKonquestBout()) {
            char spoken[192];
            char bout[128];
            BoutLine(bout, sizeof(bout));
            snprintf(spoken, sizeof(spoken), "Fight. %s", bout);
            Say(spoken, "fight", oga::Priority::High);
            return;
        }
        if (ObjectiveUp() && DlgReadLine(line, sizeof(line))) {
            char spoken[256];
            snprintf(spoken, sizeof(spoken), "Objective. %s", line);
            Say(spoken, "objective", oga::Priority::High);
            strncpy(g_dlgLast, line, sizeof(g_dlgLast) - 1);
            g_dlgLast[sizeof(g_dlgLast) - 1] = 0;
            return;
        }
        if (DialogueUp() && DlgReadLine(line, sizeof(line))) {
            Say(line, "dialogue", oga::Priority::High);
            strncpy(g_dlgLast, line, sizeof(g_dlgLast) - 1);
            g_dlgLast[sizeof(g_dlgLast) - 1] = 0;
            return;
        }
        if (InFreeRoam()) {
            char pos[64];
            RoamPositionClause(pos, sizeof(pos));
            char spoken[128];
            snprintf(spoken, sizeof(spoken), "Konquest. %s", pos);
            Say(spoken, "status", oga::Priority::High);
            return;
        }
        Say("Konquest.", "status", oga::Priority::High);
        return;
    }
    if (InFight()) {
        char line[192];
        char fight[160];
        FightLine(fight, sizeof(fight));
        snprintf(line, sizeof(line), "Round %u. %s", u32(kRoundAddr), fight);
        Say(line, "status", oga::Priority::High);
        return;
    }
    // Honest scope for every other screen: mapped is claimed per-screen
    // as each one verifies, never ahead of the evidence.
    Say("Mortal Kombat Unchained. This screen is not mapped yet.", "status",
        oga::Priority::High);
}

void CmdMenuState(void)
{
    if (OnMainMenu()) {
        Log("[mku] MENU main menu");
        CmdWhereAmI();
        return;
    }
    if (OnKombatSub()) {
        Log("[mku] MENU kombat submenu");
        CmdWhereAmI();
        return;
    }
    if (OnCharSelect()) {
        Log("[mku] MENU character select");
        CmdWhereAmI();
        return;
    }
    if (OnQuitDialog()) {
        Log("[mku] MENU quit dialog");
        CmdWhereAmI();
        return;
    }
    if (OnPause()) {
        Log("[mku] MENU pause menu");
        CmdWhereAmI();
        return;
    }
    // Untracked menu: silent, per adapter.h — but leave the character gate's
    // numbers in the log, or the next screen's mismatch is a guess.
    {
        uint32_t ci = u32(kCharCursorAddr);
        uint32_t ch = u32(kCharHolderAddr);
        uint32_t exp = ci < (uint32_t) kCharCount
                           ? kCharObjBase - ci * kCharObjStride : 0;
        char line[192];
        snprintf(line, sizeof(line),
                 "[mku] unmapped screen: char cursor=%u holder=0x%08X expected=0x%08X cursor_magic=0x%08X",
                 ci, ch, exp, ci < (uint32_t) kCharCount ? u32(exp + 4) : 0);
        Log(line);
    }
}

void CmdMenuNav(Command cmd)
{
    // The pad edge reaches the game on the NEXT frame, so nothing is spoken
    // here: the frame watch names the row the cursor actually landed on and
    // announces screen entry. The pause menu still needs this tap to identify
    // its holder (a scan, not a speech), so that work stays.
    if (InFight() && !g_pauseHolder && g_candCount >= 0 &&
        g_frozenFrames >= kPauseFreezeFrames)
        PauseConfirmStep(cmd == Command::MenuNext ? 1 : -1);
}

void CmdNotMapped(void)
{
    // Ally/enemy cycling has no meaning in a 1-on-1 fighter: there is
    // exactly one opponent, and WhereAmI already speaks the full fight
    // state. The refusal stays honest rather than faking a cycle.
    Say("Not mapped yet in this game.", "status", oga::Priority::High);
}

} // namespace

static bool mku_attach(const Host* host)
{
    g_host = host;
    SfReset();   // no stale beeps or latches from a previous session
    return true;   // the game code check already happened in the registry
}

static void mku_on_frame(void) { MkuOnFrame(); }

static void mku_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();  break;
        case Command::NextAlly:        CmdNotMapped(); break;
        case Command::PrevAlly:        CmdNotMapped(); break;
        case Command::NextEnemy:       CmdNotMapped(); break;
        case Command::PrevEnemy:       CmdNotMapped(); break;
        case Command::NextUnactedAlly: CmdNotMapped(); break;
        case Command::DumpState:       CmdDump();      break;
        case Command::MenuState:       CmdMenuState(); break;
        case Command::Guide:           CmdVillageGuide(); break;
        case Command::MenuNext:        CmdMenuNav(cmd); break;
        case Command::MenuPrev:        CmdMenuNav(cmd); break;
        case Command::MenuLeft:        CmdMenuNav(cmd); break;
        case Command::MenuRight:       CmdMenuNav(cmd); break;
        // OSK commands are ignored silently: this adapter is not on a
        // tracked keyboard, and adapter.h is explicit that adapters in
        // that position do exactly that.
        default: break;
    }
}

static bool mku_ready(void)
{
    // Ready means "the adapter currently has usable game state" — true
    // on the screens whose gates verify (main menu, Kombat submenu,
    // character select), in a live fight, and in Konquest, using the
    // same gates the speech uses.
    return OnMainMenu() || OnKombatSub() || OnOptionsTabs() ||
           OnOptionsRows() || OnCharSelect() || InFight() || InKonquest();
}

static void mku_detach(void) { SfReset(); g_host = nullptr; }

// NOTE: the instance lives at oga scope (like kDragonBallZSaiyans), NOT inside
// an anonymous namespace -- otherwise the registry's `oga::kMortalKombatUnchained`
// declaration will not link (dbz_adapter_test.cpp documents the same trap).
extern const Adapter kMortalKombatUnchained;
const Adapter kMortalKombatUnchained = {
    "mku",
    "Mortal Kombat: Unchained",
    "ULUS10102",            // PSP game code for Mortal Kombat: Unchained (USA), from PARAM.SFO
    mku_attach,
    mku_on_frame,
    mku_command,
    mku_ready,
    mku_detach,
    mku_cue_snapshot,
    "Menus, character select, fight state, battle health cues (hit beeps and milestone callouts), pause, and Konquest dialogue, objectives, bouts, free-roam position and village landmark bearings",
    "Spoken: the main menu, the Kombat submenu, character select, "
    "fight state (health, round wins, timer), the pause menu and "
    "its quit confirmation dialog, the Options tabs and rows, and "
    "Konquest dialogue and objective "
    "lines as they appear, and Konquest bouts (the entry line with "
    "both healths, plus the outcome). During any fight, health is "
    "carried by sound instead of speech: a pitch-mapped beep on "
    "every hit (the foe on the right, you on the left, the pitch "
    "falling as the health falls), with the percent spoken only at "
    "75, 50, 25 and nearly gone.\n"
    "Controls: Triangle goes back in the front-end menus.\n"
    "The QUIT GAME dialog reached from the pause menu's Main Menu "
    "row is read: it asks Are you sure, with No (the default) and "
    "Yes, and the highlight is spoken as it moves. In Konquest "
    "free-roam, the Where Am I key reads your position as two "
    "numbers (the game's own ground coordinates), and the Guide "
    "key reads the Earthrealm village's landmarks — Apep's post, "
    "the village spawn, Bo' Rai Cho's hall and the west fence — "
    "each with its distance and, once you have moved, its clock "
    "bearing relative to the way you last travelled; before you "
    "have moved it gives distances only. Compass headings are "
    "not read. In Konquest bouts the round, timer and "
    "win words are not read (they are per-phase machinery), and only "
    "Bo' Rai Cho is named among Konquest opponents — others are "
    "announced as opponent.\n"
    "Supported version: Mortal Kombat: Unchained, USA (ULUS10102). "
    "Other pressings are not verified.",
};

} // namespace oga
