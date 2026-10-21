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

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[mku] Mortal Kombat: Unchained (ULUS10102) anchor scan, user RAM 0x%08X-0x%08X",
             kUserRamBase, kUserRamEnd);
    Log(line);

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

bool OnCharSelect(void)
{
    if (InKonquest()) return false;   // front-end state is stale there
    uint32_t idx = u32(kCharCursorAddr);
    if (idx >= (uint32_t) kCharCount) return false;
    uint32_t obj = kCharObjBase - idx * kCharObjStride;
    if (u32(kCharHolderAddr) != obj) return false;
    return u32(obj + 4) == kObjMagic;
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
// where the player paused earlier, can briefly say "Paused". And the
// QUIT GAME dialog (reached from MAIN MENU) is not mapped — its NO/YES
// selection lives in a generic widget list with no distinctive
// signature — so while it is open the adapter still reports the pause
// row underneath it; left/right stay silent in pause speech because
// they belong to that dialog.
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

void MkuOnFrame(void)
{
    g_frameNo++;
    DlgOnFrame();
    if (!InFight()) {
        g_frozenFrames = 0;
        g_lastTimer = 0xFFFFFFFF;
        g_pauseHolder = 0;
        g_pauseTop = 0;
        g_candCount = -1;
        return;
    }
    uint32_t t = u32(kTimerAddr);
    if (t == g_lastTimer) {
        if (g_frozenFrames < 100000) g_frozenFrames++;
    } else {
        g_frozenFrames = 0;
        g_lastTimer = t;
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
    if (OnPause()) {
        Log("[mku] MENU pause menu");
        CmdWhereAmI();
        return;
    }
    // untracked menu: silent, per adapter.h
}

void CmdMenuNav(Command cmd)
{
    // The host forwards the D-pad tap alongside this command; the cursor
    // has already moved when this runs, so speak the row it landed on.
    if (OnMainMenu()) { SayMainMenuRow(); return; }
    if (OnKombatSub()) {
        char row[96];
        KombatSubRow(row, sizeof(row));
        Say(row, "menu", oga::Priority::High);
        return;
    }
    if (OnOptionsTabs()) {
        char row[96];
        OptionsTabRow(row, sizeof(row));
        Say(row, "menu", oga::Priority::High);
        return;
    }
    if (OnOptionsRows()) {
        char row[96];
        OptionsRow(row, sizeof(row), false);
        Say(row, "menu", oga::Priority::High);
        return;
    }
    if (OnCharSelect()) { SayCharRow(); return; }
    if (InFight()) {
        // Pause menu: up/down taps also drive holder confirmation (the
        // first stepping tap identifies the holder cell). Left/right
        // stay silent — in pause they belong to the unmapped quit
        // dialog, and in the fight itself they are movement.
        if (cmd == Command::MenuNext || cmd == Command::MenuPrev) {
            if (!g_pauseHolder && g_candCount >= 0 &&
                g_frozenFrames >= kPauseFreezeFrames)
                PauseConfirmStep(cmd == Command::MenuNext ? 1 : -1);
            if (OnPause()) {
                char row[96];
                PauseRow(row, sizeof(row));
                Say(row, "menu", oga::Priority::High);
            }
        }
        return;
    }
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

static void mku_detach(void) { g_host = nullptr; }

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
    nullptr,     // cue_snapshot
    "Menus, character select, fight state, pause, and Konquest dialogue and objectives",
    "Spoken: the main menu, the Kombat submenu, character select, "
    "fight state (health, round wins, timer), the pause menu, the "
    "Options tabs and rows, and Konquest dialogue and objective "
    "lines as they appear.\n"
    "Controls: Triangle goes back in the front-end menus.\n"
    "Not mapped yet: the QUIT GAME dialog reached from the main "
    "menu — the game asks NO/YES and the selection is not read, so "
    "do not confirm a quit by ear. Konquest free-roam position and "
    "headings are not read, and Konquest bout speech is partial.\n"
    "Supported version: Mortal Kombat: Unchained, USA (ULUS10102). "
    "Other pressings are not verified.",
};

} // namespace oga
