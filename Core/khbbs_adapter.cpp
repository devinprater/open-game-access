/*
 * khbbs_adapter.cpp — Kingdom Hearts: Birth by Sleep (PSP, ULUS10505), as an
 * Open Game Access adapter.
 *
 * WHY THIS EXISTS: Birth by Sleep is a real-time action RPG whose battles run
 * on a lock-on and a Command Deck — D-pad Up/Down walks the deck, Triangle
 * fires the highlighted command. That shape is the Dissidia treatment's home
 * ground (speech for menus and on-demand queries, a host-side cue snapshot
 * for the lock-on), and this file is the scaffold that treatment starts
 * from.
 *
 * THE VERIFIED MAP (do not adjust without re-confirming against the game's
 * own screens; method in docs/reverse-engineering/kingdom-hearts-birth-by-sleep.md):
 * Verified 2026-10-17 on the USA retail build (ULUS10505, DISC_VERSION 1.00;
 * PARAM.SFO read off the disc itself), PPSSPP, hunts 4-5. Both cursors were
 * found by the screenshot-verified sequence method: a value was accepted
 * only when it matched the highlighted item at every step of a D-pad walk
 * (idle control pair included) AND was the unique exact match in all 24 MB.
 *
 *   Title menu cursor .... u16 @ 0x09920D76 (current), +2 previous,
 *                          +4 the item count (3 fresh profile; 4 once
 *                          clear data adds TRINITY ARCHIVES at index 2,
 *                          Install Game last). 0-based. Wraps.
 *   Title submenu ptr .... u32 @ 0x09920D8C. 0 while the title menu is on
 *                          top; while the difficulty select is open it
 *                          holds that object's heap address. This is the
 *                          gate: the difficulty cursor below KEEPS its
 *                          last value after the screen closes (hunt5
 *                          x0->x1), so the pointer — and only the
 *                          pointer — says which screen is on top.
 *   Difficulty cursor .... u16 @ the object + 0xF6 (current), +0xF8
 *                          (previous), +0xFA the item count (4). 0 Beginner
 *                          Mode, 1 Standard Mode, 2 Proud Mode, 3 Critical
 *                          Mode. Opens on Standard (1). Wraps.
 *
 *   ⛔ THE OBJECT BASE MOVES (bug observed 2026-10-21 in the
 *   titlegate boots, fixed 2026-10-22): the addresses above were
 *   mapped on the hunts' no-system-data lineage (title count 3),
 *   where the object sits at 0x09922EB0. On a system-data profile
 *   (title count 4, Trinity Archives present) the title leaves one
 *   extra 0x20 heap block ahead of it — present from title-build
 *   time — and the object lands at 0x09922ED0, so the old gate
 *   (exact pointer value + cursor/count at fixed addresses) read
 *   zeros and failed closed: no difficulty speech at all on those
 *   boots. Placement is deterministic per profile (three boots
 *   each lineage, khbbs-diffgate plus the original hunt dumps).
 *   The gate now FOLLOWS the pointer and verifies the object by
 *   its header words — u32 @ obj+0x00 == 0xFFFFFFFF and u32 @
 *   obj+0x0C == 0x08B90160 in every difficulty-open dump in both
 *   lineages — then reads cursor/count at the derived offsets.
 *   While the "Play the game in ... Mode?" confirm dialog is up
 *   the pointer and identity hold but the count word reads 2
 *   (the dialog's own option count), so the gate closes there,
 *   exactly as the old gate behaved on the lineage it worked on.
 *
 *   The +4 count fields are the fail-closed witnesses: on the boot
 *   dialogs (where the cursor and pointer both read 0 and would fake a
 *   title menu at NEW GAME) the title count reads garbage, not 3 — so
 *   both gates require the count before they believe the cursor.
 *
 *   ⛔ THE COUNT ALONE WAS NOT ENOUGH (bug observed 2026-10-21 in the
 *   app, fixed 2026-10-22): TITLE.ELF writes the count and cursor when
 *   the title struct is BUILT, which happens before the boot logos
 *   finish — so the old gate (pointer 0 + count + cursor) fired during
 *   the Disney logo fade and stayed fired through the opening movie
 *   and the title's own fade-in, announcing "Title menu. New Game"
 *   over a white transition while D-pad taps did nothing. The count
 *   check only ever excluded the pre-init dialogs. Reproduced and
 *   dissected on the bench (khbbs-titlegate, six cold boots, two
 *   profiles): the gate turned true at the logo fade on every boot,
 *   and every scripted D-pad probe failed until one specific event.
 *   The positive witnesses — the title is only believed when ALL hold:
 *     Flow phase, u32 @ 0x09920CD0: 1 during the boot logos, 2 during
 *       the opening movie, 3 once the title screen owns the flow.
 *     Title step, u8 @ 0x09920D98: walks 1, 2 through the logo cards,
 *       3 while the title fades in, 4 once the menu is idle. (During
 *       the movie the surrounding word is reused by the player; the
 *       phase word above is what closes the gate there.)
 *     Fade word, u32 @ 0x09920F44: ramps while the title fades in and
 *       returns to exactly 0 in the same few frames the step reaches
 *       4. Before that event the menu ignores input; after it, the
 *       cursor answers the next tap — on the skip path, on the
 *       no-skip path (movie played out, ~9,300 frames), and on a
 *       fresh profile (the no-system-data prompt path, count 3).
 *   The difficulty round-trip leaves all three in their title state,
 *   so the gate reopens the moment backing out lands — matching the
 *   game, whose title answers input immediately on return.
 *
 *   ⛔ THE PHASE IS A COUNTER, NOT A SCREEN ID (found 2026-10-24,
 *   bench arch2/arch4): the phase advances by one on every scene
 *   change inside the title module — 3 = title from boot, 4 =
 *   Trinity Archives, 5 = title again, 6 = Archives again, 7 =
 *   title. Odd phases are title instances; even phases >= 4 are
 *   Archives instances. The original == 3 gate went silent forever
 *   after a single Archives visit. OnTitleMenu accepts any odd
 *   phase >= 3 and compensates with a no-world-loaded guard (both
 *   map-id bytes read 0xFF at the title); the Archives section
 *   below carries the Archives' own gates.
 *
 * FIELD STATE (verified 2026-10-18, decomp-led; RE notes have the full
 * evidence chain). The player's actor struct sits at 0x09976090 on the
 * hunts' boot lineage (cross-boot stability NOT yet proven):
 *   Current HP ......... u16 @ actor + 0x380 (0x09976410). Max HP u16
 *                        @ actor + 0x382 (0x09976412). Layout PROVEN by
 *                        the game's own code: FUN_089327e8 computes
 *                        cur*100/max as a low-HP percentage; the spawn
 *                        init FUN_08928330 copies both from a params
 *                        record. Ventus starts at 35/35.
 *   Command Gauge ...... u16 @ actor + 0x226 (0x099762B6). Rises ~3
 *                        per connecting hit; FUN_08985fa0 compares it
 *                        against the current command's charge
 *                        threshold. No decay while in the field.
 *   Actor witnesses .... u16 @ actor + 0x330 == 1 and the flags word
 *                        @ actor + 0x234 has bit 0x20 set (both left
 *                        by the spawn init). At the title screen the
 *                        whole block reads zero, so InField() below
 *                        fails closed off-field.
 *   Player position .... floats @ 0x09961630/34/38 (x, y, z) — NOT in
 *                        the actor struct; a separate allocation. Not
 *                        spoken yet (no heading/landmark model).
 *   Deck cursor ........ u32 @ 0x099964EC: 0 Quick Blitz, 1 Aero,
 *                        2 Strike Raid (the starting deck; the cycle
 *                        covers deck commands only — the Attack row is
 *                        outside it). D-pad Down = +1, Up = -1, wraps.
 *                        The word reads 0 while the deck is DORMANT
 *                        (pre-lesson), so deck speech is CHANGE-ONLY:
 *                        the adapter speaks the landed command when the
 *                        word actually moved after a nav command, and
 *                        stays silent when it did not.
 *
 * CAMP MENU (verified 2026-10-18, fixture lineage, hunts camp1-14;
 * opened with START in the field):
 *   Main cursor ........ u16 @ main object +0x66 (0..8, wraps both
 *                        ends; unique exact-sequence hit over an
 *                        11-dump DOWN/UP walk). +0x6A the item count
 *                        (9). Items: Command Decks, Items, Abilities,
 *                        D-Links, Stats, Reports, Command Board,
 *                        Config, Game Help.
 *   Active witnesses ... u32 @ 0x08B5BEF8 == 8 and u32 @ 0x08BD194C
 *                        == 2 on EVERY camp screen; both read 0 on
 *                        the title, in the field, and after the menu
 *                        closes. The cursor itself PERSISTS STALE after
 *                        close — the witnesses, not the cursor, say the
 *                        menu is up.
 *   RELOCATION (2026-10-22, khbbs-campgate cg-p7/cg-p8/cg-p9):
 *   the camp heap does NOT shift on cold boots — a session's FIRST
 *   camp build lands on the fixture addresses on both profile
 *   lineages (with or without SYSTEM.DAT), unlike the difficulty
 *   object, which moves +0x20 when SYSTEM.DAT exists; every
 *   cursor was walked live on cold boots of both lineages, and
 *   even the off-screen stale-garbage fingerprints match (slot
 *   word 2361, inventory word 19268 on the main list). BUT the
 *   screen objects are rebuilt at fresh heap addresses on every
 *   dive/return cycle within a session (main object 0x099195F0 ->
 *   0x0992E020 -> 0x0992E0C0 across two cycles; slot widget
 *   0x09925170 -> 0x09922CF0 on the second visit), so the fixture
 *   addresses are first-build values only. The adapter reads
 *   every camp word through the game's per-session screen
 *   registry (0x09919264: slot 3 = main object, slot 6 = decks
 *   object, each zeroed while unbuilt and verified by its class
 *   tag) and the decks object's child chain (slot screen object
 *   @ +0x9C, slot widget @ its +0x84) — see the camp constants
 *   block for the full map. One behavioural correction also came
 *   out of the cold-boot check: the decks-submenu word does not
 *   freeze under the slot list on cold boots (see CampTrack).
 *   Decks submenu ...... cursor u16 @ decks object +0x126 (0..4,
 *                        wraps): Edit Deck, Meld Commands, Command
 *                        List, Choose Deck, Finish Commands.
 *   Edit Deck groups ... focus u16 @ decks object +0xA4: 0 Battle
 *                        Commands, 1 Action Commands, 2 Shotlock
 *                        Command. Navigation is spatial, not a ring.
 *   Slot list .......... cursor u16 @ slot widget +0x66, +0x6A the
 *                        slot count (8 for Battle Commands; read
 *                        dynamically).
 *   Command inventory .. cursor u16 @ slot widget +0x138, filter
 *                        tab u16 @ +0x134 (the list opened by
 *                        Cross on a slot). The cursor is 0-based over
 *                        the command rows — the "---" none entry pinned
 *                        above them is NOT counted (camp13/14: cursor
 *                        0 = Quick Blitz, 1 = Meteor Crash, 2 =
 *                        Sliding Dash, screenshot-checked). Row
 *                        contents ARE read (2026-10-19): while camp is
 *                        open, the static BSS word @ 0x08B75E70 names
 *                        the work block whose first field is the
 *                        owned-command instance pool — 10-byte records
 *                        {u16 def id, u8 level (a count for items),
 *                        u8 zero, u16 growth, u16 ability def id, u16
 *                        flags}; a consumed instance keeps everything
 *                        but its id, which is zeroed. The list is the
 *                        family-2 instances (def record byte +0 == 2:
 *                        battle, magic, items, group, special) sorted
 *                        by def id, pool order breaking ties. Def
 *                        records: 475 x 16 bytes @ 0x08B34924 (static
 *                        EBOOT data), +1 the sub-type (1 battle,
 *                        2 magic, 3 item), +8 the name pointer. The
 *                        meld diff proved the layout: melding zeroed
 *                        the consumed Fire Dash's id in place and
 *                        wrote the Sonic Blade result over the
 *                        consumed Thunder Surge's slot.
 *   There is NO single "which camp screen" word (searched: no page
 *   enum in the heap or overlay BSS). The adapter therefore TRACKS
 *   the screen: the main count reads 9 only on the main list, and
 *   exactly one cursor moves per D-pad tap (deeper screens freeze
 *   the cursors above them — the slot index provably stays put
 *   under the inventory), so the cursor that changed names the
 *   screen. Dives are inferred when the count leaves 9 under the
 *   Command Decks item; other items' sub-screens speak generically
 *   until their own hunts land.
 *
 * ⛔ These are TITLE-module addresses. BBS loads a separate ELF per mode
 * (TITLE.ELF, MENU_CAMP.ELF, MISSION.ELF, ... in USRDIR/MODULE), and this
 * state lives in the heap region the title flow owns. Nothing here is
 * claimed for the camp menu, battles, or the Command Deck — those are
 * separate hunts, and unverified fields stay out of speech.
 *
 * What exists here:
 *   * the registry identity (the core reads the disc ID from PARAM.SFO and
 *     exact-matches it, as with the other PSP adapters),
 *   * verified speech for the title menu and the difficulty select,
 *   * the Mirage Arena Hub landmark table behind the Guide query,
 *   * honest "not mapped" speech everywhere else,
 *   * DumpState: an anchor-string scan of PSP user RAM, the RE tool the
 *     next mapping passes pull threads from.
 *
 * ⛔ READ-ONLY. Like every other adapter here, this only inspects memory
 * and drives the real console's buttons when the player asks. It never
 * writes RAM.
 */
#include "adapter.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

namespace oga {
namespace {

const Host* g_host = nullptr;

// ---- PSP memory window ------------------------------------------------------
constexpr uint32_t kUserRamBase = 0x08800000;
constexpr uint32_t kUserRamEnd  = 0x0A000000;

uint16_t u16(uint32_t a) { return g_host ? g_host->read16(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }
uint8_t  u8 (uint32_t a) { return g_host ? g_host->read8 (g_host->ctx, a) : 0; }
float    f32(uint32_t a) { uint32_t u = u32(a); float f; memcpy(&f, &u, 4); return f; }

void Say(const char* s, const char* group, oga::Priority pri)
{
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

// ---- the title flow (VERIFIED 2026-10-17, hunts 4-5) -------------------------

constexpr uint32_t kTitleCursorAddr   = 0x09920D76;  // u16 current (+2: prev)
constexpr uint32_t kTitleCountAddr    = 0x09920D7A;  // u16 item count: 3 or 4
constexpr uint32_t kSubmenuPtrAddr    = 0x09920D8C;  // u32 gate pointer
// Positive "the title menu is up and answers input" witnesses
// (bench-verified 2026-10-22, six cold boots; see the header note):
constexpr uint32_t kTitlePhaseAddr    = 0x09920CD0;  // u32 flow phase == 3
constexpr uint32_t kTitleStepAddr     = 0x09920D98;  // u8 title step == 4 (idle)
constexpr uint32_t kTitleFadeAddr     = 0x09920F44;  // u32 fade word == 0 (settled)
// The difficulty object is located THROUGH the submenu pointer,
// never by its base address: the object is heap-allocated and its
// base moves with the profile — 0x09922EB0 on a no-system-data
// profile (title count 3), 0x09922ED0 with system data (count 4) —
// because a system-data title leaves one extra 0x20 heap block
// ahead of it, present from title-build time (khbbs-diffgate,
// 2026-10-22; the +0x20 cold-boot failure of the old exact-pointer
// gate). Cursor and count ride at fixed offsets from the base;
// the two header words are the object's identity, identical in
// every difficulty-open dump in both lineages (nine dumps).
constexpr uint32_t kDiffObjHead       = 0xFFFFFFFF;  // u32 @ obj+0x00 while open
constexpr uint32_t kDiffObjTag        = 0x08B90160;  // u32 @ obj+0x0C while open
constexpr uint32_t kDiffOffCursor     = 0xF6;        // u16 current (+2: prev)
constexpr uint32_t kDiffOffCount      = 0xFA;        // u16 item count == 4

// The title grows a fourth item, Trinity Archives, once any clear
// data / archives exist (fixture lineage, 2026-10-18): the order is
// prefix-stable and Install Game stays last.
const char* const kTitleItems3[] = {"New Game", "Continue", "Install Game"};
const char* const kTitleItems4[] = {
    "New Game", "Continue", "Trinity Archives", "Install Game"};
const char* const kDiffItems[] = {
    "Beginner Mode", "Standard Mode", "Proud Mode", "Critical Mode"};
constexpr int kDiffCount = 4;

// The difficulty-select object the submenu pointer currently
// names, verified by the object's own header words; 0 when the
// current submenu is anything else. Following the pointer (with
// the identity check) is what makes the gate work on both heap
// placements — an exact match on the base address silently
// failed closed on every system-data cold boot.
uint32_t DiffObj(void)
{
    uint32_t obj = u32(kSubmenuPtrAddr);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kDiffObjHead || u32(obj + 0x0C) != kDiffObjTag) return 0;
    return obj;
}
bool OnDifficulty(void)
{
    // The count witness still applies, read at the derived
    // address: while the "Play the game in ... Mode?" confirm
    // dialog is up the pointer and identity hold but the count
    // word reads 2 (the dialog's own option count), so this gate
    // closes there — the same behaviour the old gate had on the
    // one lineage where it worked.
    uint32_t obj = DiffObj();
    return obj != 0 &&
           u16(obj + kDiffOffCount) == kDiffCount &&
           u16(obj + kDiffOffCursor) < kDiffCount;
}
bool OnTitleMenu(void)
{
    // The submenu pointer is the "top screen" witness: a child screen
    // leaves the title cursor untouched underneath it (hunt5), so the
    // title speaks only when no submenu object is current. The count
    // check keeps zeroed RAM and the boot dialogs (cursor 0, pointer 0,
    // count garbage) from faking a title menu at NEW GAME. The count
    // itself names the table: 3 on a fresh profile, 4 with archives.
    //
    // The phase/step/fade triple is the positive witness that the
    // menu is actually on screen and answering input: the count and
    // cursor are written at struct-build time and hold their title
    // values through the logos, the opening movie and the fade-in
    // (the 2026-10-21 early-fire bug). Only when the flow phase is
    // a title phase, the title step has reached idle (4) and the
    // fade word has settled (0) does the real menu take input —
    // bench-verified across six cold boots (header note).
    //
    // The phase is a COUNTER of title-module scenes, not a screen
    // id (bench arch2/arch4, 2026-10-24): 3 is the title reached
    // from boot, 4 the Trinity Archives, 5 the title again after
    // backing out of them, 6 the Archives again, 7 the title —
    // odd phases are title instances, even ones Archives
    // instances, and probes at phases 5 and 7 answer the D-pad.
    // The old == 3 test therefore went silent forever after one
    // Archives visit. The difficulty select consumes no step (it
    // is a child under the submenu pointer, gate above). As the
    // compensating tighten for accepting more phases, the gate
    // also requires that no world is loaded: both map-id bytes
    // read 0xFF at the title on every lineage (the same two bytes
    // InField() treats as "no map loaded"; their named constants
    // live with the field state below).
    int count = u16(kTitleCountAddr);
    uint32_t phase = u32(kTitlePhaseAddr);
    return u32(kSubmenuPtrAddr) == 0 &&
           (count == 3 || count == 4) &&
           u16(kTitleCursorAddr) < count &&
           phase >= 3 && (phase & 1) == 1 &&
           u8(kTitleStepAddr) == 4 &&
           u32(kTitleFadeAddr) == 0 &&
           u8(0x08B61CA0) == 0xFF && u8(0x08B759C0) == 0xFF;
}

void TitleRow(char* buf, size_t n)
{
    int c = u16(kTitleCursorAddr);
    int count = u16(kTitleCountAddr);
    const char* const* items = (count == 4) ? kTitleItems4 : kTitleItems3;
    snprintf(buf, n, "%s, %d of %d", items[c], c + 1, count);
}
void DiffRow(char* buf, size_t n)
{
    uint32_t obj = DiffObj();
    int c = obj ? (int) u16(obj + kDiffOffCursor) : 0;
    if (c < 0 || c >= kDiffCount) c = 0;  // unreachable via the gate; stay safe
    snprintf(buf, n, "%s, %d of %d", kDiffItems[c], c + 1, kDiffCount);
}

// ---- the Trinity Archives (VERIFIED 2026-10-24, bench arch1-arch8) ---------
//
// Choosing TRINITY ARCHIVES on the title (the row exists once clear
// data does) leaves the title for a records mode with five rows:
// Timeline / Theater / Game Stats / Trophies / Secret Reports.
// Per the flow-counter model above, an Archives instance is any
// even phase >= 4; the title cursor resets to NEW GAME on return.
//
// The Archives' screens use the same list-widget shape as the
// title struct (header pair, cursor at +6, previous at +8, count
// at +0x0A):
//   top level: base 0x099214D0 — +0x00 0xDFFF, +0x02 0xFFF8,
//     cursor u16 @ +0x06 (0x099214D6), count u16 @ +0x0A == 5.
//     Its +0x14 word (u32 @ 0x099214E4) is the child witness, and
//     it is TWO fields, not a boolean (arch6/arch7, frame-logged):
//     the HIGH half is the sub-state — 0x0002 while ANY child
//     screen is open (Timeline's chart and text page, the Theater
//     list, Game Stats, Trophies, Secret Reports — every child
//     driven), held for ~50 frames while a child closes, and
//     0x0006 for exactly two frames when the top-level cursor
//     moves; the LOW half keeps 0x0008 at the top level after
//     any child visit (0x0000 only before the first). So "a
//     child is open" is the high-half test, never an equality:
//     the first wiring's == 0 / == 0x00020008 tests went silent
//     after one child round trip and flickered on every cursor
//     move. The top cursor stays parked on the open child's row
//     the whole time, which is how the child gates below know
//     which child they are looking at.
//   The Theater list shares one visual widget at base 0x09927640
//     (header pair at +0/+2) and keeps a private controller with
//     its own count word; Secret Reports uses NO shared widget —
//     its controller is a private cluster (below). The arch5
//     bench read the shared widget's stale remains as Reports
//     anchors (0x099276C8/0x099276D0); arch6-arch8 disproved
//     them — those words are junk while Reports is open.
// The top list wraps (UP from Timeline lands on Secret Reports).
// A freshly opened screen eats input for a few hundred frames
// after its words settle (arch2's first CROSS was swallowed
// whole), so the arrival watches below wait for a stable gate
// before speaking, the up-side mirror of the game-over debounce.
//
// What each row opens (screenshot-verified; WIRED = speaks):
//   Timeline ....... a story-segment chart (three character
//                    lanes, world-medallion nodes) opening prose
//                    text pages. Not a list — NOT wired.
//   Theater ........ WIRED: a story list — Terra / Ventus /
//                    Aqua / Final Episode / Where the Heart Goes /
//                    Blank Points; controller count u16 @
//                    0x09927704 == 6, cursor u16 @ 0x09927708.
//                    A story opens that story's cutscene list —
//                    a deeper level, not wired.
//   Game Stats ..... a static progress page (Combat / Mini-games
//                    / Story / Item Collection percentages, total
//                    play time). No cursor — NOT wired.
//   Trophies ....... an icon grid with no text labels at this
//                    level — NOT wired.
//   Secret Reports . WIRED: a text list — Xehanort's Letter,
//                    then Xehanort's Report 1..N. Its controller
//                    cluster (arch7/arch8, screenshot-walked one
//                    slow tap at a time): cursor u16 @
//                    0x09927C18, previous @ +0x02 trailing it
//                    exactly (12 at the wrap), count u16 @
//                    0x09927C20 read live (13 on the fixture =
//                    Letter + Reports 1..12 — the fixture holds
//                    no Report 13; a fuller collection reads 14
//                    and names it by the same index rule),
//                    visible-window word u16 @ 0x09927C22 == 10.
//                    The list wraps at the bottom. A report
//                    opens its text page — not wired.
constexpr uint32_t kArchBase       = 0x099214D0;
constexpr uint16_t kArchHeadA      = 0xDFFF;   // u16 @ base + 0x00
constexpr uint16_t kArchHeadB      = 0xFFF8;   // u16 @ base + 0x02
constexpr uint32_t kArchCursorAddr = 0x099214D6;  // base + 0x06
constexpr uint32_t kArchCountAddr  = 0x099214DA;  // base + 0x0A
constexpr uint32_t kArchChildAddr  = 0x099214E4;  // u32 @ base + 0x14
constexpr uint32_t kArchChildState = 0x0002;      // its HIGH half: a child is open
constexpr int kArchCount = 5;
const char* const kArchItems[] = {
    "Timeline", "Theater", "Game Stats", "Trophies", "Secret Reports"};

constexpr uint32_t kArchListHeadAddr  = 0x09927642;  // child widget + 0x02
constexpr uint32_t kTheaterCountAddr  = 0x09927704;
constexpr uint32_t kTheaterCursorAddr = 0x09927708;
constexpr int kTheaterCount = 6;
const char* const kTheaterItems[] = {
    "Terra", "Ventus", "Aqua", "Final Episode",
    "Where the Heart Goes", "Blank Points"};
constexpr uint32_t kReportsCursorAddr = 0x09927C18;
constexpr uint32_t kReportsCountAddr  = 0x09927C20;  // the live row count
constexpr uint32_t kReportsWindowAddr = 0x09927C22;  // visible rows (10)

bool ArchivesPhase(void)
{
    uint32_t phase = u32(kTitlePhaseAddr);
    return phase >= 4 && (phase & 1) == 0;
}
bool ArchivesWidget(void)
{
    return u16(kArchBase) == kArchHeadA &&
           u16(kArchBase + 2) == kArchHeadB &&
           u16(kArchCountAddr) == (uint16_t) kArchCount;
}
bool ArchivesChildOpen(void)   // the witness word's high half names it
{
    return (u32(kArchChildAddr) >> 16) == kArchChildState;
}
bool ArchivesUp(void)   // the Archives top level owns the screen
{
    if (!ArchivesPhase() || !ArchivesWidget()) return false;
    if (ArchivesChildOpen()) return false;
    return u16(kArchCursorAddr) < (uint16_t) kArchCount;
}
void ArchivesRow(char* buf, size_t n)
{
    int c = (int) u16(kArchCursorAddr);
    if (c < 0 || c >= kArchCount) c = 0;  // unreachable via the gate; stay safe
    snprintf(buf, n, "%s, %d of %d", kArchItems[c], c + 1, kArchCount);
}
bool ArchivesChildUp(void)   // some Archives child screen is open
{
    if (!ArchivesPhase() || !ArchivesWidget()) return false;
    return ArchivesChildOpen();
}
bool TheaterUp(void)
{
    if (!ArchivesChildUp()) return false;
    if (u16(kArchCursorAddr) != 1) return false;  // top cursor parked on Theater
    if (u16(kArchListHeadAddr) != kArchHeadB) return false;
    if (u16(kTheaterCountAddr) != (uint16_t) kTheaterCount) return false;
    return u16(kTheaterCursorAddr) < (uint16_t) kTheaterCount;
}
void TheaterRow(char* buf, size_t n)
{
    int c = (int) u16(kTheaterCursorAddr);
    if (c < 0 || c >= kTheaterCount) c = 0;  // unreachable via the gate; stay safe
    snprintf(buf, n, "%s, %d of %d", kTheaterItems[c], c + 1, kTheaterCount);
}
int ArchReportsCount(void)
{
    int count = (int) u16(kReportsCountAddr);
    return (count >= 1 && count <= 40) ? count : 0;
}
bool ArchReportsUp(void)
{
    if (!ArchivesChildUp()) return false;
    if (u16(kArchCursorAddr) != 4) return false;  // top cursor parked on Secret Reports
    int count = ArchReportsCount();
    if (count <= 0) return false;
    // The cluster's visible-window word: 10 while the full page
    // shows; on a collection smaller than a page it reads the
    // count itself. Either reading names this screen's cluster.
    int win = (int) u16(kReportsWindowAddr);
    if (win != 10 && win != count) return false;
    return u16(kReportsCursorAddr) < (uint16_t) count;
}
void ArchReportsRow(char* buf, size_t n)
{
    int c = (int) u16(kReportsCursorAddr);
    int count = ArchReportsCount();
    if (count <= 0 || c < 0 || c >= count) { snprintf(buf, n, "Secret Reports"); return; }
    if (c == 0) snprintf(buf, n, "Xehanort's Letter, %d of %d", 1, count);
    else snprintf(buf, n, "Xehanort's Report %d, %d of %d", c, c + 1, count);
}

// Arrival speech for the Archives screens. Entering a screen is
// not a translated command (the host drives menus with raw
// buttons), so the lines hang on on_frame's rising edge like the
// game-over and movie-pause arrivals — with a stability wait,
// because a freshly opened screen's words settle a few hundred
// frames before it answers input (see the section header). A
// child round trip drops the top gate and raises it again, so
// backing out re-announces the top level, as the Load-screen
// round trip re-announces the game-over menu.
bool g_archTopUp = false;  int g_archTopStable = 0;
bool g_theaterUp = false;  int g_theaterStable = 0;
bool g_reportsUp = false;  int g_reportsStable = 0;
constexpr int kArchStableFrames = 30;

void WatchArchives(void)
{
    bool th = TheaterUp();
    if (th) {
        if (!g_theaterUp && ++g_theaterStable >= kArchStableFrames) {
            char row[96]; TheaterRow(row, sizeof(row));
            char line[192];
            snprintf(line, sizeof(line), "Theater. %s.", row);
            Say(line, "menu", oga::Priority::High);
            g_theaterUp = true;
        }
    } else { g_theaterUp = false; g_theaterStable = 0; }

    bool rp = ArchReportsUp();
    if (rp) {
        if (!g_reportsUp && ++g_reportsStable >= kArchStableFrames) {
            char row[96]; ArchReportsRow(row, sizeof(row));
            char line[192];
            snprintf(line, sizeof(line), "Secret Reports. %s.", row);
            Say(line, "menu", oga::Priority::High);
            g_reportsUp = true;
        }
    } else { g_reportsUp = false; g_reportsStable = 0; }

    bool up = ArchivesUp();
    if (up) {
        if (!g_archTopUp && ++g_archTopStable >= kArchStableFrames) {
            char row[96]; ArchivesRow(row, sizeof(row));
            char line[192];
            snprintf(line, sizeof(line), "Trinity Archives. %s.", row);
            Say(line, "menu", oga::Priority::High);
            g_archTopUp = true;
        }
    } else { g_archTopUp = false; g_archTopStable = 0; }
}

// ---- field state (VERIFIED 2026-10-18, decomp-led) ---------------------------
//
// The player actor struct (decomp: FUN_08928330 spawn init writes the
// HP pair from a params record; FUN_089327e8 reads it for the low-HP
// check). The struct is HEAP-ALLOCATED and its base FLOATS PER BOOT:
// 0x09976090 on the tutorial lineage, 0x0996C250 on the fixture
// lineage (where its gauge and action counter provably respond to
// the player's own attacks). It is located per boot by the spawn
// fingerprints in ActorPlausible — with all of them required, the
// sweep is a unique hit in every field / hub / camp dump tested and
// finds nothing at the title screen on either lineage. The deck
// cursor word is verified ONLY on the tutorial lineage, so deck
// speech stays limited to that lineage's actor base. The character
// name comes from the save-side player block (see below), verified
// on all three lineages, so any located actor can be named; a
// relocated actor whose block cannot be found speaks health and
// gauge without a name or command.

constexpr uint32_t kTutorialActorBase = 0x09976090;
constexpr uint32_t kActorOffPtr   = 0x58;   // u32, live pointer
constexpr uint32_t kActorOffState = 0x220;  // u32, small per-module enum (1 field/hub, 12 FE field, 15 dead)
constexpr uint32_t kActorOffGauge = 0x226;  // u16, Command Gauge
constexpr uint32_t kActorOffFlags = 0x234;  // u32, bit 0x20 set
constexpr uint32_t kActorOffMark  = 0x330;  // u16 == 1 (spawn mark)
constexpr uint32_t kActorOffEngage = 0x33C; // u32, locked target's
                                            // scene id (battle41)
constexpr uint32_t kActorOffCurHp = 0x380;  // u16
constexpr uint32_t kActorOffMaxHp = 0x382;  // u16

// ---- low-health warning (VERIFIED 2026-10-24, decomp + khbbs-lowhp1) -------
//
// The game's own danger line, from its own code: FUN_089327e8 (the
// per-actor HUD update the CWCheat "Infinite HP" code led to) boosts
// the HP gauge's presentation exactly when
//     (cur * 100) / max < 26        (integer math, signed shorts)
// AND a status byte (id 0x1bf through FUN_089326a8's table; byte at
// actor+0x39A) is nonzero — it is 1 in normal play. The function's
// output byte at actor+0x5F1 tracks the test live: bench khbbs-lowhp1
// pinned Aqua's HP down a staircase in the Radiant Garden fight
// (max 149) and +0x5F1 read 49 at EVERY step at or above the line
// (cur 149/121/75/47/40/39) and 53 — the base + 4, the decomp's boost
// — at EVERY step below it (38/30/20/10/5), flipping between cur 39
// (26%) and cur 38 (25%), exactly the decomp's boundary; the gauge
// arc itself pulses in the low state (a/b screenshot diff at 3% HP
// runs ~10x the background rate). The adapter therefore computes
// the fraction with the game's own arithmetic — it does not read
// +0x5F1, because turning that byte into a boolean would need its
// un-boosted base, which lives in a global object this adapter does
// not locate.
//
// On top of the game's line the adapter adds only a latch (see
// WatchLowHp): speak once on entering the zone, stay silent while
// health stays low, and re-arm only at kLowHpRearmPct — five points
// above the game's line, so a heal that lands just over the line
// followed by one small hit does not toggle the warning.
constexpr int kLowHpPct = 26;       // the game's line: pct < 26 is low
constexpr int kLowHpRearmPct = 31;  // re-arm: pct >= 31

// ---- lock-on record + locked foe (VERIFIED 2026-10-19, battle41) -----------
// The player's lock-on component record floats per boot; it is
// LOCATED by its back-pointer to the actor (see LocateLockRecord).
//   +0x38 u32 ... the player actor
//   +0x3C u32 ... the target's scene object (0 while unlocked)
//   +0x40 f32[3]  the player's position (moves as the player walks)
//   +0x50 f32[3]  the target's position (tracks it live)
// The target's entity block starts 0x1B8 BEFORE its scene object;
// enemies share the actor layout for HP (cur/max at +0x380/+0x382,
// damage-verified: the Radiant Garden Red Hot Chili, 1200 max).
// The scene object's first word is its scene id, which the actor
// mirrors at +0x33C — that match is the locator's strongest check.
constexpr uint32_t kLockRecOffActor = 0x38;
constexpr uint32_t kLockRecOffTgt   = 0x3C;
constexpr uint32_t kLockRecOffPPos  = 0x40;
constexpr uint32_t kLockRecOffTPos  = 0x50;
constexpr uint32_t kFoeBlockBack    = 0x1B8;
constexpr uint32_t kDeckCursorAddr = 0x099964EC;          // u32

const char* const kDeckItems[] = {"Quick Blitz", "Aero", "Strike Raid"};
constexpr int kDeckKnown = 3;

// ---- save-side player block (VERIFIED 2026-10-18, three lineages) -----------
//
// The loaded save slot's player record sits at a 1 MB-aligned heap
// base (0x09F00000 in every bench lineage; the CWCheat author's
// build had it at a different aligned base, so the base is located,
// never assumed). Layout from the aligned base B:
//   u32 @ B+0x287DC  munny
//   u16 @ B+0x287E4  level
//   u16 @ B+0x287E6  max HP (mirrors the actor's max HP exactly)
//   u8  @ B+0x287F7  character id: 0 Ventus, 1 Terra, 2 Aqua
// The character byte was constant in all 13 dumps checked (field,
// camp, hub, fight, world map) and distinct per character. The
// block is located by tying it to the already-located actor: the
// max-HP mirror must equal the actor's max HP, with level, munny,
// and a plausible character byte as corroboration; only a unique
// aligned base is accepted.

constexpr uint32_t kBlockOffMunny = 0x287DC;
constexpr uint32_t kBlockOffLevel = 0x287E4;
constexpr uint32_t kBlockOffMaxHp = 0x287E6;
constexpr uint32_t kBlockOffChar  = 0x287F7;

const char* const kCharNames[] = {"Ventus", "Terra", "Aqua"};
constexpr int kCharKnown = 3;

// ---- field location (VERIFIED 2026-10-19) ----------------------------------
// The loaded map's identity, kept twice by the game:
//   u8 @ 0x08B61CA0 = world id, +1 = room id (copy A)
//   u8 @ 0x08B759C0 / +1 = the same pair (copy B)
// Crowned by differential over field dumps from three worlds
// and confirmed against the OpenKH room lists: the Land of
// Departure tutorial = world 1 room 5 "Mountain Path", the
// Mirage Arena hub = 15/1 "Hub" (the camp screen itself
// labels that spot "Hub"), the Radiant Garden plaza = 6/13
// "Central Square". Both pairs read 0xFF on the title (no
// map loaded). Copy B reads 0xFF during Theater playback
// while A tracks the scene's map, so the speech clause
// requires the two copies to AGREE — true in every gameplay
// dump measured (~1,000 snapshots), and disagreement only
// ever under-speaks during a transition, never mis-speaks.
constexpr uint32_t kWorldAddrA = 0x08B61CA0;
constexpr uint32_t kWorldAddrB = 0x08B759C0;

const char* const kWorldNames[] = {
    nullptr,                 // 0x00 EX Generic
    "Land of Departure",     // 0x01 DP
    "Dwarf Woodlands",       // 0x02 SW
    "Castle of Dreams",      // 0x03 CD
    "Enchanted Dominion",    // 0x04 SB
    "Mysterious Tower",      // 0x05 YT
    "Radiant Garden",        // 0x06 RG
    nullptr,                 // 0x07 events-only maps (the Lanes
                             //      Between); the room name
                             //      carries these
    "Olympus Coliseum",      // 0x08 HE
    "Deep Space",            // 0x09 LS
    "Destiny Islands",       // 0x0A DI
    "Neverland",             // 0x0B PP
    "Disney Town",           // 0x0C DC
    "Keyblade Graveyard",    // 0x0D KG
    nullptr,                 // 0x0E unused
    "Mirage Arena",          // 0x0F VS
    "Command Board",         // 0x10 BD
    "World Map",             // 0x11 WM
    "The Hundred Acre Wood", // 0x12 WP
};
constexpr int kWorldKnown = 0x13;

// ---- camp menu (VERIFIED 2026-10-18, fixture lineage; relocation
// audit + locator fix 2026-10-22, khbbs-campgate cg-p7/cg-p8/cg-p9) ---
//
// The camp cursors are NOT at fixed addresses. A cold boot's FIRST
// camp build lands on the fixture lineage's addresses (screen object
// 0x099195F0, slot widget 0x09925170) on both profile lineages — the
// camp heap does not shift for SYSTEM.DAT the way the difficulty
// object did — but every dive/return cycle REPLACES the screen
// objects at fresh heap addresses (main object 0x099195F0 ->
// 0x0992E020 -> 0x0992E0C0 across two cycles, cg-p8; slot widget
// 0x09925170 -> 0x09922CF0 on the second visit, cg-p9), so a fixed
// address goes stale the second time any camp screen is built in a
// session. What IS stable is the game's per-session screen
// registry at 0x09919264 (identical address on fixture dumps, cold
// std boots and cold nosys boots): slot 3 holds the main screen
// object, slot 6 the current sub-screen object (the decks object
// while the decks tree is up; the other sub-screens' objects are
// mapped in the section after next), each zeroed while its
// screen is not built. The objects carry the difficulty object's
// identity shape (head 0xFFFFFFFF @ +0x00, class tag @ +0x0C), so
// the locators below follow the registry and verify identity,
// DiffObj-style:
//   main object (tag 0x08BAD400):  cursor @ +0x66, count @ +0x6A (9)
//   decks object (tag 0x08BA59D0): group focus @ +0xA4,
//                                  decks cursor @ +0x126,
//                                  slot screen pointer @ +0x9C
//   slot screen obj (tag 0x08BA29E0): slot widget pointer @ +0x84
//   slot widget: slot cursor @ +0x66, slot count @ +0x6A,
//                filter tab @ +0x134, inventory cursor @ +0x138
// (the slot list and the command inventory are two list widgets in
// one allocation — their cursors sit 0xD2 apart in every sample:
// fixture lineage, cg-p9 visits 1 and 2, cg-std2, cg-nosys1).
constexpr uint32_t kCampRegistryAddr = 0x09919264;  // u32[] screen registry
constexpr uint32_t kCampActiveAAddr = 0x08B5BEF8; // u32 == 8 while open
constexpr uint32_t kCampActiveBAddr = 0x08BD194C; // u32 == 2 while open
constexpr uint32_t kCampObjHead     = 0xFFFFFFFF; // u32 @ any camp obj+0x00
constexpr uint32_t kCampMainObjTag  = 0x08BAD400; // u32 @ main obj+0x0C
constexpr uint32_t kCampDecksObjTag = 0x08BA59D0; // u32 @ decks obj+0x0C
constexpr uint32_t kCampPaneObjTag  = 0x08BA29E0; // u32 @ pane obj+0x0C

const char* const kCampItems[] = {
    "Command Decks", "Items", "Abilities", "D-Links", "Stats",
    "Reports", "Command Board", "Config", "Game Help"};
constexpr int kCampCount = 9;
const char* const kDecksItems[] = {
    "Edit Deck", "Meld Commands", "Command List", "Choose Deck",
    "Finish Commands"};
constexpr int kDecksCount = 5;
const char* const kGroupNames[] = {
    "Battle Commands", "Action Commands", "Shotlock Command"};
constexpr int kGroupCount = 3;

// ---- the other camp sub-screens (surveyed 2026-10-24, khbbs-scan
// runs sub1/sub2/sub3; see the RE doc's sub-screen section) ---------
// Registry slot 6 holds the CURRENT sub-screen object while one is
// open (0 on the main list); each screen's class tag names it, and
// each screen's list cursor sits at +0x66 — inline on the screen
// object (Stats, Game Help) or on a child list widget the object
// points at (Items, Abilities, D-Links, Config). Counts ride at the
// widget's +0x6C (Abilities, D-Links, Config — Config's is per-tab);
// the fixed lists (Items 2, Stats 3, Game Help 6) have no count word.
constexpr uint32_t kCampItemsObjTag  = 0x08B946C0;
constexpr uint32_t kCampItemsWidTag  = 0x08B94790;  // via obj+0x260
constexpr uint32_t kCampAbilObjTag   = 0x08B8F720;
constexpr uint32_t kCampAbilWidTag   = 0x08B8F7F0;  // via obj+0x7C
constexpr uint32_t kCampDLinkObjTag  = 0x08BB2960;
constexpr uint32_t kCampDLinkWidTag  = 0x08BB2A30;  // via obj+0x220
constexpr uint32_t kCampStatsObjTag  = 0x08BA8B00;  // cursor inline
constexpr uint32_t kCampCfgObjTag    = 0x08B92910;
constexpr uint32_t kCampCfgWidTag    = 0x08B929E0;  // via obj+0x7C
constexpr uint32_t kCampHelpObjTag   = 0x08BB4CD8;  // cursor inline
// Reports runs under a sibling module: while it is open the camp
// witnesses are dead and the registry is wiped. A static cell in
// the menu module's data names the Reports screen object instead
// (0 everywhere else — verified on two different dive histories).
constexpr uint32_t kReportsPtrAddr   = 0x0991C214;
constexpr uint32_t kReportsObjTag    = 0x08B8E2F0;  // cursor +0x66, count +0x6A

const char* const kItemsRows[] = {"Equip", "Stock"};
constexpr int kItemsCount = 2;
const char* const kStatsRows[] = {
    "Basic Stats", "Command Styles", "Finish Commands"};
constexpr int kStatsCount = 3;
const char* const kHelpRows[] = {
    "Command Deck", "Action Commands", "Dimension Links",
    "Adaptive Combat", "Mirage Arena", "Sticker Album"};
constexpr int kHelpCount = 6;
const char* const kReportsRows[] = {
    "Story", "Secret Reports", "Game Records", "Character Files",
    "The Unversed", "Ice Cream Guide", "Command Collection",
    "Treasures", "Sticker Album"};
constexpr int kReportsCount = 9;
const char* const kCfgGeneralRows[] = {
    "Controls", "Camera Up/Down", "Camera Left/Right", "Command Deck",
    "Maps", "Color Depth", "CPU Speed", "Install Data", "Game Mode"};
constexpr int kCfgGeneralCount = 9;
const char* const kCfgMultiRows[] = {
    "Nickname", "Armor Color", "Room Selection", "Room",
    "Record Sharing", "D-Link Sharing"};
constexpr int kCfgMultiCount = 6;

bool CampActive(void)
{
    return u32(kCampActiveAAddr) == 8 && u32(kCampActiveBAddr) == 2;
}

// The current main screen object, via registry slot 3, or 0. The
// count word is part of the identity: it is 9 only while the main
// list is the built screen.
uint32_t CampMainObj(void)
{
    uint32_t obj = u32(kCampRegistryAddr + 3 * 4);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kCampObjHead || u32(obj + 0x0C) != kCampMainObjTag)
        return 0;
    if (u16(obj + 0x6A) != (uint16_t) kCampCount) return 0;
    if (u16(obj + 0x66) >= (uint16_t) kCampCount) return 0;
    return obj;
}

// The current decks screen object, via registry slot 6, or 0. Its
// cursor words are NOT part of the identity: the decks cursor word
// churns under the deeper screens (see CampTrack), so only the
// head/tag words verify.
uint32_t CampDecksObj(void)
{
    uint32_t obj = u32(kCampRegistryAddr + 6 * 4);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kCampObjHead || u32(obj + 0x0C) != kCampDecksObjTag)
        return 0;
    return obj;
}

// The slot screen object (the decks object's +0x9C child), or 0
// while the slot list is not built — the pointer is 0 on the
// submenu AND in the editor's group stage; it is written when
// the slot list first opens and stays live through the inventory
// (cg-p9 dumps; the editor must therefore NOT gate on it).
uint32_t CampPaneObj(void)
{
    uint32_t decks = CampDecksObj();
    if (!decks) return 0;
    uint32_t obj = u32(decks + 0x9C);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kCampObjHead || u32(obj + 0x0C) != kCampPaneObjTag)
        return 0;
    return obj;
}

// The slot list / command inventory widget (the pane's +0x84
// child), or 0 while the slot list is not built. The count word
// (the deck's slot count, 1..16) and a plausible cursor verify the
// widget; a dangling pointer to the freed widget fails here.
uint32_t CampSlotWidget(void)
{
    uint32_t pane = CampPaneObj();
    if (!pane) return 0;
    uint32_t w = u32(pane + 0x84);
    if (w < kUserRamBase || w + 0x200 > kUserRamEnd) return 0;
    uint16_t cnt = u16(w + 0x6A);
    if (cnt < 1 || cnt > 16) return 0;
    if (u16(w + 0x66) > 64) return 0;
    return w;
}

bool OnCampMenu(void)
{
    return CampActive() && CampMainObj() != 0;
}

// A sub-screen object from registry slot 6 carrying a specific
// class tag, or 0 — head + tag verify, exactly like the decks
// locator. Slot 6 names whichever sub-screen is currently built.
uint32_t CampSubObj(uint32_t wantTag)
{
    uint32_t obj = u32(kCampRegistryAddr + 6 * 4);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kCampObjHead || u32(obj + 0x0C) != wantTag)
        return 0;
    return obj;
}

// A sub-screen's child list widget: the pointer at obj+ptrOff,
// verified by head + its own class tag.
uint32_t CampSubWidget(uint32_t obj, uint32_t ptrOff, uint32_t wantTag)
{
    if (!obj) return 0;
    uint32_t w = u32(obj + ptrOff);
    if (w < kUserRamBase || w + 0x200 > kUserRamEnd) return 0;
    if (u32(w) != kCampObjHead || u32(w + 0x0C) != wantTag) return 0;
    return w;
}

// --- Meld Commands (Command Decks -> Meld Commands) ----------------
// Mapped 2026-10-24 (bench meld runs 1-6). The decks object stays
// in registry slot 6 for the whole screen; opening Meld Commands
// hangs a meld pane off decks +0x9C (class tag kMeldPaneTag — a
// different class from the slot list's pane, so CampPaneObj fails
// closed here), and the pane's +0x88 child is the meld list widget
// (tag kMeldWidgetTag). Everything relocates between camp visits;
// only the chain is stable. Widget words (all u16):
//   +0x66  panel focus: 0 Slot 1, 1 Slot 2, 2 Item, 3 OK (OK joins
//          the ring only once a valid pair is set; while a list is
//          open the word freezes at the row the list belongs to)
//   +0x6A  panel row count: 3, or 4 once a valid pair is set
//   +0x74  phase flags: 0x200 a list is open, 0x400 the "Meld these
//          commands?" dialog is up, 0x800 "Melding complete!" is up
//   +0x14A command-list tab (eight meld tabs: All, Attacks,
//          Magic, Friendship, Movement, Defense, Reprisals,
//          Synthesis — see MeldListBuild)
//   +0x14E command-list cursor; +0x150 Synthesis (item) list cursor
//   +0x163 usable-only filter flag (a u8 — the byte at +0x164 is
//          0xFF, so a u16 read here is always nonzero)
// The confirm dialog's Yes/No cursor is stored NOWHERE readable:
// the bench's frame-series probe (meld run 11, 90 frames per
// glove state across the statics and the camp heap) found it only
// as scene-graph position floats of the glove graphic. The word
// at 0x08B5BEE8 that sparse stop-sampling first suggested is a
// blink-phase word — opposite values for the same glove state in
// different runs. So the dialog cursor is tracked from the host
// echoes instead: it starts on Yes (screenshot-proven), Left and
// Right flip it in the two-option ring, and the dialog's fall
// without the completion phase speaks the cancellation. The list
// cursor words are NOT rewritten when a list opens or the filter
// flips — the display re-bases to row 0 while the word keeps its
// old value (proven in the bench), so the tracker resyncs on the
// next word change, the deck editor's post-deck-switch rule.
constexpr uint32_t kMeldPaneTag = 0x08B9CF88;
constexpr uint32_t kMeldWidgetTag = 0x08B9CEB8;

uint32_t CampMeldPane(uint32_t decks)
{
    if (!decks) return 0;
    uint32_t pane = u32(decks + 0x9C);
    if (pane < kUserRamBase || pane + 0x100 > kUserRamEnd) return 0;
    if (u32(pane) != kCampObjHead || u32(pane + 0x0C) != kMeldPaneTag)
        return 0;
    return pane;
}

uint32_t CampMeldWidget(uint32_t decks)
{
    uint32_t pane = CampMeldPane(decks);
    if (!pane) return 0;
    uint32_t w = u32(pane + 0x88);
    if (w < kUserRamBase || w + 0x200 > kUserRamEnd) return 0;
    if (u32(w) != kCampObjHead || u32(w + 0x0C) != kMeldWidgetTag)
        return 0;
    return w;
}

// The Reports screen object, or 0. The pointer cell sits in the
// menu module's static data; the object it names is heap-built per
// visit and verified by head + tag like every other camp object.
uint32_t ReportsObj(void)
{
    uint32_t obj = u32(kReportsPtrAddr);
    if (obj < kUserRamBase || obj + 0x100 > kUserRamEnd) return 0;
    if (u32(obj) != kCampObjHead || u32(obj + 0x0C) != kReportsObjTag)
        return 0;
    return obj;
}

bool ReportsUp(void) { return ReportsObj() != 0; }

void CampRow(char* buf, size_t n)
{
    uint32_t obj = CampMainObj();
    int c = obj ? (int) u16(obj + 0x66) : 0;
    snprintf(buf, n, "%s, %d of %d", kCampItems[c], c + 1, kCampCount);
}

// Which camp screen is on top, tracked across commands (see the map
// comment: no page word exists; the count + the one cursor that moved
// name the screen). Reset whenever the camp menu is not active.
enum class CampScreen { None, Main, DecksSub, Editor, SlotList, Inventory,
                        Items, Abilities, DLinks, Stats, Reports, Config,
                        GameHelp, MeldPanel, MeldList };
CampScreen g_campScreen = CampScreen::None;
bool MeldTrack(uint32_t decks);   // Meld Commands (defined below)
int g_lastMain = -1, g_lastSub = -1, g_lastGroup = -1;
int g_lastSlot = -1, g_lastInv = -1;
int g_lastSlotRaw = -1;  // slot word as last read, garbage included

// A cursor only counts when its value is plausible for that cursor.
// The first plausible sighting syncs the remembered value; only a
// later change counts as a move.
bool CampCursorMoved(int raw, int plausibleMax, int* last)
{
    if (raw < 0 || raw > plausibleMax) return false;
    if (*last < 0) { *last = raw; return false; }
    if (raw == *last) return false;
    *last = raw;
    return true;
}

// Screen tracking, driven by each cursor's VERIFIED off-screen
// behaviour (camp7-14 dumps; word table re-measured on a straight
// Cross dive, live11, 2026-10-19; relocation audit khbbs-campgate
// cg-p7/cg-p8/cg-p9, 2026-10-22). All words are read through the
// locators above; a word whose object is not currently built reads
// as -1, which the move discipline treats as "no signal" and the
// arrival discipline treats as garbage:
//   the group word freezes at its last value under deeper screens,
//   so a change while tracked shallower means a dive or a return.
//   The SUB word only pretends to freeze: on the fixture lineage it
//   held its value under the slot list, but on cold boots opening
//   the slot list rewrites it with heap churn (0, 1, 2360 across
//   identical dives) — it is only meaningful while the submenu is
//   the tracked screen. The churn can land in the arrival frame
//   itself or a few frames later (frame trace cg-p4b: slot arrived
//   f=7465, churn at f=7469), so the Editor branch lets a slot
//   arrival outrank a same-frame sub move AND the SlotList /
//   Inventory branches ignore sub moves outright: no genuine path
//   leaves either screen straight for the submenu. The slot word
//   is unreachable above the slot list (widget not built), and
//   turns plausible the moment the list opens — but then STAYS
//   plausible for the rest of the visit, so only the
//   unreachable->plausible TRANSITION (slotArrived) proves the
//   list just opened; mere plausibility does not (it would
//   ping-pong after a back-out). The widget is freed on back-out,
//   so a tracked SlotList/Inventory whose widget stops resolving
//   has returned to the editor. The inventory word holds 0 under
//   the slot list and moves only in the inventory, so a change
//   from the seeded 0 means the inventory is up. Editor and
//   submenu are word-identical at entry (sub 0, group 0, slot
//   unreachable): the editor is named once the group focus moves
//   or the slot list arrives — a straight Cross dive is followed
//   through the cascade below in one call.
void CampTrack(void)
{
    if (!CampActive()) {
        // Reports runs under a sibling module that tears the camp
        // heap down: both witnesses are dead while it is up, yet it
        // is a camp screen for speech. Its own locator names it.
        if (ReportsObj()) {
            g_campScreen = CampScreen::Reports;
            return;
        }
        g_campScreen = CampScreen::None;
        g_lastMain = g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
        g_lastSlotRaw = -1;
        return;
    }
    uint32_t mainObj = CampMainObj();
    if (mainObj) {
        g_campScreen = CampScreen::Main;
        g_lastMain = (int) u16(mainObj + 0x66);
        g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
        g_lastSlotRaw = -1;
        return;
    }
    // The other camp sub-screens name themselves by the slot-6
    // object's class tag; their row text resolves live in
    // CampRowFor, so tracking only has to hold the identity. (The
    // Command Board item leaves camp entirely — the witnesses die
    // and it tracks as no screen, like the field under it.)
    if (CampSubObj(kCampItemsObjTag)) { g_campScreen = CampScreen::Items; return; }
    if (CampSubObj(kCampAbilObjTag))  { g_campScreen = CampScreen::Abilities; return; }
    if (CampSubObj(kCampDLinkObjTag)) { g_campScreen = CampScreen::DLinks; return; }
    if (CampSubObj(kCampStatsObjTag)) { g_campScreen = CampScreen::Stats; return; }
    if (CampSubObj(kCampCfgObjTag))   { g_campScreen = CampScreen::Config; return; }
    if (CampSubObj(kCampHelpObjTag))  { g_campScreen = CampScreen::GameHelp; return; }
    if (g_campScreen == CampScreen::Main || g_campScreen == CampScreen::None) {
        g_campScreen = CampScreen::DecksSub;  // a dive happened
        g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
        g_lastSlotRaw = -1;
    }
    uint32_t decksObj = CampDecksObj();
    // Meld Commands hangs its own pane off the decks object and
    // owns the screen while it is up — the decks sub word churns
    // under it, so none of the decks tracking below may run.
    if (MeldTrack(decksObj)) return;
    uint32_t slotW = CampSlotWidget();
    int sub  = decksObj ? (int) u16(decksObj + 0x126) : -1;
    int grp  = decksObj ? (int) u16(decksObj + 0xA4) : -1;
    int slot = slotW ? (int) u16(slotW + 0x66) : -1;
    int inv  = slotW ? (int) u16(slotW + 0x138) : -1;
    bool subMoved = CampCursorMoved(sub, 15, &g_lastSub);
    bool grpMoved = CampCursorMoved(grp, 2, &g_lastGroup);
    bool slotArrived = (g_lastSlotRaw > 64 || g_lastSlotRaw < 0) &&
                       slot >= 0 && slot <= 64;
    g_lastSlotRaw = slot;

    if (g_campScreen == CampScreen::DecksSub) {
        if (grpMoved || slotArrived) g_campScreen = CampScreen::Editor;
        if (g_campScreen == CampScreen::DecksSub) return;
        // promoted: fall through so a straight dive cascades
    }
    if (g_campScreen == CampScreen::Editor) {
        bool slotMoved = CampCursorMoved(slot, 64, &g_lastSlot);
        if (slotArrived || slotMoved) {
            // The slot list just opened. The sub word does NOT
            // reliably freeze under deeper screens: on cold boots,
            // opening the slot list rewrites it with heap churn
            // (0, 1 and 2360 observed across identical dives,
            // khbbs-campgate cg-std1 / cg-p3, 2026-10-22), and a
            // plausible churn value lands in this very frame.
            // Arrival is the stronger signal — it can only fire on
            // the garbage->plausible transition the opening causes —
            // so it wins, and the stale sub move is discarded with
            // the promotion instead of demoting the screen.
            g_campScreen = CampScreen::SlotList;
            g_lastSlot = slot;
            g_lastInv = (inv >= 0 && inv <= 1023) ? inv : -1;
            subMoved = false;
            // fall through to the SlotList block
        } else if (subMoved) {
            g_campScreen = CampScreen::DecksSub;
            g_lastGroup = g_lastSlot = g_lastInv = -1;
            return;
        } else return;
    }
    if (g_campScreen == CampScreen::SlotList) {
        if (!slotW) {
            // The widget was freed on back-out: the editor is up.
            g_campScreen = CampScreen::Editor;
            g_lastSlot = g_lastInv = -1;
            return;
        }
        bool invMoved = CampCursorMoved(inv, 1023, &g_lastInv);
        CampCursorMoved(slot, 64, &g_lastSlot);
        if (invMoved) { g_campScreen = CampScreen::Inventory; return; }
        // No subMoved exit here: the churn that rewrites the sub
        // word when the slot list opens can land frames AFTER the
        // arrival frame (cg-std3/cg-std4: the list spoke as the
        // decks submenu from its first row), and no genuine path
        // leaves the slot list straight for the submenu — CIRCLE
        // returns to the editor, which the grpMoved exit below
        // catches. The top-of-function CampCursorMoved call has
        // already synced g_lastSub, so ignoring the move here
        // loses nothing.
        if (grpMoved) {
            g_campScreen = CampScreen::Editor;
            g_lastSlot = slot;  // seed: a later slot MOVE re-promotes
            g_lastInv = -1;
        }
        return;
    }
    if (g_campScreen == CampScreen::Inventory) {
        if (!slotW) {
            // The widget was freed on back-out: the editor is up.
            g_campScreen = CampScreen::Editor;
            g_lastSlot = g_lastInv = -1;
            return;
        }
        CampCursorMoved(inv, 1023, &g_lastInv);
        if (CampCursorMoved(slot, 64, &g_lastSlot)) {
            g_campScreen = CampScreen::SlotList;
            g_lastInv = -1;
            return;
        }
        if (grpMoved) {
            g_campScreen = CampScreen::Editor;
            g_lastSlot = slot;
            g_lastInv = -1;
            return;
        }
        // subMoved ignored here too, same reason as the SlotList
        // branch: the sub word is heap churn under every screen
        // deeper than the submenu, and leaving the inventory goes
        // through the slot list or the editor, never straight to
        // the submenu.
        return;
    }
}

// ---- command inventory contents (VERIFIED 2026-10-19, fixture lineage) ------
// The instance pool + def table map is in the header comment. The
// list is rebuilt on each speech call: the pool cannot change while
// the inventory screen itself is up, and a rebuild is a few hundred
// small reads — cheap next to the actor sweep.

constexpr uint32_t kWorkPtrAddr  = 0x08B75E70;  // u32 -> work block / pool
constexpr uint32_t kDefTableAddr = 0x08B34924;  // def records, 16 B each
constexpr int kDefCount = 475;
constexpr int kPoolScan = 600;                  // pool holds ~573 slots

struct InvEntry { uint16_t id; uint8_t level; uint8_t sub; uint16_t ability; };
InvEntry g_invEntries[kPoolScan];

// A def record's name is a guest string; copy it out, rejecting
// pointers or text that do not look like a name.
bool DefName(uint16_t id, char* buf, size_t n)
{
    if (id >= kDefCount || n < 2) return false;
    uint32_t p = u32(kDefTableAddr + (uint32_t) id * 16 + 8);
    if (p < kUserRamBase || p >= kUserRamEnd) return false;
    size_t i = 0;
    while (i + 1 < n) {
        uint8_t c = u8(p + (uint32_t) i);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;
        buf[i++] = (char) c;
    }
    buf[i] = 0;
    return i > 0;
}

// Build the sorted deck-command list for one filter tab into
// g_invEntries and return its row count (-1 when the pool is
// unavailable or the tab is out of range). Shared by the deck
// inventory and the Meld Commands list, whose rows are the same
// construction read through a different widget.
// Tab: 0 All, 1 Battle (sub 1), 2 Magic (sub 2), 3 Items (sub 3),
// 4 Friendship (subs 12 and 13).
int InvListBuild(int tab)
{
    if (tab < 0 || tab > 4) return -1;
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd)
        return -1;
    int cnt = 0;
    for (int s = 0; s < kPoolScan; s++) {
        uint32_t a = pool + (uint32_t) s * 10;
        uint16_t id = u16(a);
        if (id == 0 || id >= kDefCount) continue;      // empty / freed
        if (u8(a + 3) != 0) continue;                  // not an instance
        uint32_t def = kDefTableAddr + (uint32_t) id * 16;
        if (u8(def) != 2) continue;                    // deck family only
        uint8_t sub = u8(def + 1);
        if (tab == 1 && sub != 1) continue;
        if (tab == 2 && sub != 2) continue;
        if (tab == 3 && sub != 3) continue;
        if (tab == 4 && sub != 12 && sub != 13) continue;
        uint8_t level = u8(a + 2);
        if (level > 99) continue;
        g_invEntries[cnt].id = id;
        g_invEntries[cnt].level = level;
        g_invEntries[cnt].sub = sub;
        g_invEntries[cnt].ability = u16(a + 6);
        cnt++;
    }
    // Stable insertion sort by def id (pool order breaks ties, as
    // the game's own list does: plain Fire Dash before the Combo
    // Plus copy, matching the camp14 screen).
    for (int i = 1; i < cnt; i++) {
        InvEntry t = g_invEntries[i];
        int j = i - 1;
        while (j >= 0 && g_invEntries[j].id > t.id) {
            g_invEntries[j + 1] = g_invEntries[j];
            j--;
        }
        g_invEntries[j + 1] = t;
    }
    return cnt;
}

// Format one built row: "Meteor Crash, level 5, with Combo Plus"
// (plus ", 2 of 150" when withPos). Items (sub-type 3) carry a
// count, not a level: "Potion, 60 in stock".
bool InvEntryText(const InvEntry& r, int row, int cnt, bool withPos,
                  char* buf, size_t n)
{
    char name[40];
    if (!DefName(r.id, name, sizeof(name))) return false;
    char head[96];
    if (r.sub == 3)
        snprintf(head, sizeof(head), "%s, %d in stock", name, r.level);
    else
        snprintf(head, sizeof(head), "%s, level %d", name, r.level);
    char ability[40];
    bool hasAb = r.ability && DefName(r.ability, ability, sizeof(ability));
    if (!withPos) {
        if (hasAb)
            snprintf(buf, n, "%s, with %s", head, ability);
        else
            snprintf(buf, n, "%s", head);
        return true;
    }
    if (hasAb)
        snprintf(buf, n, "%s, with %s, %d of %d", head, ability,
                 row + 1, cnt);
    else
        snprintf(buf, n, "%s, %d of %d", head, row + 1, cnt);
    return true;
}

// Describe inventory row `row` (0-based over the command rows, the
// cursor word's own counting). False when the pool is unavailable or
// the row is out of range — the caller falls back to the row number.
bool InvRowText(int row, char* buf, size_t n)
{
    if (row < 0) return false;
    // The list on screen follows the slot list's filter tab
    // (live13); an unresolvable tab fails closed.
    uint32_t slotW = CampSlotWidget();
    int tab = slotW ? (int) u16(slotW + 0x134) : -1;
    int cnt = InvListBuild(tab);
    if (cnt < 0 || row >= cnt) return false;
    return InvEntryText(g_invEntries[row], row, cnt, true, buf, n);
}

// --- Meld Commands speech -------------------------------------------
// The state model is the bench map in the locator comment above.
// Two things the game never stores had to be inferred:
//  * Picks. Nothing in RAM records a chosen ingredient (the bench
//    swept the heap, the overlay BSS and every camp object for a
//    committed def id / pool slot / instance pointer and found only
//    render scratch). But the host forwards pad presses as echo
//    commands (adapter.h: Cross is OskType, Circle is OskDelete, on
//    both platforms), and on this screen a Cross press while a list
//    is open either picks the highlighted row — the list closes —
//    or is refused and nothing changes at all. So a press is
//    snapshotted by CmdMeldCross and resolved by the tracker: the
//    list closing without a following Circle announces the pick,
//    and a press that changes nothing for 50 tracker passes is
//    spoken as the refusal the game itself never voices.
//  * The result. The NEW COMMAND preview's result is computed at
//    render time and stored nowhere, but a finished meld rewrites
//    the pool: both bench melds zeroed one ingredient's slot and
//    wrote the result over the other ingredient's slot. The tracker
//    snapshots the pool when the confirm dialog opens and diffs it
//    when the completion banner rises.
// The usable-only filter's row set cannot be rebuilt from the pool
// (for slot 2 it is the first ingredient's valid partners — recipe
// data the adapter does not carry), so filtered rows speak as row
// numbers, and a filtered pick announces the slot without a name.

// InvEntryText for meld rows: identical, except a level byte of
// 0 prints no level at all — the action commands the meld list
// adds (Jump and friends) carry 0 there and the game shows no
// numeral for them, only the mastered crown.
bool MeldEntryText(const InvEntry& e, int row, int cnt, bool withPos,
                   char* buf, size_t n)
{
    if (e.level != 0)
        return InvEntryText(e, row, cnt, withPos, buf, n);
    char name[64];
    if (!DefName(e.id, name, sizeof(name))) return false;
    char core[160];
    if (e.ability != 0) {
        char ab[64];
        if (!DefName(e.ability, ab, sizeof(ab))) return false;
        snprintf(core, sizeof(core), "%s, with %s", name, ab);
    } else {
        snprintf(core, sizeof(core), "%s", name);
    }
    if (withPos)
        snprintf(buf, n, "%s, %d of %d", core, row + 1, cnt);
    else
        snprintf(buf, n, "%s", core);
    return true;
}

const char* const kMeldCrystals[9] = {
    "Shimmering Crystal", "Fleeting Crystal", "Pulsing Crystal",
    "Wellspring Crystal", "Soothing Crystal", "Hungry Crystal",
    "Abounding Crystal", "Chaos Crystal", "Secret Gem",
};

int g_meldRow = 0;               // focus row of the active meld screen
bool g_meldActive = false;       // the meld pane is up
bool g_meldListUp = false;       // a list phase is showing
bool g_meldSynth = false;        // the open list is the Synthesis list
int g_meldEditSlot = 0;          // panel row the open list belongs to
int g_meldTab = 0;               // command-list tab
bool g_meldFilter = false;       // usable-only filter flag
bool g_meldResync = false;       // cursor word stale; display is row 0
int g_meldWord = -1;             // last raw cursor word seen
uint64_t g_meldPasses = 0;       // tracker passes while the screen is up
bool g_meldPickValid[2] = { false, false };
bool g_meldPickNamed[2] = { false, false };
InvEntry g_meldPick[2];
bool g_meldItemValid = false;
int g_meldItem = -1;             // crystal index 0..8
bool g_meldPressPick = false;    // Cross pressed while the list is up
bool g_meldPressCancel = false;  // Circle pressed while the list is up
bool g_meldRefused = false;      // refusal spoken for the current press
uint64_t g_meldPressPass = 0;
int g_meldListSession = 0;         // bumped at every list rise
int g_meldPressSession = -1;       // the session a press belongs to
int g_meldPressRow = 0;
int g_meldPressTab = 0;
int g_meldPressSlot = 0;
bool g_meldPressSynth = false;
bool g_meldPressFilter = false;
bool g_meldDialog = false;
int g_meldDlgCursor = -1;
bool g_meldDone = false;
bool g_meldResultPending = false;
int g_meldResultWait = 0;
uint16_t g_meldSnap[kPoolScan];  // pool ids when the dialog opened
bool g_meldSnapValid = false;

void MeldSay(const char* s) { Say(s, "menu", oga::Priority::High); }

void MeldResetAll()
{
    g_meldActive = g_meldListUp = g_meldSynth = false;
    g_meldPickValid[0] = g_meldPickValid[1] = false;
    g_meldPickNamed[0] = g_meldPickNamed[1] = false;
    g_meldItemValid = false; g_meldItem = -1;
    g_meldPressPick = g_meldPressCancel = g_meldRefused = false;
    g_meldDialog = g_meldDone = false;
    g_meldSnapValid = false;
    g_meldWord = -1;
}

// The panel row's line: the slot/item names plus whatever has
// locked in ("Slot 1: Fire Dash, level 4, with Combo Plus").
bool MeldPanelRowText(int row, char* buf, size_t n)
{
    switch (row) {
        case 0: case 1:
            if (g_meldPickValid[row] && g_meldPickNamed[row]) {
                char t[128];
                if (InvEntryText(g_meldPick[row], 0, 0, false,
                                 t, sizeof(t))) {
                    snprintf(buf, n, "Slot %d: %s", row + 1, t);
                    return true;
                }
            }
            if (g_meldPickValid[row]) {
                snprintf(buf, n, "Slot %d: set", row + 1);
                return true;
            }
            snprintf(buf, n, "Slot %d", row + 1);
            return true;
        case 2:
            if (g_meldItemValid && g_meldItem >= 0 && g_meldItem < 9)
                snprintf(buf, n, "Item: %s", kMeldCrystals[g_meldItem]);
            else
                snprintf(buf, n, "Item");
            return true;
        case 3:
            snprintf(buf, n, "OK");
            return true;
        default:
            return false;
    }
}

InvEntry g_meldEntries[kPoolScan];

// Build the Meld Commands list for one tab into g_meldEntries and
// return its command-row count (-1 when the pool is unavailable).
// The construction is the bench run-6 map (all 156 All-tab rows
// photographed): the pool's deck commands (family 2) WITHOUT the
// item stacks (sub 3 never appears in this list), plus the action
// commands (family 3). Deck commands order by def id; action
// commands order by the def table's menu-order byte (+7) — Slide
// (def 243, order 19) sits after Sonic Impact (def 258, order 15).
// The tabs are the meld screen's own (bench runs 7 and 9, counts
// from the wrap words): All (147 + the crystal tail), Attacks
// (sub 1, 56), Magic (sub 2, 65), Friendship (subs 12 and 13,
// 13), Movement (family 3 sub 4, 6), Defense (family 3 sub 5, 5),
// Reprisals (family 3 sub 6, 2), Synthesis (the nine crystals
// alone, as a command-list tab — no pool rows).
int MeldListBuild(int tab)
{
    if (tab < 0 || tab > 7) return -1;
    if (tab == 7) return 0;
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd)
        return -1;
    int cnt = 0;
    for (int s = 0; s < kPoolScan; s++) {
        uint32_t a = pool + (uint32_t) s * 10;
        uint16_t id = u16(a);
        if (id == 0 || id >= kDefCount) continue;
        if (u8(a + 3) != 0) continue;               // not an instance
        uint32_t def = kDefTableAddr + (uint32_t) id * 16;
        uint8_t fam = u8(def);
        uint8_t sub = u8(def + 1);
        if (fam != 2 && fam != 3) continue;
        if (fam == 2 && sub == 3) continue;         // item stacks
        if (tab == 1 && !(fam == 2 && sub == 1)) continue;
        if (tab == 2 && !(fam == 2 && sub == 2)) continue;
        if (tab == 3 && !(fam == 2 && (sub == 12 || sub == 13)))
            continue;
        if (tab == 4 && !(fam == 3 && sub == 4)) continue;
        if (tab == 5 && !(fam == 3 && sub == 5)) continue;
        if (tab == 6 && !(fam == 3 && sub == 6)) continue;
        uint8_t level = u8(a + 2);
        if (level > 99) continue;
        g_meldEntries[cnt].id = id;
        g_meldEntries[cnt].level = level;
        g_meldEntries[cnt].sub = sub;
        g_meldEntries[cnt].ability = u16(a + 6);
        cnt++;
    }
    // Stable insertion sort (pool order breaks ties): family 2 by
    // def id, then family 3 by the def table's order byte.
    for (int i = 1; i < cnt; i++) {
        InvEntry t = g_meldEntries[i];
        uint32_t tdef = kDefTableAddr + (uint32_t) t.id * 16;
        uint32_t tfam = u8(tdef);
        uint32_t tord = (tfam == 3) ? u8(tdef + 7) : t.id;
        int j = i - 1;
        while (j >= 0) {
            uint32_t jdef = kDefTableAddr +
                            (uint32_t) g_meldEntries[j].id * 16;
            uint32_t jfam = u8(jdef);
            uint32_t jord = (jfam == 3) ? u8(jdef + 7)
                                        : g_meldEntries[j].id;
            if (jfam < tfam || (jfam == tfam && jord <= tord)) break;
            g_meldEntries[j + 1] = g_meldEntries[j];
            j--;
        }
        g_meldEntries[j + 1] = t;
    }
    return cnt;
}

// One list row's line. Command rows use the meld construction with
// the meld list's own count (the All tab appends the nine
// Synthesis crystals after the commands); the Synthesis list is
// the crystals alone, in their fixed order. Filtered rows cannot
// be rebuilt (see the section comment) and speak as row numbers.
bool MeldListRowText(int row, char* buf, size_t n)
{
    if (row < 0) return false;
    if (g_meldSynth) {
        if (row > 8) return false;
        snprintf(buf, n, "%s, %d of 9", kMeldCrystals[row], row + 1);
        return true;
    }
    if (g_meldFilter) {
        snprintf(buf, n, "Row %d", row + 1);
        return true;
    }
    int cnt = MeldListBuild(g_meldTab);
    if (cnt < 0) return false;
    if (g_meldTab == 7) {
        // The Synthesis tab: the crystals alone, as list rows.
        if (row > 8) return false;
        snprintf(buf, n, "%s, %d of 9", kMeldCrystals[row], row + 1);
        return true;
    }
    int total = cnt + (g_meldTab == 0 ? 9 : 0);
    if (row < cnt)
        return MeldEntryText(g_meldEntries[row], row, total, true,
                             buf, n);
    int ci = row - cnt;
    if (g_meldTab == 0 && ci >= 0 && ci < 9) {
        snprintf(buf, n, "%s, %d of %d", kMeldCrystals[ci],
                 row + 1, total);
        return true;
    }
    return false;
}

// Whether the snapshotted press row is an item, not a command
// (chooses the refusal line's wording): the Synthesis tab's rows
// and the All tab's crystal tail qualify — the meld list carries
// no item stacks.
bool MeldPressRowIsItem()
{
    if (g_meldPressSynth || g_meldPressFilter) return false;
    if (g_meldPressTab == 7) return true;
    int cnt = MeldListBuild(g_meldPressTab);
    if (cnt < 0) return false;
    return g_meldPressRow >= cnt && g_meldPressTab == 0;
}

void MeldAnnounceRefusal()
{
    if (g_meldPressSlot == 1) {
        MeldSay("That pair won't meld.");
        return;
    }
    if (MeldPressRowIsItem()) {
        MeldSay("That's an item, not a command.");
        return;
    }
    MeldSay("That command can't be melded.");
}

// The press resolved as a pick: speak what locked in and record it
// for the panel rows and the completion diff.
void MeldAnnouncePick()
{
    char line[192];
    if (g_meldPressSynth) {
        if (g_meldPressRow >= 0 && g_meldPressRow < 9) {
            g_meldItemValid = true;
            g_meldItem = g_meldPressRow;
            snprintf(line, sizeof(line), "Item: %s.",
                     kMeldCrystals[g_meldPressRow]);
            MeldSay(line);
        }
        return;
    }
    int slot = g_meldPressSlot;
    if (slot < 0 || slot > 1) return;
    if (g_meldPressFilter) {
        // A filtered pick cannot be named (see the section comment).
        g_meldPickValid[slot] = true;
        g_meldPickNamed[slot] = false;
        snprintf(line, sizeof(line), "Slot %d set.", slot + 1);
        MeldSay(line);
        return;
    }
    int cnt = MeldListBuild(g_meldPressTab);
    if (cnt < 0) return;
    if (g_meldPressRow >= cnt || g_meldPressTab == 7) {
        // A crystal row (the All tab's tail, or the Synthesis
        // tab): it sets the item.
        int ci = (g_meldPressTab == 7) ? g_meldPressRow
                                       : g_meldPressRow - cnt;
        if (ci >= 0 && ci < 9) {
            g_meldItemValid = true;
            g_meldItem = ci;
            snprintf(line, sizeof(line), "Item: %s.",
                     kMeldCrystals[ci]);
            MeldSay(line);
        }
        return;
    }
    g_meldPick[slot] = g_meldEntries[g_meldPressRow];
    g_meldPickValid[slot] = true;
    g_meldPickNamed[slot] = true;
    char t[128];
    if (MeldEntryText(g_meldPick[slot], 0, 0, false, t, sizeof(t))) {
        snprintf(line, sizeof(line), "Slot %d: %s.", slot + 1, t);
        MeldSay(line);
    }
}

void MeldSnapshot()
{
    g_meldSnapValid = false;
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd)
        return;
    for (int s = 0; s < kPoolScan; s++)
        g_meldSnap[s] = u16(pool + (uint32_t) s * 10);
    g_meldSnapValid = true;
}

// The completion line: diff the pool against the dialog-open
// snapshot; the result sits in a slot that held an ingredient.
// Returns true once the created command has been found and
// named. At the completion banner's first frame both ingredient
// slots still read empty — the result is written a moment later
// (bench run 10's dumps) — so the tracker retries this over the
// following passes before falling back to the bare line.
bool MeldTryResult()
{
    if (g_meldSnapValid) {
        uint32_t pool = u32(kWorkPtrAddr);
        if (pool >= kUserRamBase &&
            pool + kPoolScan * 10 <= kUserRamEnd) {
            for (int s = 0; s < kPoolScan; s++) {
                uint32_t a = pool + (uint32_t) s * 10;
                uint16_t now = u16(a);
                uint16_t was = g_meldSnap[s];
                if (now == 0 || now >= kDefCount) continue;
                if (was == 0 || was >= kDefCount || was == now)
                    continue;
                uint32_t wdef = kDefTableAddr + (uint32_t) was * 16;
                if (u8(wdef) != 2) continue;   // not a command slot
                if ((g_meldPickValid[0] && was != g_meldPick[0].id) &&
                    (g_meldPickValid[1] && was != g_meldPick[1].id))
                    continue;                  // not an ingredient slot
                if (g_meldPickValid[0] && !g_meldPickValid[1] &&
                    was != g_meldPick[0].id)
                    continue;
                if (g_meldPickValid[1] && !g_meldPickValid[0] &&
                    was != g_meldPick[1].id)
                    continue;
                InvEntry r;
                r.id = now;
                r.level = u8(a + 2);
                r.sub = u8(kDefTableAddr + (uint32_t) now * 16 + 1);
                r.ability = u16(a + 6);
                char t[128];
                if (InvEntryText(r, 0, 0, false, t, sizeof(t))) {
                    char line[192];
                    snprintf(line, sizeof(line),
                             "Melding complete. %s.", t);
                    MeldSay(line);
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

// Cross pressed while a meld list is open (the host's echo for the
// confirm button): snapshot the press for the tracker to resolve.
void CmdMeldCross()
{
    if (g_campScreen != CampScreen::MeldList || !g_meldListUp) return;
    g_meldPressPick = true;
    g_meldPressCancel = false;
    g_meldRefused = false;
    g_meldPressPass = g_meldPasses;
    g_meldPressSession = g_meldListSession;
    g_meldPressRow = g_meldRow;
    g_meldPressTab = g_meldTab;
    g_meldPressSlot = g_meldEditSlot;
    g_meldPressSynth = g_meldSynth;
    g_meldPressFilter = g_meldFilter;
}

// Circle pressed while a meld list is open: the coming close is a
// cancel, not a pick.
void CmdMeldCircle()
{
    if (g_campScreen != CampScreen::MeldList || !g_meldListUp) return;
    g_meldPressCancel = true;
}

// Left/Right while the confirm dialog is up: the dialog's cursor
// is echo-tracked (it is stored nowhere readable — see the meld
// constants comment). It starts on Yes; either direction flips it
// in the two-option ring.
void CmdMeldFlip()
{
    if (!g_meldDialog) return;
    g_meldDlgCursor = (g_meldDlgCursor == 0) ? 1 : 0;
    MeldSay(g_meldDlgCursor == 1 ? "No." : "Yes.");
}

// Per-pass Meld Commands tracking; called from CampTrack with the
// decks object (0 when camp is elsewhere). Returns true while the
// meld screen owns the camp display.
bool MeldTrack(uint32_t decks)
{
    uint32_t w = CampMeldWidget(decks);
    if (!w) {
        if (g_meldActive) {
            MeldResetAll();
            // Hand the display back as a freshly seeded decks
            // submenu (the screen the game returns to), so the
            // churned sub word the meld screen leaves behind is
            // re-read as a cursor seed, never as a move.
            if (g_campScreen == CampScreen::MeldPanel ||
                g_campScreen == CampScreen::MeldList) {
                g_campScreen = CampScreen::DecksSub;
                g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
                g_lastSlotRaw = -1;
            }
        }
        return false;
    }
    bool entered = !g_meldActive;
    g_meldActive = true;
    g_meldPasses++;
    uint16_t phase = u16(w + 0x74);
    bool listUp = (phase & 0x200) != 0;
    bool dlgUp = (phase & 0x400) != 0;
    bool doneUp = (phase & 0x800) != 0;
    int glove = (int) u16(w + 0x66);
    if (glove < 0 || glove > 3) glove = 0;

    if (entered) {
        g_meldListUp = g_meldDialog = g_meldDone = false;
        g_meldPressPick = g_meldPressCancel = false;
        g_campScreen = CampScreen::MeldPanel;
        g_meldRow = glove;
        char t[128];
        if (MeldPanelRowText(glove, t, sizeof(t))) {
            char line[160];
            snprintf(line, sizeof(line), "Meld Commands. %s.", t);
            MeldSay(line);
        }
    }

    // The completion banner: the meld executed under the dialog.
    // The result record lands a little after the banner rises, so
    // the announcement is retried over the following passes.
    if (doneUp && !g_meldDone) {
        g_meldDone = true;
        g_meldResultPending = true;
        g_meldResultWait = 0;
    } else if (!doneUp) {
        g_meldDone = false;
    }
    if (g_meldResultPending) {
        if (MeldTryResult() || ++g_meldResultWait > 900) {
            if (g_meldResultWait > 900) MeldSay("Melding complete.");
            g_meldResultPending = false;
            g_meldPickValid[0] = g_meldPickValid[1] = false;
            g_meldPickNamed[0] = g_meldPickNamed[1] = false;
            g_meldItemValid = false;
        }
    }

    // The confirm dialog's rise and fall. The cursor itself is
    // echo-tracked (see the constants comment): it starts on Yes.
    if (dlgUp && !g_meldDialog) {
        g_meldDialog = true;
        MeldSnapshot();
        g_meldDlgCursor = 0;
        g_meldPressPick = g_meldPressCancel = false;
        MeldSay("Meld these commands? Yes.");
    } else if (!dlgUp && g_meldDialog) {
        g_meldDialog = false;
        // A fall the completion banner never blessed: the player
        // chose No or backed out. (On the completion path the
        // banner rises first and the result flow has taken over.)
        if (!g_meldDone && !doneUp) MeldSay("Melding cancelled.");
    }

    // List rise: seed from the widget's frozen words. The glove
    // must sit on the row the list belongs to (0, 1 or 2): the
    // meld execution and the dialog machinery also raise the list
    // phase bit, with the glove at 3 (OK), and those phases are
    // not lists — a press echo landing in one must never be read
    // as a list press (bench runs 12/13's spurious refusal).
    if (listUp && !g_meldListUp && glove >= 0 && glove <= 2) {
        g_meldListUp = true;
        g_meldListSession++;
        g_meldEditSlot = glove;
        g_meldSynth = (glove == 2);
        g_meldTab = (int) u16(w + 0x14A);
        if (g_meldTab < 0 || g_meldTab > 7) g_meldTab = 0;
        g_meldFilter = u8(w + 0x163) != 0;
        g_meldWord = (int) u16(w + (g_meldSynth ? 0x150 : 0x14E));
        g_meldResync = true;    // the display re-based to row 0
        g_meldPressPick = g_meldPressCancel = false;
        g_meldRefused = false;
        g_campScreen = CampScreen::MeldList;
        g_meldRow = 0;
        return true;
    }
    // List fall: resolve the press bookkeeping.
    if (!listUp && g_meldListUp) {
        if (g_meldPressPick && !g_meldPressCancel && !g_meldRefused)
            MeldAnnouncePick();
        g_meldPressPick = g_meldPressCancel = false;
        g_meldListUp = false;
        g_campScreen = CampScreen::MeldPanel;
        g_meldRow = glove;
        return true;
    }
    if (g_meldListUp) {
        if (!g_meldSynth) {
            int tab = (int) u16(w + 0x14A);
            if (tab != g_meldTab && tab >= 0 && tab <= 7) {
                g_meldTab = tab;
                g_meldWord = (int) u16(w + 0x14E);  // rewritten to 0
                g_meldResync = false;
            }
            bool filt = u8(w + 0x163) != 0;
            if (filt != g_meldFilter) {
                g_meldFilter = filt;
                g_meldResync = true;   // display re-based to row 0
            }
        }
        int word = (int) u16(w + (g_meldSynth ? 0x150 : 0x14E));
        if (word != g_meldWord) {
            g_meldWord = word;
            g_meldResync = false;      // a real move: the word is truth
        }
        g_meldRow = g_meldResync ? 0 : g_meldWord;
        // A press that neither closed the list nor resolved within
        // the window was refused; the game says nothing, so we do.
        // The press must belong to THIS list session: the meld
        // execution briefly re-raises list-like phases, and a
        // snapshot from an earlier session must never refuse there
        // (bench run 12's spurious refusal at the execute press).
        if (g_meldPressPick && !g_meldRefused &&
            g_meldPressSession == g_meldListSession &&
            g_meldPasses - g_meldPressPass > 50) {
            g_meldRefused = true;
            MeldAnnounceRefusal();
        }
        return true;
    }

    g_campScreen = CampScreen::MeldPanel;
    g_meldRow = glove;
    return true;
}

// The generic line for a dived screen whose own row cannot be
// resolved this frame: the camp item's name, as before this hunt.
void CampItemFallback(char* buf, size_t n)
{
    if (g_lastMain >= 0 && g_lastMain < kCampCount)
        snprintf(buf, n, "%s", kCampItems[g_lastMain]);
    else
        snprintf(buf, n, "Camp menu");
}

void CampRowFor(CampScreen s, char* buf, size_t n)
{
    switch (s) {
        case CampScreen::Main: CampRow(buf, n); return;
        case CampScreen::Items: {
            uint32_t w = CampSubWidget(CampSubObj(kCampItemsObjTag),
                                       0x260, kCampItemsWidTag);
            int cur = w ? (int) u16(w + 0x66) : -1;
            if (cur >= 0 && cur < kItemsCount)
                snprintf(buf, n, "%s, %d of %d", kItemsRows[cur],
                         cur + 1, kItemsCount);
            else
                CampItemFallback(buf, n);
            return;
        }
        case CampScreen::Abilities: {
            uint32_t w = CampSubWidget(CampSubObj(kCampAbilObjTag),
                                       0x7C, kCampAbilWidTag);
            int cur = w ? (int) u16(w + 0x66) : -1;
            int cnt = w ? (int) u16(w + 0x6C) : 0;
            if (w && cnt >= 1 && cnt <= 64 && cur >= 0 && cur < cnt) {
                // Rows are 12-byte records at the widget's +0x80:
                // def id @ +0, installed count @ +6, zero-terminated
                // (29 on the fixture save; the count word agrees).
                uint32_t rec = w + 0x80 + (uint32_t) cur * 12;
                uint16_t id = u16(rec);
                char name[48];
                if (id && DefName(id, name, sizeof(name))) {
                    int inst = (int) u16(rec + 6);
                    if (inst >= 1 && inst <= 9)
                        snprintf(buf, n, "%s, %d installed, %d of %d",
                                 name, inst, cur + 1, cnt);
                    else
                        snprintf(buf, n, "%s, %d of %d", name,
                                 cur + 1, cnt);
                    return;
                }
                snprintf(buf, n, "Ability %d of %d", cur + 1, cnt);
                return;
            }
            CampItemFallback(buf, n);
            return;
        }
        case CampScreen::DLinks: {
            uint32_t obj = CampSubObj(kCampDLinkObjTag);
            uint32_t w = CampSubWidget(obj, 0x220, kCampDLinkWidTag);
            int cur = w ? (int) u16(w + 0x66) : -1;
            int cnt = w ? (int) u16(w + 0x6C) : 0;
            if (obj && w && cnt >= 1 && cnt <= 32 &&
                cur >= 0 && cur < cnt) {
                // Rows are 20-byte records at the OBJECT's +0x80,
                // def id @ +0 (8 on the fixture save).
                uint16_t id = u16(obj + 0x80 + (uint32_t) cur * 20);
                char name[48];
                if (id && DefName(id, name, sizeof(name))) {
                    snprintf(buf, n, "%s, %d of %d", name, cur + 1, cnt);
                    return;
                }
                snprintf(buf, n, "D-Link %d of %d", cur + 1, cnt);
                return;
            }
            CampItemFallback(buf, n);
            return;
        }
        case CampScreen::Stats: {
            uint32_t obj = CampSubObj(kCampStatsObjTag);
            int cur = obj ? (int) u16(obj + 0x66) : -1;
            if (cur >= 0 && cur < kStatsCount)
                snprintf(buf, n, "%s, %d of %d", kStatsRows[cur],
                         cur + 1, kStatsCount);
            else
                CampItemFallback(buf, n);
            return;
        }
        case CampScreen::Reports: {
            uint32_t obj = ReportsObj();
            int cur = obj ? (int) u16(obj + 0x66) : -1;
            int cnt = obj ? (int) u16(obj + 0x6A) : 0;
            int total = (cnt >= 1 && cnt <= kReportsCount)
                            ? cnt : kReportsCount;
            if (obj && cur >= 0 && cur < total)
                snprintf(buf, n, "%s, %d of %d", kReportsRows[cur],
                         cur + 1, total);
            else
                snprintf(buf, n, "Reports");
            return;
        }
        case CampScreen::Config: {
            uint32_t w = CampSubWidget(CampSubObj(kCampCfgObjTag),
                                       0x7C, kCampCfgWidTag);
            int cur = w ? (int) u16(w + 0x66) : -1;
            int cnt = w ? (int) u16(w + 0x6C) : 0;
            // The count word names the tab: 9 rows on General, 6 on
            // Multiplayer (no stable tab word survives the switch).
            const char* const* rows = nullptr;
            if (cnt == kCfgGeneralCount) rows = kCfgGeneralRows;
            else if (cnt == kCfgMultiCount) rows = kCfgMultiRows;
            if (rows && cur >= 0 && cur < cnt)
                snprintf(buf, n, "%s, %d of %d", rows[cur], cur + 1, cnt);
            else
                CampItemFallback(buf, n);
            return;
        }
        case CampScreen::GameHelp: {
            uint32_t obj = CampSubObj(kCampHelpObjTag);
            int cur = obj ? (int) u16(obj + 0x66) : -1;
            if (cur >= 0 && cur < kHelpCount)
                snprintf(buf, n, "%s, %d of %d", kHelpRows[cur],
                         cur + 1, kHelpCount);
            else
                CampItemFallback(buf, n);
            return;
        }
        case CampScreen::DecksSub:
            if (g_lastMain == 0 && g_lastSub >= 0 && g_lastSub < kDecksCount)
                snprintf(buf, n, "%s, %d of %d", kDecksItems[g_lastSub],
                         g_lastSub + 1, kDecksCount);
            else if (g_lastMain >= 0 && g_lastMain < kCampCount)
                snprintf(buf, n, "%s", kCampItems[g_lastMain]);
            else
                snprintf(buf, n, "Camp menu");
            return;
        case CampScreen::Editor:
            if (g_lastGroup >= 0 && g_lastGroup < kGroupCount)
                snprintf(buf, n, "%s", kGroupNames[g_lastGroup]);
            else
                snprintf(buf, n, "Edit Deck");
            return;
        case CampScreen::SlotList: {
            uint32_t slotW = CampSlotWidget();
            int cnt = slotW ? (int) u16(slotW + 0x6A) : 0;
            if (cnt >= 1 && cnt <= 16)
                snprintf(buf, n, "Slot %d of %d", g_lastSlot + 1, cnt);
            else
                snprintf(buf, n, "Slot %d", g_lastSlot + 1);
            return;
        }
        case CampScreen::Inventory: {
            char txt[192];
            if (InvRowText(g_lastInv, txt, sizeof(txt))) {
                snprintf(buf, n, "%s", txt);
                return;
            }
            snprintf(buf, n, "Row %d", g_lastInv + 1);
            return;
        }
        case CampScreen::MeldPanel: {
            char txt[192];
            if (MeldPanelRowText(g_meldRow, txt, sizeof(txt))) {
                snprintf(buf, n, "%s", txt);
                return;
            }
            snprintf(buf, n, "Slot %d", g_meldRow + 1);
            return;
        }
        case CampScreen::MeldList: {
            char txt[192];
            if (MeldListRowText(g_meldRow, txt, sizeof(txt))) {
                snprintf(buf, n, "%s", txt);
                return;
            }
            snprintf(buf, n, "Row %d", g_meldRow + 1);
            return;
        }
        default:
            snprintf(buf, n, "Camp menu");
            return;
    }
}

void CampWhere(char* buf, size_t n)
{
    char row[96];
    CampRowFor(g_campScreen, row, sizeof(row));
    switch (g_campScreen) {
        case CampScreen::Main:
            snprintf(buf, n, "Camp menu. %s.", row); return;
        case CampScreen::DecksSub:
            if (g_lastMain == 0)
                snprintf(buf, n, "Command Decks menu. %s.", row);
            else
                snprintf(buf, n, "Camp menu. %s.", row);
            return;
        case CampScreen::Editor:
            snprintf(buf, n, "Edit Deck. %s.", row); return;
        case CampScreen::SlotList:
            snprintf(buf, n, "Edit Deck. %s.", row); return;
        case CampScreen::Inventory:
            snprintf(buf, n, "Command list. %s.", row); return;
        case CampScreen::MeldPanel:
            if (g_meldDialog) {
                snprintf(buf, n, "Meld these commands? %s.",
                         g_meldDlgCursor == 1 ? "No" : "Yes");
                return;
            }
            snprintf(buf, n, "Meld Commands. %s.", row); return;
        case CampScreen::MeldList:
            if (g_meldSynth)
                snprintf(buf, n, "Meld Commands, Item. %s.", row);
            else if (g_meldEditSlot == 0 || g_meldEditSlot == 1)
                snprintf(buf, n, "Meld Commands, Slot %d. %s.",
                         g_meldEditSlot + 1, row);
            else
                snprintf(buf, n, "Meld Commands. %s.", row);
            return;
        case CampScreen::Items:
            snprintf(buf, n, "Items. %s.", row); return;
        case CampScreen::Abilities:
            snprintf(buf, n, "Abilities. %s.", row); return;
        case CampScreen::DLinks:
            snprintf(buf, n, "D-Links. %s.", row); return;
        case CampScreen::Stats:
            snprintf(buf, n, "Stats. %s.", row); return;
        case CampScreen::Reports:
            snprintf(buf, n, "Reports. %s.", row); return;
        case CampScreen::Config:
            snprintf(buf, n, "Config. %s.", row); return;
        case CampScreen::GameHelp:
            snprintf(buf, n, "Game Help. %s.", row); return;
        default:
            snprintf(buf, n, "Camp menu."); return;
    }
}

uint32_t g_actorBase = 0;   // located player actor; 0 = not located
uint32_t g_lastActorBase = 0;   // last base where the actor verified live (game-over gate witness); 0 = none
uint64_t g_lastScanMs = 0;  // host time of the last locate sweep
int g_charId = -1;          // character id from the player block; -1 = unknown
int g_lastLock = -1;        // last polled lock state: -1 unknown, 0 off, 1 on
bool g_lowHpWarned = false; // low-health warning fired for this drop
uint32_t g_lowHpActor = 0;  // actor the low-HP latch belongs to
uint16_t g_lowHpMax = 0;    // max HP the low-HP latch belongs to
uint32_t g_lockRec = 0;     // located lock-on record address; 0 = not yet
uint64_t g_lastLockScanMs = 0;  // host time of the last lock-record sweep
uint32_t g_nodeBase = 0;    // located engagement component; 0 = not yet
uint32_t g_nodeActor = 0;   // actor the cached component belongs to
uint64_t g_lastNodeScanMs = 0;  // host time of the last component sweep
// Movement-derived facing (design: docs/reverse-engineering/
// kingdom-hearts-birth-by-sleep.md, "DIRECTIONS — state of the hunt").
// actor+0xAB8 is NOT a facing vector (measured: one constant diagonal
// per area), so facing comes from the player's own movement: the
// anchor is the last position a >= 1-unit leg started from, and the
// facing delta is that leg's displacement. Bearings to landmarks are
// spoken as clock positions relative to this delta. The first
// landmark table — the Mirage Arena Hub — speaks through the Guide
// query (CmdHubGuide below); other rooms' tables are still open
// work, and the tracker stays bench-visible via DumpState.
float g_faceAnchorX = 0.0f, g_faceAnchorZ = 0.0f;  // last leg's start
bool g_faceAnchorSet = false;
float g_faceDX = 0.0f, g_faceDZ = 0.0f;            // last >= 1-unit leg
bool g_faceValid = false;

void FaceReset(void)
{
    g_faceAnchorSet = false;
    g_faceValid = false;
}

// The normalized movement facing, when a >= 1-unit leg has been
// tracked. False (and the outputs untouched) when no leg has
// completed since the last reset — never a guessed direction.
bool FacingVector(float* dx, float* dz)
{
    if (!g_faceValid) return false;
    float len = sqrtf(g_faceDX * g_faceDX + g_faceDZ * g_faceDZ);
    if (!(len >= 1.0f)) return false;
    *dx = g_faceDX / len;
    *dz = g_faceDZ / len;
    return true;
}

// Find the save-side player block for the located actor and cache
// its character byte. Runs once per actor location; a miss leaves
// the character unknown (the field line just omits the name).
void ResolveCharacter(void)
{
    if (g_charId >= 0 || !g_actorBase) return;
    uint16_t maxHp = u16(g_actorBase + kActorOffMaxHp);
    uint32_t found = 0;
    for (uint32_t base = 0x08800000; base + 0x28800 <= 0x0A000000; base += 0x100000) {
        if (u16(base + kBlockOffMaxHp) != maxHp) continue;
        uint16_t level = u16(base + kBlockOffLevel);
        if (level < 1 || level > 99) continue;
        if (u32(base + kBlockOffMunny) > 999999) continue;
        if (u8(base + kBlockOffChar) >= (uint8_t) kCharKnown) continue;
        if (found) return;  // ambiguous: two blocks claim this actor
        found = base;
    }
    if (found) g_charId = (int) u8(found + kBlockOffChar);
}

bool ActorPlausible(uint32_t base)
{
    // The spawn fingerprints at one candidate base (see the section
    // header). Each one alone matches random heap data somewhere;
    // together they matched exactly the player actor in every dump.
    uint16_t maxHp = u16(base + kActorOffMaxHp);
    uint16_t curHp = u16(base + kActorOffCurHp);
    if (maxHp < 15 || maxHp > 999 || curHp == 0 || curHp > maxHp) return false;
    // The spawn mark reads 1 normally and 2 while the player is
    // locked on to a target (VERIFIED 2026-10-18, battle30/31 toggle
    // differential: 1 in every unlocked dump, 2 in both locked
    // dumps, back to 1 after unlocking). Accept both; requiring 1
    // made the field vanish for the whole lock-on.
    uint16_t mark = u16(base + kActorOffMark);
    if (mark != 1 && mark != 2) return false;
    if ((u32(base + kActorOffFlags) & 0x20) == 0) return false;
    uint32_t state = u32(base + kActorOffState);
    // The state word is a small enum, but its values are per-module:
    // 1 in the tutorial field and the Mirage Arena hub, 12 in the
    // Final Episode field, 15 while dead, 28 during a respawn
    // transition (all VERIFIED 2026-10-18, battle26/27). The old
    // 1..3 test missed the FE actor entirely, so accept any small
    // nonzero value; the other fingerprints plus the unique-hit rule
    // in LocateActor() carry the discrimination (validated against
    // nine reference dumps: unique hit in every field dump, zero on
    // both title screens).
    if (state < 1 || state > 255) return false;
    uint32_t ptr = u32(base + kActorOffPtr);
    if (ptr < 0x08800000 || ptr >= 0x0A000000) return false;
    if (u16(base + kActorOffGauge) > 1000) return false;
    return true;
}

// Sweep the heap for the player actor. Only a UNIQUE hit is
// accepted: a tie means some layout this build doesn't know, and
// failing closed beats speaking from the wrong struct.
uint32_t LocateActor(void)
{
    uint32_t found = 0;
    for (uint32_t base = 0x09800000; base + 0x400 <= 0x0A000000; base += 4) {
        if ((u32(base + kActorOffFlags) & 0x20) == 0) continue;
        if (!ActorPlausible(base)) continue;
        if (found) return 0;  // second hit: ambiguous, trust neither
        found = base;
    }
    return found;
}

bool InField(void)
{
    // The world map is not the field (VERIFIED 2026-10-24,
    // bench wm1): both map-id copies hold world 0x11, room 1
    // for the whole map session — flight, the visit dialog and
    // the map's own pause menu alike — but the map's
    // glider-side actor passes ActorPlausible, so without this
    // exclusion the field speech claims the map: the low-HP
    // warning fired once there on a 1-HP save (the game-over
    // work's side-finding). Excluding here also keeps the map
    // from clobbering g_lastActorBase, which the game-over
    // gate's corpse witness needs across a Continue (death ->
    // world map -> visit).
    if (u8(kWorldAddrA) == 0x11 && u8(kWorldAddrA + 1) == 1 &&
        u8(kWorldAddrB) == 0x11 && u8(kWorldAddrB + 1) == 1)
        return false;
    if (g_actorBase && ActorPlausible(g_actorBase)) {
        g_lastActorBase = g_actorBase;   // the game-over gate's witness base
        ResolveCharacter();
        return true;
    }
    g_actorBase = 0;
    g_charId = -1;
    // No map loaded: both map-id copies read 0xFF on the title and the
    // difficulty screens, so no field actor can exist. Skip the heap sweep
    // there instead of walking 8 MB every few seconds for nothing.
    if (u8(kWorldAddrA) == 0xFF && u8(kWorldAddrB) == 0xFF) return false;
    // Not located: sweep, but at most once every few seconds —
    // ready() is polled per frame and the sweep walks the heap.
    uint64_t now = g_host ? g_host->now_ms : 0;
    if (g_lastScanMs && now >= g_lastScanMs && now - g_lastScanMs < 3000)
        return false;
    g_lastScanMs = now;
    g_actorBase = LocateActor();
    if (g_actorBase) {
        g_lastActorBase = g_actorBase;   // the game-over gate's witness base
        ResolveCharacter();
    }
    return g_actorBase != 0;
}

bool ActorIsTutorial(void) { return g_actorBase == kTutorialActorBase; }

int g_lastDeck = -1;  // last deck index spoken; -1 = none yet

void DeckName(uint32_t idx, char* buf, size_t n)
{
    if (idx < (uint32_t) kDeckKnown)
        snprintf(buf, n, "%s, %d of %d", kDeckItems[idx], (int) idx + 1, kDeckKnown);
    else
        snprintf(buf, n, "Command %d", (int) idx + 1);
}

// Locate the lock-on record for the current actor. The record's
// own address floats per boot, but it points back at the actor;
// fifteen words in RAM do that at the same offset, so the hit
// must also tie out end to end: the record's target id equals the
// actor's engagement id, the target's entity block carries a
// sane HP pair, and both position triples are finite. Unique
// hit only — anything less fails closed (battle41 dumps: exactly
// one of the fifteen back-pointer candidates passes all checks).
uint32_t LocateLockRecord(void)
{
    uint32_t eng = u32(g_actorBase + kActorOffEngage);
    if (!eng) return 0;
    uint32_t found = 0;
    int hits = 0;
    for (uint32_t x = 0x09800000; x + 0x58 < kUserRamEnd; x += 4) {
        if (u32(x + kLockRecOffActor) != g_actorBase) continue;
        uint32_t tgt = u32(x + kLockRecOffTgt);
        if (tgt < kUserRamBase || tgt >= kUserRamEnd) continue;
        if (u32(tgt) != eng) continue;
        uint32_t blk = tgt - kFoeBlockBack;
        unsigned mx = (unsigned) u16(blk + kActorOffMaxHp);
        unsigned cur = (unsigned) u16(blk + kActorOffCurHp);
        if (mx < 10 || mx > 20000 || cur > mx) continue;
        bool sane = true;
        for (int i = 0; i < 6; ++i) {
            float v = f32(x + kLockRecOffPPos + (uint32_t) i * 4);
            if (!(v >= -300.0f && v <= 300.0f)) sane = false;
        }
        if (!sane) continue;
        ++hits;
        found = x;
    }
    return hits == 1 ? found : 0;
}

// Validated read of the locked target: its health pair, the 3D
// distance, and the height difference (target minus player).
// Every input is re-validated on each call; any failure returns
// false and the caller degrades honestly. (The target's NAME is
// not read yet — the scene id is not proven to be a type id.)
bool ReadFoe(unsigned* curOut, unsigned* mxOut, float* distOut, float* dyOut)
{
    if (!g_lockRec || u32(g_lockRec + kLockRecOffActor) != g_actorBase) {
        // The record sweep is 2M reads. ReadFoe() runs inside the per-frame
        // cue snapshot, so an unlocatable record must NOT be re-swept every
        // frame: throttle it like the player-node locate does.
        uint64_t now = g_host ? g_host->now_ms : 0;
        if (g_lastLockScanMs && now >= g_lastLockScanMs &&
            now - g_lastLockScanMs < 1000)
            return false;
        g_lastLockScanMs = now;
        g_lockRec = LocateLockRecord();
    }
    if (!g_lockRec) return false;
    uint32_t tgt = u32(g_lockRec + kLockRecOffTgt);
    if (!tgt) return false;
    if (u32(tgt) != u32(g_actorBase + kActorOffEngage)) return false;
    uint32_t blk = tgt - kFoeBlockBack;
    unsigned cur = (unsigned) u16(blk + kActorOffCurHp);
    unsigned mx = (unsigned) u16(blk + kActorOffMaxHp);
    if (mx < 10 || mx > 20000 || cur > mx) return false;
    float px = f32(g_lockRec + kLockRecOffPPos), pz = f32(g_lockRec + kLockRecOffPPos + 8);
    float tx = f32(g_lockRec + kLockRecOffTPos), tz = f32(g_lockRec + kLockRecOffTPos + 8);
    float py = f32(g_lockRec + kLockRecOffPPos + 4), ty = f32(g_lockRec + kLockRecOffTPos + 4);
    if (!(px >= -300.0f && px <= 300.0f && pz >= -300.0f && pz <= 300.0f &&
          tx >= -300.0f && tx <= 300.0f && tz >= -300.0f && tz <= 300.0f))
        return false;
    float dx = tx - px, dy = ty - py, dz = tz - pz;
    *curOut = cur;
    *mxOut = mx;
    *distOut = sqrtf(dx * dx + dy * dy + dz * dz);
    *dyOut = dy;
    return true;
}

// ---- player position (VERIFIED 2026-10-19) ----------------------------------
// The lock-on record is one state of the player's engagement
// component; the same component exists in EVERY field area,
// combat or not, its target word 0 while nothing is engaged.
// Identity: static descriptor words at +0x04 = 0x08B73DD0
// and +0x1C = 0x08B73DB4, a STATE descriptor at +0x0C
// (0x08B73DE0 while unlocked, 0x08B73E10 while locked on —
// correlated with the actor's spawn mark across 12 dumps,
// battle27/30/41), plus the owner back-pointer at +0x38;
// the player's position floats ride at +0x40 (the lock
// record's player-position slot). Crowned
// by movement differentials in the Mirage Arena hub (nav1 /
// nav2 benches): the triple froze through an idle control,
// tracked two walk legs smoothly, and stopped dead at the
// railing while the stick was still held. Census over five
// dumps (hub field, hub camp, combat unlocked, combat
// locked, tutorial): exactly one descriptor+owner hit each;
// two tutorial look-alikes whose +0x38 also names the actor
// carry none of the descriptor words.
constexpr uint32_t kNodeDescA = 0x08B73DD0;
constexpr uint32_t kNodeDescBOff = 0x08B73DE0;  // state descriptor: unlocked
constexpr uint32_t kNodeDescBOn = 0x08B73E10;   // state descriptor: locked on
constexpr uint32_t kNodeDescC = 0x08B73DB4;

bool NodePlausible(uint32_t x)
{
    uint32_t descB = u32(x + 0x0C);
    if (u32(x + 4) != kNodeDescA ||
        (descB != kNodeDescBOff && descB != kNodeDescBOn) ||
        u32(x + 0x1C) != kNodeDescC)
        return false;
    if (u32(x + kLockRecOffActor) != g_actorBase) return false;
    float px = f32(x + kLockRecOffPPos), py = f32(x + kLockRecOffPPos + 4),
          pz = f32(x + kLockRecOffPPos + 8);
    if (!(px >= -400.0f && px <= 400.0f && py >= -400.0f && py <= 400.0f &&
          pz >= -400.0f && pz <= 400.0f))
        return false;
    if (px == 0.0f && py == 0.0f && pz == 0.0f) return false;
    return true;
}

uint32_t LocatePlayerNode(void)
{
    uint32_t found = 0;
    for (uint32_t x = 0x09800000; x + 0x58 < kUserRamEnd; x += 4) {
        if (u32(x + kLockRecOffActor) != g_actorBase) continue;
        if (!NodePlausible(x)) continue;
        if (found) return 0;  // ambiguous: trust neither
        found = x;
    }
    return found;
}

// Validate the cached engagement component, re-locating it
// (throttled) when the cache is empty, stale, or implausible.
bool EnsureNode(void)
{
    if (!g_nodeBase || g_nodeActor != g_actorBase ||
        !NodePlausible(g_nodeBase)) {
        g_nodeBase = 0;
        uint64_t now = g_host ? g_host->now_ms : 0;
        if (g_lastNodeScanMs && now >= g_lastNodeScanMs &&
            now - g_lastNodeScanMs < 3000)
            return false;
        g_lastNodeScanMs = now;
        g_nodeBase = LocatePlayerNode();
        g_nodeActor = g_actorBase;
        if (!g_nodeBase) return false;
    }
    return true;
}

// "Position 0, -43. " — the player's ground-plane coordinates
// in the room's own frame (x, z): the raw material for
// surveying a room's exits and landmarks, and for judging
// movement between two asks. Silent when the component
// cannot be located uniquely.
bool PositionClause(char* buf, size_t n)
{
    if (!EnsureNode()) return false;
    float px = f32(g_nodeBase + kLockRecOffPPos);
    float pz = f32(g_nodeBase + kLockRecOffPPos + 8);
    int ix = (int) (px + (px >= 0 ? 0.5f : -0.5f));
    int iz = (int) (pz + (pz >= 0 ? 0.5f : -0.5f));
    snprintf(buf, n, "Position %d, %d. ", ix, iz);
    return true;
}

// Per-frame facing tracking on the same located component the
// position clause reads. Cheap once the component is cached: two
// float reads a frame; the locate sweep stays throttled inside
// EnsureNode. A component change (new area, new actor) restarts
// the anchor — a leg is only ever measured within one component.
uint32_t g_faceNode = 0;  // component the facing anchor belongs to

void FaceTrack(void)
{
    if (!EnsureNode()) return;
    if (g_faceNode != g_nodeBase) { FaceReset(); g_faceNode = g_nodeBase; }
    float px = f32(g_nodeBase + kLockRecOffPPos);
    float pz = f32(g_nodeBase + kLockRecOffPPos + 8);
    if (!g_faceAnchorSet) {
        g_faceAnchorX = px; g_faceAnchorZ = pz; g_faceAnchorSet = true;
        return;
    }
    float dx = px - g_faceAnchorX, dz = pz - g_faceAnchorZ;
    if (dx * dx + dz * dz >= 1.0f) {
        g_faceDX = dx; g_faceDZ = dz; g_faceValid = true;
        g_faceAnchorX = px; g_faceAnchorZ = pz;
    }
}

// ---- Mirage Arena Hub landmarks (table VERIFIED 2026-10-22) ------------------
// The Hub (world 15, room 1) is two floors that are NOT walkably
// connected: the entrance platform (spawn, save point, transporter
// ring) and the main lobby (arena terminal, Moogle Medal Shop,
// Log In terminal, return ring), joined only by the transporter. The positions are
// the room's own spawn data (.olo in the VS01 map arc), cross-
// checked against the bench's walked survey and two interaction
// proofs (games/kh-bbs/hub-landmarks.md): the save point matched
// the fixture's stand position to 0.01, both rings were used in
// both directions, and the save point's menu is the world-map
// exit. Only the current floor's landmarks are spoken: naming the
// other floor's would send the player walking at a rim with no
// exit on it. The floor split is z: the platform is the disc
// around z = -40, the lobby is around the origin, and -20 sits in
// the unwalkable gap between them.
// Bearings are clock positions relative to the movement-derived
// facing (the directions design in the RE doc): 12 is straight
// ahead, 3 is to the right — DBZ: Another Road's field form. No
// facing yet (the player has not moved a full unit since arriving
// in the room) means distance only, never a guessed direction.
struct HubLandmark { const char* name; float x; float z; };
static const HubLandmark kHubPlatformMarks[] = {
    {"Save point", 0.01f, -43.00f},
    {"Transporter", 0.04f, -37.00f},
};
static const HubLandmark kHubLobbyMarks[] = {
    {"Arena terminal", 0.00f, 0.00f},
    {"Moogle Medal Shop", 4.68f, 9.01f},
    {"Log In terminal", 0.00f, 8.20f},
    {"Transporter", 0.00f, -9.00f},
};
constexpr float kHubFloorSplitZ = -20.0f;

// The Guide query in the Hub: speak this floor's landmarks, each
// with its distance and, once facing is known, its clock bearing.
// FAIL SILENT everywhere else — the query has no answer outside
// world 15 room 1, the camp menu owns speech while it is open,
// and an unlocatable position says nothing rather than guessing.
void CmdHubGuide(void)
{
    if (CampActive() || ReportsUp()) return;
    unsigned wA = u8(kWorldAddrA), rA = u8(kWorldAddrA + 1);
    unsigned wB = u8(kWorldAddrB), rB = u8(kWorldAddrB + 1);
    if (wA != 15 || rA != 1 || wA != wB || rA != rB) return;
    if (!InField() || !EnsureNode()) return;
    float px = f32(g_nodeBase + kLockRecOffPPos);
    float pz = f32(g_nodeBase + kLockRecOffPPos + 8);
    const HubLandmark* marks = kHubLobbyMarks;
    int count = (int) (sizeof(kHubLobbyMarks) / sizeof(kHubLobbyMarks[0]));
    if (pz < kHubFloorSplitZ) {
        marks = kHubPlatformMarks;
        count = (int) (sizeof(kHubPlatformMarks) / sizeof(kHubPlatformMarks[0]));
    }
    float fdx = 0.0f, fdz = 0.0f;
    bool facing = FacingVector(&fdx, &fdz);
    for (int i = 0; i < count; i++) {
        float dx = marks[i].x - px, dz = marks[i].z - pz;
        float dist = sqrtf(dx * dx + dz * dz);
        char line[128];
        if (dist < 1.5f) {
            snprintf(line, sizeof(line), "%s, right here.", marks[i].name);
        } else if (!facing) {
            snprintf(line, sizeof(line), "%s, %u away.", marks[i].name,
                     (unsigned) (dist + 0.5f));
        } else {
            // Signed angle from the facing leg to the landmark,
            // in degrees: 0 ahead, + to the right (the y-component
            // of facing x target, which the hub survey's frame —
            // x east, z north — makes positive to the right).
            float inv = 1.0f / dist;
            float sinA = fdz * (dx * inv) - fdx * (dz * inv);
            float cosA = fdx * (dx * inv) + fdz * (dz * inv);
            float rel = atan2f(sinA, cosA) * 57.2957795f;
            if (rel < 0.0f) rel += 360.0f;
            int clock = (int) (rel / 30.0f + 0.5f);
            if (clock == 0) clock = 12;
            if (clock == 12)
                snprintf(line, sizeof(line), "%s, %u away, straight ahead.",
                         marks[i].name, (unsigned) (dist + 0.5f));
            else
                snprintf(line, sizeof(line), "%s, %u away, %d o'clock.",
                         marks[i].name, (unsigned) (dist + 0.5f), clock);
        }
        Say(line, "guide", oga::Priority::High);
    }
}

// The spoken foe clause for a held lock: the target's health and
// distance. Any validation failure leaves the plain suffix.
bool LockClause(char* buf, size_t n)
{
    unsigned cur, mx;
    float dist, dy;
    if (!ReadFoe(&cur, &mx, &dist, &dy)) return false;
    unsigned units = (unsigned) (dist + 0.5f);
    snprintf(buf, n, " Locked on. Enemy health %u of %u, %u unit%s away.",
             cur, mx, units, units == 1 ? "" : "s");
    return true;
}

// Room names from the OpenKH BBS worlds dictionary
// (openkh.dev/bbs/dictionary/worlds.html), matched against
// live RAM: (1,5) Mountain Path = the tutorial map,
// (6,13) Central Square = the Radiant Garden plaza,
// (15,1) Hub = the Mirage Arena hub (the camp screen
// itself labels that spot "Hub").
struct RoomName { uint8_t world; uint8_t room; const char* name; };
static const RoomName kRoomNames[] = {
    { 1, 0, "The Land of Departure" },
    { 1, 1, "Forecourt" },
    { 1, 2, "Great Hall" },
    { 1, 3, "Ventus's Room" },
    { 1, 4, "Ventus's Room" },
    { 1, 5, "Mountain Path" },
    { 1, 6, "Summit" },
    { 1, 7, "Forecourt" },
    { 1, 8, "Forecourt" },
    { 1, 9, "Great Hall" },
    { 1, 10, "Ruins" },
    { 1, 11, "Chamber of Waking" },
    { 1, 12, "Castle Oblivion" },
    { 1, 13, "Character Selection" },
    { 1, 14, "Forecourt" },
    { 1, 15, "Ruins" },
    { 1, 16, "Mountain Path" },
    { 2, 0, "Dwarf Woodlands" },
    { 2, 1, "Mine Entrance" },
    { 2, 2, "The Mine" },
    { 2, 3, "Vault" },
    { 2, 4, "Magic Mirror Chamber" },
    { 2, 5, "Underground Waterway" },
    { 2, 6, "Courtyard" },
    { 2, 7, "Flower Glade" },
    { 2, 8, "Deep Woods" },
    { 2, 9, "Inside the Mirror" },
    { 2, 10, "Cottage Clearing" },
    { 2, 11, "The Cottage" },
    { 2, 12, "Mountain Trail" },
    { 3, 0, "Castle of Dreams" },
    { 3, 1, "Cinderella's Room" },
    { 3, 2, "Mousehole" },
    { 3, 3, "Wardrobe Room" },
    { 3, 4, "Entrance" },
    { 3, 5, "The Chateau" },
    { 3, 6, "Forest" },
    { 3, 7, "Palace Courtyard" },
    { 3, 8, "Corridor" },
    { 3, 9, "Ballroom" },
    { 3, 10, "Foyer" },
    { 3, 11, "Passage" },
    { 3, 12, "Antechamber" },
    { 3, 13, "Wardrobe Room" },
    { 3, 14, "Mousehole" },
    { 3, 15, "Wardrobe Room" },
    { 4, 0, "Enchanted Dominion" },
    { 4, 1, "Dungeon Cell" },
    { 4, 2, "Gates" },
    { 4, 3, "Maleficent's Throne" },
    { 4, 4, "Dungeon" },
    { 4, 5, "Hall" },
    { 4, 6, "Forbidden Mountain" },
    { 4, 7, "Waterside" },
    { 4, 8, "Forest Clearing" },
    { 4, 9, "Bridge" },
    { 4, 10, "Bridge" },
    { 4, 11, "Audience Chamber" },
    { 4, 12, "Audience Chamber" },
    { 4, 14, "Hallway" },
    { 4, 15, "Aurora's Chamber" },
    { 4, 16, "Tower Room" },
    { 4, 17, "Hall" },
    { 4, 18, "Aurora's Chamber" },
    { 4, 19, "Hall" },
    { 4, 20, "Hall" },
    { 4, 21, "Hall" },
    { 4, 22, "Hall" },
    { 4, 23, "Hall" },
    { 4, 24, "Hall" },
    { 4, 25, "Hall" },
    { 4, 26, "Hall" },
    { 4, 27, "Hall" },
    { 4, 28, "Hall" },
    { 4, 29, "Hall" },
    { 4, 30, "Hall" },
    { 4, 31, "Hall" },
    { 4, 32, "Hall" },
    { 4, 33, "Hall" },
    { 4, 34, "Hall" },
    { 4, 35, "Hall" },
    { 4, 36, "Hall" },
    { 4, 37, "Hall" },
    { 4, 38, "Hall" },
    { 4, 39, "Gates" },
    { 5, 0, "The Mysterious Tower" },
    { 5, 1, "Sorcerer's Chamber" },
    { 5, 2, "Mysterious Tower" },
    { 5, 3, "Entrance" },
    { 5, 4, "Sorcerer's Chamber" },
    { 6, 0, "Radiant Garden" },
    { 6, 1, "Outer Garden" },
    { 6, 2, "Entryway" },
    { 6, 3, "Central Square" },
    { 6, 4, "Aqueduct" },
    { 6, 5, "Castle Town" },
    { 6, 6, "Reactor" },
    { 6, 7, "Fountain Court" },
    { 6, 8, "Merlin's House" },
    { 6, 9, "Gardens" },
    { 6, 10, "Front Doors" },
    { 6, 11, "Purification Facility" },
    { 6, 12, "Outer Gardens" },
    { 6, 13, "Central Square" },
    { 6, 14, "Central Square" },
    { 7, 1, "Louie's Ruins" },
    { 7, 2, "Court" },
    { 7, 3, "Path Crossroads" },
    { 7, 4, "UG Ruins Entrance" },
    { 7, 5, "UG Ruins Passage 1" },
    { 7, 6, "UG Ruins Passage 2" },
    { 7, 7, "UG Courtyard" },
    { 7, 8, "Jungle Near Ruins" },
    { 7, 9, "Eminence" },
    { 7, 10, "Jungle" },
    { 7, 11, "Man-Village River" },
    { 7, 12, "Bog" },
    { 7, 20, "Realm of Darkness: Middle Zone" },
    { 7, 21, "Realm of Darkness: Lower Zone" },
    { 7, 22, "Realm of Darkness: Upper Zone" },
    { 7, 23, "The Dark Margin" },
    { 7, 24, "Cinderella's Castle (FM)" },
    { 7, 30, "Destiny Islands (Noon)" },
    { 7, 31, "Traverse Town 2nd District" },
    { 7, 33, "Text only present in HD ReMiX" },
    { 7, 34, "Text only present in HD ReMiX" },
    { 7, 35, "Traverse Town 3rd District" },
    { 7, 50, "Lanes Between" },
    { 7, 52, "Realm of Darkness" },
    { 7, 53, "FOR wm EVENT" },
    { 7, 60, "Planner Test" },
    { 7, 61, "pi Amusement Park" },
    { 7, 62, "tm_Opera" },
    { 7, 63, "nd_Notredame 1st Floor" },
    { 7, 64, "nd_Notredame Quasimodo's Room" },
    { 7, 65, "Traverse Town District 4" },
    { 7, 66, "tp_01: The Legacy's Deck" },
    { 7, 67, "tl_the_city_Main Road" },
    { 7, 68, "Reserved" },
    { 7, 69, "Reserved" },
    { 7, 70, "Reserved" },
    { 7, 71, "Reserved" },
    { 7, 72, "Reserved" },
    { 7, 73, "Reserved" },
    { 7, 74, "Reserved" },
    { 7, 75, "Reserved" },
    { 7, 76, "Reserved" },
    { 7, 77, "Reserved" },
    { 7, 78, "Reserved" },
    { 7, 79, "Reserved" },
    { 7, 80, "Reserved" },
    { 7, 81, "Reserved" },
    { 7, 82, "Reserved" },
    { 7, 83, "Reserved" },
    { 7, 84, "Reserved" },
    { 7, 85, "Reserved" },
    { 8, 0, "Olympus Coliseum" },
    { 8, 1, "Coliseum Gates" },
    { 8, 2, "Vestibule" },
    { 8, 3, "West Bracket" },
    { 8, 4, "East Bracket" },
    { 8, 5, "Town Near Thebes" },
    { 8, 6, "East Bracket" },
    { 9, 0, "Deep Space" },
    { 9, 1, "Prison Block" },
    { 9, 2, "Turo Transporter" },
    { 9, 3, "Durgon Transporter" },
    { 9, 4, "Ship Corridor" },
    { 9, 5, "Control Room" },
    { 9, 6, "Containment Pod" },
    { 9, 7, "Ship Hub" },
    { 9, 8, "Machinery Bay" },
    { 9, 9, "Launch Deck" },
    { 9, 10, "Ship Exterior" },
    { 9, 11, "Outer Space" },
    { 9, 12, "Ship Corridor" },
    { 9, 13, "Lanes Between" },
    { 9, 14, "Bay Access" },
    { 10, 0, "Destiny Islands" },
    { 10, 1, "Island Beach" },
    { 10, 2, "Island Beach" },
    { 10, 3, "Island Beach" },
    { 10, 4, "Main Island Beach" },
    { 11, 0, "Neverland" },
    { 11, 1, "Cove" },
    { 11, 2, "Cliff" },
    { 11, 3, "Mermaid Lagoon" },
    { 11, 4, "Seacoast" },
    { 11, 5, "Jungle Clearing" },
    { 11, 6, "Peter's Hideout" },
    { 11, 7, "Gully" },
    { 11, 8, "Indian Camp" },
    { 11, 9, "Rainbow Falls: Base" },
    { 11, 10, "Rainbow Falls: Ascent" },
    { 11, 11, "Rainbow Falls: Crest" },
    { 11, 12, "Skull Rock: Entrance" },
    { 11, 13, "Skull Rock: Cavern" },
    { 11, 14, "Night Sky" },
    { 12, 0, "Disney Town" },
    { 12, 1, "Library" },
    { 12, 2, "Main Plaza" },
    { 12, 3, "Fruitball Court" },
    { 12, 4, "Racecourse A" },
    { 12, 5, "Raceway" },
    { 12, 6, "Gizmo Gallery" },
    { 12, 7, "Pete's Rec Room" },
    { 12, 8, "Racecourse B" },
    { 12, 9, "Racecourse C" },
    { 12, 10, "Racecourse D" },
    { 12, 11, "Lanes Between" },
    { 12, 12, "Raceway Registration" },
    { 12, 13, "Ice Cream" },
    { 12, 14, "Fruitball" },
    { 12, 15, "Race: Castle Course" },
    { 13, 0, "Keyblade Graveyard" },
    { 13, 1, "Badlands" },
    { 13, 2, "Seat of War" },
    { 13, 3, "Twister Trench" },
    { 13, 4, "Eye of the Storm" },
    { 13, 5, "Eye of the Storm" },
    { 13, 6, "Eye of the Storm" },
    { 13, 7, "Fissure" },
    { 13, 8, "Keyblade Graveyard" },
    { 13, 9, "Keyblade Graveyard" },
    { 13, 10, "Keyblade Graveyard" },
    { 13, 11, "Will's Cage" },
    { 13, 12, "Keyblade Graveyard" },
    { 13, 50, "Ventus's Mind" },
    { 13, 51, "Ventus's Mind" },
    { 13, 52, "Ventus's Mind" },
    { 13, 53, "Sora's Mind" },
    { 13, 54, "Terra-Xehanort's Mind" },
    { 13, 55, "Keyblade Graveyard" },
    { 13, 56, "Badlands" },
    { 15, 0, "Mirage Arena" },
    { 15, 1, "Hub" },
    { 15, 2, "Coliseum" },
    { 15, 3, "Arena" },
    { 15, 4, "Badlands" },
    { 15, 5, "Pinball" },
    { 15, 6, "Ship Hub" },
    { 15, 7, "Mousehole" },
    { 15, 8, "Forest" },
    { 15, 9, "Skull Rock" },
    { 15, 10, "Audience Chamber" },
    { 15, 11, "Forecourt" },
    { 15, 12, "Summit" },
    { 15, 13, "Launch Deck" },
    { 15, 14, "Ship Exterior" },
    { 15, 15, "Arena" },
    { 15, 16, "Great Hall (Noon)" },
    { 15, 17, "Monstro (Interior A)" },
    { 15, 18, "Monstro (Exterior)" },
    { 15, 19, "Monstro (Interior B)" },
    { 15, 20, "Summit(Broken Environment)" },
    { 16, 0, "Command Board" },
    { 16, 1, "Land of Departure BG" },
    { 16, 3, "Cinderella BG" },
    { 16, 9, "Lilo & Stitch BG" },
    { 16, 11, "Peter Pan BG" },
    { 16, 12, "Disney Castle BG" },
    { 16, 18, "Winnie the Pooh BG" },
    { 16, 19, "Peter Pan BG" },
    { 17, 0, "World Map" },
    { 18, 0, "The Hundred Acre Wood" },
    { 23, 0, "Jump Festa (sw10)" },
    { 23, 1, "Jump Festa (cd07)" },
    { 23, 2, "Jump Festa (cd09)" },
    { 23, 3, "Jump Festa (sb10" },
    { 23, 10, "The Town" },
    { 23, 11, "Polygon Partition Test" },
    { 23, 20, "Notredame (Exterior)" },
    { 23, 21, "Notredame (Interior)" },
    { 23, 70, "tt27" },
    { 23, 71, "dc01" },
    { 23, 72, "Battle Dice Test 1" },
    { 23, 73, "Battle Dice Test 2" },
    { 23, 74, "Battle Dice Test 3" },
    { 23, 75, "Battle Dice Test 4" },
    { 23, 90, "Operations Test" },
    { 23, 91, "Camera Collision Test" },
    { 23, 92, "Planner Test" },
    { 23, 93, "Grid" },
    { 23, 94, "Swimming Test" },
    { 23, 95, "Effect Test" },
    { 23, 96, "Placement Test 1" },
    { 23, 97, "Placement Test 2" },
};

const char* RoomNameFor(unsigned w, unsigned r)
{
    for (size_t i = 0; i < sizeof(kRoomNames) / sizeof(kRoomNames[0]); i++)
        if (kRoomNames[i].world == w && kRoomNames[i].room == r)
            return kRoomNames[i].name;
    return nullptr;
}

// "Radiant Garden, Central Square. " — the loaded map's name,
// written into buf with a trailing space for the field line
// to follow. Speaks only what is known: an unnamed room
// leaves the world alone, an events-only world leaves the
// room alone, and no agreement between the game's two
// copies (or the 0xFF no-map sentinel) says nothing at all.
bool LocationClause(char* buf, size_t n)
{
    unsigned wA = u8(kWorldAddrA), rA = u8(kWorldAddrA + 1);
    unsigned wB = u8(kWorldAddrB), rB = u8(kWorldAddrB + 1);
    if (wA == 0xFF || wA != wB || rA != rB) return false;
    const char* wn = wA < (unsigned) kWorldKnown ? kWorldNames[wA] : nullptr;
    const char* rn = RoomNameFor(wA, rA);
    if (wn && rn && strcmp(wn, rn) == 0) rn = nullptr;  // "World Map, World Map."
    if (wn && rn) { snprintf(buf, n, "%s, %s. ", wn, rn); return true; }
    if (wn) { snprintf(buf, n, "%s. ", wn); return true; }
    if (rn) { snprintf(buf, n, "%s. ", rn); return true; }
    return false;
}

void FieldLine(char* buf, size_t n)
{
    unsigned cur = (unsigned) u16(g_actorBase + kActorOffCurHp);
    unsigned max = (unsigned) u16(g_actorBase + kActorOffMaxHp);
    unsigned gauge = (unsigned) u16(g_actorBase + kActorOffGauge);
    // The character name comes from the player block (all lineages);
    // only the deck cursor word is tutorial-lineage-only.
    const char* name = (g_charId >= 0 && g_charId < kCharKnown)
                           ? kCharNames[g_charId] : nullptr;
    char lockBuf[96] = "";
    if (u16(g_actorBase + kActorOffMark) == 2 &&
        !LockClause(lockBuf, sizeof(lockBuf)))
        snprintf(lockBuf, sizeof(lockBuf), " Locked on.");
    const char* lockSuffix = lockBuf;
    if (!ActorIsTutorial()) {
        // Relocated actor: health and gauge are struct-relative and
        // proven; add the name when the player block identified the
        // character, but never the tutorial lineage's deck facts.
        if (name)
            snprintf(buf, n, "%s, health %u of %u. Gauge %u.%s", name, cur, max, gauge, lockSuffix);
        else
            snprintf(buf, n, "Health %u of %u. Gauge %u.%s", cur, max, gauge, lockSuffix);
        return;
    }
    char cmd[64];
    uint32_t idx = u32(kDeckCursorAddr);
    if (idx < (uint32_t) kDeckKnown)
        snprintf(cmd, sizeof(cmd), "%s", kDeckItems[idx]);
    else
        snprintf(cmd, sizeof(cmd), "command %d", (int) idx + 1);
    snprintf(buf, n, "%s, health %u of %u. Command: %s. Gauge %u.%s",
             name ? name : "Ventus", cur, max, cmd, gauge, lockSuffix);
}

// Foe query — the Dissidia battle mapping (NextAlly in battle
// speaks the opponent; here the opponent is the locked-on target).
// Speech carries health, distance and height. Horizontal
// direction is NOT spoken: the lock-on camera already faces the
// target, and the mods survey's rule puts direction in the cue
// layer, state in speech. Unlocked, the query refuses honestly.
void CmdFoe(void)
{
    if (u16(g_actorBase + kActorOffMark) != 2) {
        Say("Not locked on.", "foe", oga::Priority::High);
        return;
    }
    unsigned cur, mx;
    float dist, dy;
    if (!ReadFoe(&cur, &mx, &dist, &dy)) {
        Say("Lock lost.", "foe", oga::Priority::High);
        return;
    }
    const char* vert = dy > 5.0f ? ", above you" : dy < -5.0f ? ", below you" : "";
    char line[192];
    snprintf(line, sizeof(line), "Enemy health %u of %u. %u away%s.",
             cur, mx, (unsigned) (dist + 0.5f), vert);
    Say(line, "foe", oga::Priority::High);
}

// Lock query — the other half of the Dissidia battle mapping
// (NextEnemy speaks the lock state). Kept short; the foe's
// details are the foe query's job.
void CmdLockQuery(void)
{
    if (u16(g_actorBase + kActorOffMark) != 2) {
        Say("Lock off.", "lock", oga::Priority::High);
        return;
    }
    unsigned cur, mx;
    float dist, dy;
    if (!ReadFoe(&cur, &mx, &dist, &dy)) {
        Say("Lock lost.", "lock", oga::Priority::High);
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "Locked on. %u away.", (unsigned) (dist + 0.5f));
    Say(line, "lock", oga::Priority::High);
}

// Self readout for the remaining unit-cycling commands in the
// field (Dissidia maps them to its battle self query).
void CmdSelfLine(void)
{
    char line[192];
    FieldLine(line, sizeof(line));
    if (ActorIsTutorial()) g_lastDeck = (int) u32(kDeckCursorAddr);
    Say(line, "status", oga::Priority::High);
}

// ---- the game-over menu (VERIFIED 2026-10-24, bench go1/go2/go3) ------
//
// Death in the field -> a fade -> a three-row menu over the fallen
// character: Continue / Retry / Load Game. The bench mapped it end
// to end: death at a deterministic frame with cur HP pinned to 1,
// the menu fully up ~394 frames later, and every row's landing
// driven (Retry restarts the fight in place at full HP on the SAME
// actor base; Continue leaves to the world map and frees the actor
// struct; Load Game opens the game's Load screen over the
// still-drawn menu — that screen's own speech is separate work).
//
// The rows live in a small list object:
//   +0x00 u16 0xFFFF   +0x02 u16 0xFFF8   +0x04 u16 0
//   +0x06 u16 CURSOR   +0x08 u16 0        +0x0A u16 COUNT (3)
// The cursor word was the UNIQUE exact match over 24 MB of user RAM
// for the whole D-pad walk (0,1,2,0,2,1,0), and the six-word
// signature occurs once in every menu dump and nowhere in fight,
// death, transition, hub, title, difficulty or camp dumps. The
// object sits inside the battle scene object, so its address can
// float per lineage: it is located by signature scan (unique hit
// only), never trusted at a fixed address.
//
// The signature alone is NOT the gate — the object is not freed
// when the menu closes: it still reads perfectly on the world map
// after Continue and under the Load screen. Two witnesses close
// the gate:
//   * the player actor still reads dead at the last base where
//     it verified alive (state word 15, cur HP 0 — InField keeps
//     that base current; see DeadActorWitness for why the corpse
//     is re-read live instead of latched at the death frame).
//     After Continue the struct is freed (state reads 0);
//     after Retry the same base is alive at full HP.
//   * two fields of the SAME object, at +0x14 and +0x16, read 0
//     in every menu dump (seven dumps, five boots) and non-zero
//     whenever another screen owns the display: 14 and 2 under
//     the Load screen, 5 and 2 on the world map. (A neighbouring
//     word at +0x12 corroborates: 1279 in every menu dump, 4607
//     under the Load screen, 5120 on the world map.)
// The death fade itself stays silent: the signature is absent
// there (the object is not built yet).
//
// FALSIFIED WITNESS, do not re-crown: three words at
// 0x099FB41A4/B4/214 read 16 in every game-over menu dump of the
// go1/go3 sessions and 17 in their fight/world-map/load dumps,
// which looked like a module-state gate. A later live session
// (khbbs-go-dbg, same lineage, same menu) read 0 at all three —
// lineage-session junk that happened to correlate. The witnesses
// above are the ones that survived a live session.
constexpr uint32_t kGoOverlayA = 0x14;   // offset in the list object
constexpr uint32_t kGoOverlayB = 0x16;   // offset in the list object
const char* const kGameOverItems[] = { "Continue", "Retry", "Load Game" };
constexpr int kGameOverCount = 3;

uint32_t g_goStruct = 0;      // located list object; 0 = none
uint64_t g_lastGoScanMs = 0;

bool GameOverStructAt(uint32_t x)
{
    return u16(x) == 0xFFFF && u16(x + 2) == 0xFFF8 && u16(x + 4) == 0 &&
           u16(x + 8) == 0 && u16(x + 10) == (uint16_t) kGameOverCount &&
           u16(x + 6) < (uint16_t) kGameOverCount;
}

// Sweep for the list object; unique hit only, the LocateActor
// rule — a tie means a layout this build doesn't know.
uint32_t LocateGameOverStruct(void)
{
    uint32_t found = 0;
    for (uint32_t x = 0x09800000; x + 12 <= 0x0A000000; x += 2) {
        if (u16(x) != 0xFFFF) continue;
        if (!GameOverStructAt(x)) continue;
        if (found) return 0;   // second hit: ambiguous, trust neither
        found = x;
    }
    return found;
}

bool DeadActorWitness(void)
{
    // The corpse, read live at the last base where the actor
    // verified alive. Reading it fresh every frame — rather than
    // latching "he died" on the one frame the field gate drops —
    // is deliberate: the first live proof (khbbs-menus-live)
    // missed the menu entirely because the HP and state words do
    // not necessarily land together on that single frame.
    if (!g_lastActorBase) return false;
    return u16(g_lastActorBase + kActorOffCurHp) == 0 &&
           u32(g_lastActorBase + kActorOffState) == 15;
}

bool GameOverUp(void)
{
    if (!DeadActorWitness()) { g_goStruct = 0; return false; }
    if (!g_goStruct || !GameOverStructAt(g_goStruct)) {
        g_goStruct = 0;
        // The signature scan walks the heap like LocateActor's
        // sweep, so it is throttled; the corpse witness already
        // confines it to frames around a real death.
        uint64_t now = g_host ? g_host->now_ms : 0;
        if (g_lastGoScanMs && now >= g_lastGoScanMs && now - g_lastGoScanMs < 500)
            return false;
        g_lastGoScanMs = now;
        g_goStruct = LocateGameOverStruct();
        if (!g_goStruct) return false;
    }
    // The object outlives its menu (see the header): the overlay
    // fields read 0 only while THIS menu owns the screen.
    return u16(g_goStruct + kGoOverlayA) == 0 &&
           u16(g_goStruct + kGoOverlayB) == 0;
}

void GameOverRow(char* buf, size_t n)
{
    int cur = (int) u16(g_goStruct + 6);
    snprintf(buf, n, "%s, %d of %d", kGameOverItems[cur], cur + 1, kGameOverCount);
}

// ---- the movie pause menu (VERIFIED 2026-10-24, bench mp3/mp4) -------
//
// START during an in-game scene pauses it: a "PAUSE" menu with
// Resume / Skip Scene, opening on Resume, wrapping both ways.
// (The opening movie before the title is different: START skips
// it outright, no menu — bench mp1.) The menu's words are statics
// in the menu-system area:
//   cursor u16 @ 0x09918D76    count u16 @ 0x09918D7A
//   word   u16 @ 0x09918D82    word  u16 @ 0x09918D84
// The cursor word was the unique exact match for the pause walk
// (0,1,0,1,1). The count word alone is NOT the gate: it reads 2
// while the scene plays unpaused once the object exists, and it
// keeps reading 2 after Resume. The two state words carry the
// gate — 1024 and 255 in every paused dump; 5120-5121 and 5 in
// the scene playing, the visit dialog, the world map, the fight,
// the game-over menu, and after resuming. Other lineages read
// unrelated junk at all four words (camp, title, hub, difficulty
// dumps), which fails the conjunction and stays silent.
// Outcomes (mp4, deterministic single choices read off the live
// cursor word): Resume closes the menu and the scene plays on —
// a second START re-opened this same menu. Skip Scene ends the
// scene at once, no confirmation dialog, into field control in
// the plaza at full HP, with the fight the scene was introducing
// starting (INFORMATION banner, Terranort across the square).
constexpr uint32_t kMpCursorAddr = 0x09918D76;
constexpr uint32_t kMpCountAddr  = 0x09918D7A;
constexpr uint32_t kMpWord82Addr = 0x09918D82;
constexpr uint32_t kMpWord84Addr = 0x09918D84;
const char* const kMoviePauseItems[] = { "Resume", "Skip Scene" };
constexpr int kMoviePauseCount = 2;

bool g_mpUp = false;          // gate state last frame (arrival edge)

bool MoviePauseUp(void)
{
    if (u16(kMpCountAddr) != (uint16_t) kMoviePauseCount) return false;
    if (u16(kMpWord84Addr) != 255) return false;
    if (u16(kMpWord82Addr) != 1024) return false;
    return u16(kMpCursorAddr) < (uint16_t) kMoviePauseCount;
}

void MoviePauseRow(char* buf, size_t n)
{
    int cur = (int) u16(kMpCursorAddr);
    snprintf(buf, n, "%s, %d of %d", kMoviePauseItems[cur], cur + 1, kMoviePauseCount);
}

// Arrival speech for the two menus above. on_frame runs these
// every frame — both gates fail cheap (a handful of word reads)
// — and each speaks once on the rising edge, the low-HP watch's
// pattern. The spoken row is read live: the game-over menu's
// opening row varies between deaths, so it is never assumed.
// The game-over edge is debounced: the first live run proved
// the gate drops for a frame or two during a cursor move (the
// arrival line re-fired mid-menu, ahead of the nav echo), so a
// rise only speaks after the gate was down for a real stretch
// — a Load-screen round trip lasts hundreds of frames and
// still re-announces on return.
bool g_goWasUp = false;         // gate state last frame
int g_goDownFrames = 1 << 30;   // frames since the gate was last up

void WatchGameOver(void)
{
    bool up = GameOverUp();
    if (up) {
        if (!g_goWasUp && g_goDownFrames >= 30) {
            char row[96]; GameOverRow(row, sizeof(row));
            char line[160];
            snprintf(line, sizeof(line), "Game over. %s.", row);
            Say(line, "menu", oga::Priority::High);
        }
        g_goWasUp = true;
        g_goDownFrames = 0;
    } else {
        g_goWasUp = false;
        if (g_goDownFrames < (1 << 30)) g_goDownFrames++;
    }
}

void WatchMoviePause(void)
{
    bool up = MoviePauseUp();
    if (up && !g_mpUp) {
        char row[96]; MoviePauseRow(row, sizeof(row));
        char line[160];
        snprintf(line, sizeof(line), "Paused. %s.", row);
        Say(line, "menu", oga::Priority::High);
    }
    g_mpUp = up;
}

// ---- the Mirage Arena event menu (VERIFIED 2026-10-24, bench g10) ----
//
// The Hub lobby's north console — gimmick g10vs00 at (0, 8.20),
// a big machine with a circular green holographic screen —
// carries the field prompt "Log In". Cross on the prompt opens
// the MIRAGE ARENA menu, the Arena's online event console:
// New Event / Active Events / D-Links, opening on New Event,
// wrapping both ways. (OpenKH's object dictionary names the
// gimmick "Arena Entrance Device". Its second spawn copy, in
// the room's event pattern, sits at the same lobby spot with
// no other service present, which fits an event fixture.)
// Choosing New Event opens a mode list (Versus Mode / Arena
// Mode / Command Board / Rumble Racing; Versus Mode greyed
// while offline) — that screen is MAPPED but NOT wired: this
// gate closes there (see below), so the child stays silent and
// backing out re-announces this menu. Active Events and
// D-Links would list other players' sessions; with nobody in
// range both are no-ops that leave this menu up.
// The menu's widget exists in TWO homes with one layout
// (base = the screen word; offsets +0x00 screen, +0x0E cursor,
// +0x10 previous-cursor tell, +0x12 count):
//   home A @ 0x09919208 — fresh opens from the field (statics
//     in the same menu-system block as the camp registry,
//     0x09919264). First open and every re-open land here.
//   home B @ 0x0991BCB8 — the menu REBUILT after backing out
//     of the New Event mode child: there home A reads freed
//     junk while the menu is visibly up and navigable
//     (bench g10d/g10e: returned-menu row dumps track the
//     cursor at home B and nowhere else).
// The gate is the widget's full signature, identical at both
// homes in the row dumps: screen == 2, +0x04 == 15,
// +0x08 == 0xDBFF, +0x0A == 0xFFF8, +0x0C == 0, count == 3,
// cursor < 3 — CONJOINED with the menu-system active word
// (u32 @ 0x08B5BEF8 == 8, the camp gate's witness A: this
// menu is built on the same menu system as camp, and the
// word is up through every phase of the event flow) and
// with camp itself excluded (camp shares the word, but its
// witness B reads 0 throughout the event flow, so
// CampActive() stays false here). The active word is
// load-bearing, not decoration: when the menu closes after
// a mode-child trip, the home-B widget is never torn down —
// its whole block lingers with the signature intact while
// the player walks the field (proven by the g10c/g10f end
// dumps) — and only the active word (8 through the flow,
// 0 in the Hub field before and after, bench g10k's two
// full open/dive/return/close cycles) separates "menu up"
// from "menu gone, widget stale". Across the bench's 2,478
// RAM dumps the full widget signature matches only in
// event-menu states (fresh opens at A, returns at B) and
// in those post-close field dumps where the word is 0.
// On the mode child the gate fails at both homes (A reads
// zeros; B holds the child's own ghost with screen 3 and
// count 0), so the child stays silent and backing out
// re-announces this menu.
// The arrival edge is debounced like the game-over menu's:
// a rise speaks only after the gate was down for 30+ frames,
// so a flicker cannot re-fire the line mid-menu, while the
// mode-screen round trip (hundreds of frames down) still
// re-announces on return.
constexpr uint32_t kEvHomeA = 0x09919208;   // fresh opens
constexpr uint32_t kEvHomeB = 0x0991BCB8;   // rebuilt after the mode child
constexpr uint32_t kEvFlowAddr = 0x08B5BEF8;  // menu-system active word (camp witness A), u32 == 8
const char* const kEventMenuItems[] = { "New Event", "Active Events", "D-Links" };
constexpr int kEventMenuCount = 3;

bool g_evUp = false;            // gate state last frame
int g_evDownFrames = 1 << 30;   // frames since the gate was last up

// One widget home's signature; on a match, *row (if given)
// receives the cursor.
bool EventMenuAt(uint32_t base, int* row)
{
    if (u16(base + 0x00) != 2) return false;
    if (u16(base + 0x04) != 15) return false;
    if (u16(base + 0x08) != 0xDBFF) return false;
    if (u16(base + 0x0A) != 0xFFF8) return false;
    if (u16(base + 0x0C) != 0) return false;
    if (u16(base + 0x12) != (uint16_t) kEventMenuCount) return false;
    uint16_t cur = u16(base + 0x0E);
    if (cur >= (uint16_t) kEventMenuCount) return false;
    if (row) *row = (int) cur;
    return true;
}

bool EventMenuUp(void)
{
    if (u32(kEvFlowAddr) != 8) return false;
    if (CampActive()) return false;   // camp shares the active word; not this menu
    if (EventMenuAt(kEvHomeA, nullptr)) return true;
    return EventMenuAt(kEvHomeB, nullptr);
}

void EventMenuRow(char* buf, size_t n)
{
    int cur = 0;
    if (!EventMenuAt(kEvHomeA, &cur)) EventMenuAt(kEvHomeB, &cur);
    snprintf(buf, n, "%s, %d of %d", kEventMenuItems[cur], cur + 1, kEventMenuCount);
}

void WatchEventMenu(void)
{
    bool up = EventMenuUp();
    if (up) {
        if (!g_evUp && g_evDownFrames >= 30) {
            char row[96]; EventMenuRow(row, sizeof(row));
            char line[160];
            snprintf(line, sizeof(line), "Mirage Arena. %s.", row);
            Say(line, "menu", oga::Priority::High);
        }
        g_evUp = true;
        g_evDownFrames = 0;
    } else {
        g_evUp = false;
        if (g_evDownFrames < (1 << 30)) g_evDownFrames++;
    }
}

// ---- the world map (VERIFIED 2026-10-24, bench wm1) -------------------
//
// World 0x11, room 1 in the map-id space: a star map the player
// flies with the analog stick (the character's Keyblade
// glider). The D-pad does nothing on the map itself. Flying
// into a world's ring selects it — the banner shows the
// world's name, COMBAT LV and TREASURES % — and Triangle opens
// the visit dialog there. Proven on two lineages (a Final
// Episode save parked at Radiant Garden and the 100% fixture's
// Terra parked at Mirage Arena), driven by hand through the
// bench harness with screenshots at every step.
//
// Witnesses:
//   - Both map-id copies hold 17/1 for the WHOLE session:
//     parked, in flight, with the visit dialog open, with the
//     map's pause menu open, and after either closes.
//     OnWorldMap below is that conjunction, and InField()
//     excludes it (the glider-side actor otherwise passes the
//     field gate and the low-HP warning fired on the map).
//   - The selected-world word u32 @ 0x099658C0 (the low byte
//     is the world id; the upper bytes read 0 in every dump):
//     6 parked at Radiant Garden, 15 at Mirage Arena, 0 in
//     open space ("in transit"). The word the 2026-10-18
//     hunts first read, reconfirmed in wm1 by flying the
//     glider in and out of both rings.
//   - The visit dialog is a two-row list (Visit This World /
//     Leave) whose object RELOCATES per open and outlives the
//     dialog — the game-over list object's family. It is
//     found by the game-over locator's method: a unique
//     signature scan (head u16s 0xE7FF/0xFFF8, zero at +4,
//     cursor < 2 at +6, previous < 2 at +8, count 2 at +0xA),
//     then an ACTIVE witness: bit 28 of the u32 at +0x10 is
//     clear only while the dialog is open (the u16 at +0x12
//     reads 0x04 open, 0x14 closed — nine dumps, both
//     lineages). The cursor STARTS on Leave. Circle backs out
//     (the dialog closes, still parked in the ring); choosing
//     Leave closes it; choosing Visit This World leaves the
//     map for that world. A stale count-2 list object found in
//     the field reads head 0xFDFF with its active bit set —
//     excluded twice over.
//   - START on the map opens the map's own pause menu —
//     Resume / Open Menu / Save, opening on Resume, wrapping
//     both ways — on the SAME menu-system statics as the movie
//     pause (cursor @ 0x09918D76, count @ 0x09918D7A, state
//     words @ 0x09918D82/0x09918D84), with count 3 and the
//     state words at their open values 1024/255. The movie
//     pause requires count 2, so the two never cross-fire.
//     Choosing Open Menu opens the camp menu (its own speech
//     owns the screen then); choosing Save opens the save
//     screen (the save/load section below).
//
// What is NOT read live, and so not spoken: the selected
// world's position in the selectable list ("N of M"). The
// selectable set is campaign state, and no live witness for
// it survived the hunt: no node table exists in RAM or in
// the loaded map module in any shape the scans tried (world
// ids with position floats at any stride), and the one clean
// world-id sequence found in the module image is a
// trigger/trophy string table, not map data. Selection
// changes therefore speak the world's name alone. The full
// hunt is recorded in the RE doc.
constexpr uint32_t kSelWorldAddr = 0x099658C0;
constexpr int kVisitCount = 2;
const char* const kVisitItems[] = { "Visit This World", "Leave" };
const char* const kMapPauseItems[] = { "Resume", "Open Menu", "Save" };
constexpr int kMapPauseCount = 3;

uint32_t g_visitObj = 0;         // the visit dialog's object (relocates per open)
uint64_t g_lastVisitScanMs = 0;  // scan throttle, the game-over pattern
bool g_wmUp = false;             // OnWorldMap last frame (arrival edge)
int g_wmSel = -1;                // selection last adopted by the watcher
int g_wmSelCand = -1;            // candidate selection being confirmed
int g_wmSelFrames = 0;           // frames the candidate has persisted
bool g_wmDlgUp = false;          // visit dialog gate last frame
int g_wmDlgDownFrames = 1 << 30; // frames since the dialog gate was last up
bool g_wmPauseUp = false;        // map pause gate last frame

bool OnWorldMap(void)
{
    return u8(kWorldAddrA) == 0x11 && u8(kWorldAddrA + 1) == 1 &&
           u8(kWorldAddrB) == 0x11 && u8(kWorldAddrB + 1) == 1;
}

int SelectedWorld(void)
{
    return (int) (u32(kSelWorldAddr) & 0xFF);
}

// The selected world's name from the WhereAmI table, or
// nullptr when nothing (or nothing nameable) is selected.
const char* SelectedWorldName(void)
{
    int sel = SelectedWorld();
    if (sel <= 0 || sel >= kWorldKnown) return nullptr;
    return kWorldNames[sel];
}

bool VisitStructAt(uint32_t x)
{
    return u16(x) == 0xE7FF && u16(x + 2) == 0xFFF8 && u16(x + 4) == 0 &&
           u16(x + 0xA) == (uint16_t) kVisitCount &&
           u16(x + 6) < (uint16_t) kVisitCount &&
           u16(x + 8) < (uint16_t) kVisitCount;
}

// Signature scan for the visit dialog's list object (the
// game-over locator's method). Only a UNIQUE hit is accepted.
uint32_t LocateVisitStruct(void)
{
    uint32_t found = 0;
    for (uint32_t x = 0x09800000; x + 0x20 <= 0x0A000000; x += 2) {
        if (!VisitStructAt(x)) continue;
        if (found) return 0;   // second hit: ambiguous, trust neither
        found = x;
    }
    return found;
}

bool VisitDialogUp(void)
{
    if (!OnWorldMap()) { g_visitObj = 0; return false; }
    // The cached object is re-validated every frame; its
    // active witness (below) says open vs closed. But the
    // object RELOCATES per open, so a cached object whose
    // witness reads closed cannot be trusted to be the
    // current one: re-scan (throttled, the game-over pattern)
    // and adopt a fresh unique hit when one appears.
    bool cachedOpen = g_visitObj && VisitStructAt(g_visitObj) &&
                      (u32(g_visitObj + 0x10) & 0x10000000u) == 0;
    if (!cachedOpen) {
        uint64_t now = g_host ? g_host->now_ms : 0;
        if (!(g_lastVisitScanMs && now >= g_lastVisitScanMs &&
              now - g_lastVisitScanMs < 500)) {
            g_lastVisitScanMs = now;
            uint32_t hit = LocateVisitStruct();
            if (hit) g_visitObj = hit;
            else if (!g_visitObj || !VisitStructAt(g_visitObj)) g_visitObj = 0;
        }
    }
    if (!g_visitObj || !VisitStructAt(g_visitObj)) return false;
    return (u32(g_visitObj + 0x10) & 0x10000000u) == 0;
}

void VisitRow(char* buf, size_t n)
{
    int cur = (int) u16(g_visitObj + 6);
    snprintf(buf, n, "%s, %d of %d", kVisitItems[cur], cur + 1, kVisitCount);
}

void VisitArrivalLine(char* buf, size_t n)
{
    char row[96]; VisitRow(row, sizeof(row));
    const char* wn = SelectedWorldName();
    if (wn) snprintf(buf, n, "Visit %s? %s.", wn, row);
    else snprintf(buf, n, "Visit this world? %s.", row);
}

// The word-84 witness reads 255 at a fresh open but 4 after the
// pause has hosted the savedata dialog and taken the screen back
// (VERIFIED 2026-10-24, bench sl4/sl8: cursor and count intact,
// the menu fully live, the word settled at 4 in both the cancel
// and the completed-save returns). Both values are the pause's;
// closed reads 5. The movie pause keeps the strict 255 — no
// dialog ever opens over it.
bool MapPauseUp(void)
{
    if (!OnWorldMap()) return false;
    if (u16(kMpCountAddr) != (uint16_t) kMapPauseCount) return false;
    uint16_t w84 = u16(kMpWord84Addr);
    if (w84 != 255 && w84 != 4) return false;
    if (u16(kMpWord82Addr) != 1024) return false;
    return u16(kMpCursorAddr) < (uint16_t) kMapPauseCount;
}

void MapPauseRow(char* buf, size_t n)
{
    int cur = (int) u16(kMpCursorAddr);
    snprintf(buf, n, "%s, %d of %d", kMapPauseItems[cur], cur + 1, kMapPauseCount);
}

// "World map." plus the selected world's name when one is
// selected — the arrival line and the WhereAmI form.
void WorldMapLine(char* buf, size_t n)
{
    const char* wn = SelectedWorldName();
    if (wn) snprintf(buf, n, "World map. %s.", wn);
    else snprintf(buf, n, "World map.");
}

// The world-map watcher: arrival, selection changes, and the
// two sub-menus' arrivals. on_frame runs it before the field
// gate (the map is excluded from InField, so without this the
// map would be silent). Selection changes are confirmed over
// a few frames: the selection word is radius-based, and a
// graze along a ring's edge must not chatter. Reaching a
// world speaks its name; leaving one for open space stays
// silent, like losing a lock-on.
void WatchWorldMap(void)
{
    if (!OnWorldMap()) {
        g_wmUp = false; g_wmSel = -1; g_wmSelCand = -1; g_wmSelFrames = 0;
        g_wmDlgUp = false; g_wmPauseUp = false;
        if (g_wmDlgDownFrames < (1 << 30)) g_wmDlgDownFrames++;
        return;
    }
    if (!g_wmUp) {
        // Adopt the selection already in place: arriving parked
        // in a ring names the world once, in this line, and the
        // change watcher starts from it instead of repeating it.
        g_wmSel = SelectedWorld();
        g_wmSelCand = -1; g_wmSelFrames = 0;
        char line[192]; WorldMapLine(line, sizeof(line));
        Say(line, "status", oga::Priority::High);
    }
    g_wmUp = true;
    // The sub-menus own speech while they are up. The dialog's
    // arrival edge is debounced like the game-over menu's: the
    // first wm-live run proved the dialog gate drops for a few
    // frames during a cursor move (the arrival line re-fired
    // ahead of the nav echo on both D-pad taps), so a rise
    // speaks only after the gate was down for a real stretch —
    // a genuine close-and-reopen lasts far longer.
    bool dlg = VisitDialogUp();
    if (dlg) {
        if (!g_wmDlgUp && g_wmDlgDownFrames >= 30) {
            char line[192]; VisitArrivalLine(line, sizeof(line));
            Say(line, "menu", oga::Priority::High);
        }
        g_wmDlgUp = true;
        g_wmDlgDownFrames = 0;
    } else {
        g_wmDlgUp = false;
        if (g_wmDlgDownFrames < (1 << 30)) g_wmDlgDownFrames++;
    }
    bool pz = MapPauseUp();
    if (pz && !g_wmPauseUp) {
        char row[96]; MapPauseRow(row, sizeof(row));
        char line[192];
        snprintf(line, sizeof(line), "Paused. %s.", row);
        Say(line, "menu", oga::Priority::High);
    }
    g_wmPauseUp = pz;
    if (dlg || pz) return;
    int sel = SelectedWorld();
    if (sel == g_wmSel) { g_wmSelCand = -1; g_wmSelFrames = 0; return; }
    if (sel != g_wmSelCand) { g_wmSelCand = sel; g_wmSelFrames = 1; return; }
    if (++g_wmSelFrames < 6) return;
    g_wmSel = sel; g_wmSelCand = -1; g_wmSelFrames = 0;
    if (const char* wn = SelectedWorldName()) {
        char line[192];
        snprintf(line, sizeof(line), "%s.", wn);
        Say(line, "status", oga::Priority::Normal);
    }
}

// ---- the save / load screens (VERIFIED 2026-10-24, bench sl1-sl6) ---
//
// Save and Load are the PSP firmware's savedata utility dialog (the
// game calls sceUtilitySavedata in its LIST modes), rendered by the
// emulator host-side. Entered from the save point's menu and the
// world map's pause (Save), and from the title's Continue and the
// game-over menu's Load Game row (Load). Three consequences shape
// everything below:
//
//  * The live selection is a host-side variable of the dialog. No
//    guest word is a function of it: an 18-dump walk (idle controls,
//    exact-sequence and pattern matching over the full 32 MB) found
//    zero candidates, and dumps with different selections differ
//    only in the animated field behind the dialog. So the cursor
//    is TRACKED FROM THE HOST'S BUTTON ECHOES, the meld confirm
//    dialog's precedent — seeded at arrival from the dialog's own
//    start rule (below), moved by MenuNext/MenuPrev echoes, and
//    clamped at both ends: PPSSPP's list clamps, it never wraps
//    (PSPSaveDialog::DisplaySaveList), so echo counting with the
//    same clamp can never drift.
//  * A slot's stored details (character, location, level, munny,
//    play time) are read by the dialog from the memory stick's
//    PARAM.SFO files, host-side; they never pass through guest
//    RAM. The one SFO block in RAM is the CURRENT session's
//    template (what a new save would write), identical whatever
//    slot is focused — speaking it as a slot's contents would be
//    inventing data. Rows therefore speak the slot's number and
//    position only. Occupied vs empty is likewise host-side
//    knowledge; the overwrite confirmation is tracked through
//    the ambiguity that creates (below).
//  * The dialog's sub-states (list / overwrite confirm / saving /
//    save completed) have no guest-visible signature either —
//    two confirm dumps differ from list and done dumps in no
//    stable word. Sub-state is echo-tracked like the cursor.
//
// The gate is a conjunction of two witnesses, each proven against
// the other one's failure mode:
//  * Two words in the utility dialog's fixed work area at the top
//    of RAM (0x09FFF5C4 / 0x09FFF5C8) hold pointers in every open
//    dump (5 samples, both modes, three lineages, four routes)
//    and read 0 in every closed dump — field, save-point menu,
//    title, camp, world map, and the title again after backing
//    out of the dialog. They LINGER at their open values briefly
//    after a load completes (the field already resumed in one
//    capture), so they are the cheap pre-check, never the proof.
//  * The game's own SceUtilitySavedataParam block, found by a
//    signature scan (size word 0x600, gameName "ULUS10505" at
//    +0x3C, fileName "SAVEDATA.DAT" at +0x64, mode LISTLOAD=4 or
//    LISTSAVE=5 at +0x30). It exists exactly while a dialog
//    session uses it — absent in every no-dialog dump — but after
//    a LOAD session it LINGERS, fully formed, once the dialog has
//    closed (its result word then reads 1 for cancel, 0 for
//    success). The arena words are what separate "dialog up"
//    from "param block left over", so the gate is arena AND
//    param, never either alone. The block also carries the mode,
//    the slot-name list pointer (+0x60; 20-byte names, counted
//    until an empty entry — 200 names for this game,
//    "0000".."0199"), the focus request (+0x5C8) and the focus
//    name (+0x4C).
//
// The arrival focus follows PPSSPP's InitStart exactly for the
// requests whose inputs are guest-readable: FOCUS_NAME seeds at
// the index of the focus name in the list (0 when absent — both
// save routes observed: the Hub session's empty name → slot 1;
// the world-map Final Episode session's "0003" → slot 4),
// FOCUS_FIRSTLIST at 0, FOCUS_LASTLIST at the last slot. The LOAD
// dialogs request FOCUS_LATEST — the most recently modified save,
// a memory-stick fact the guest never sees — so a load opens
// UNSEEDED: arrival speaks "Load." alone, nav echoes narrow a
// possible-range with the same clamp, and rows start speaking
// only once the range collapses to one slot (in practice: after
// the player walks to either end). Nothing is ever announced
// from an unproven position.
//
// The overwrite confirm (bench sl6, frame-verified): Cross on an
// occupied slot asks "Do you want to overwrite the data?" with
// NO focused (the safe default); Left/Right move between them
// (Left at No → Yes, Right at Yes → No); Cross answers; Circle
// or answering No returns to the list. Cross on an EMPTY slot
// goes straight to the write ("Saving. Please Wait..." for a
// few frames, then "Save completed", dismissed with Circle).
// Because occupancy is unknowable, a Cross in the save list is
// ambiguous until the player's next input resolves it:
//   Cross, Left          -> the confirm is up (Left matters only
//                           there); it now sits on Yes. Announced.
//   Cross, Left, Cross   -> overwriting: the write runs in both
//                           readings of the first Cross, so the
//                           tracker enters Saving and "Saved."
//                           is spoken when the dialog closes.
//   Cross, Cross / Cross, Circle with no Left -> the No answer
//                           returning to the list and the write
//                           starting are indistinguishable; the
//                           tracker stays silent rather than
//                           guess, and the screens around the
//                           dialog name where the player lands.
// Load has no confirm: Cross starts the load ("Loading."), and
// the completed screen accepts Circle or Cross. A load of an
// empty slot fails ("Load failed") and Circle there returns to
// the list — the tracker's dismiss timeout (300 frames with
// the dialog still up; a real transfer of this size finishes
// far sooner, khbbs-sl7's slow load took ~120) makes the same
// return, re-speaking the row.
//
// Two tracker disciplines from the same live run: the press
// that OPENS the dialog can echo after the gate has risen (it
// opened the Load screen from the game-over menu and the late
// echo then read as a slot choice), so echoes are ignored for
// the first few frames after seeding; and a dialog that closes
// and reopens inside the arrival debounce still re-seeds its
// tracker silently — only the announcement is debounced.
//
// Camp trap: the camp witnesses (0x08B5BEF8 == 8, 0x08BD194C
// == 2) are SET under this dialog when it opens from the save
// point, and WhereAmI used to answer "Camp menu." there. The
// save/load branch therefore precedes camp in both command
// handlers, and CampTrack is skipped while the dialog is up.
constexpr uint32_t kSlGateAAddr = 0x09FFF5C4;
constexpr uint32_t kSlGateBAddr = 0x09FFF5C8;
constexpr uint32_t kSlParamSize = 0x600;
constexpr int kSlModeLoad = 4;    // SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD
constexpr int kSlModeSave = 5;    // SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE
constexpr uint32_t kSlOffMode = 0x30;
constexpr uint32_t kSlOffSaveName = 0x4C;
constexpr uint32_t kSlOffList = 0x60;
constexpr uint32_t kSlOffFocus = 0x5C8;
constexpr int kSlDismissFrames = 300;  // DONE-dismiss vs back-to-list
constexpr uint64_t kSlEchoGuard = 8;   // frames after seeding that
                                       // echoes are taken as the
                                       // opener's, not the dialog's

enum class SlSub : int { List, Pending, Confirm, Ambig, Saving, Loading };

bool g_slUp = false;              // gate state last frame
int g_slDownFrames = 1 << 30;     // frames since the gate was last up
uint32_t g_slParam = 0;           // cached param block address
int g_slMode = 0;                 // kSlModeLoad / kSlModeSave while up
int g_slCount = 0;                // slots in the game's name list
int g_slPos = -1;                 // tracked cursor (valid when synced)
int g_slLo = 0, g_slHi = -1;      // possible range while unseeded
bool g_slSynced = false;
int g_slOwnIdx = -1;              // the session's own file, when named
SlSub g_slSub = SlSub::List;
bool g_slChoiceYes = false;       // overwrite confirm choice (No at open)
bool g_slWrote = false;           // a write certainly ran this session
bool g_slDismissArmed = false;    // a press may have dismissed a DONE
int g_slDismissFrames = 0;        // frames since it was armed
bool g_slSeeded = false;          // this session's seed has run
uint64_t g_slSeedTick = 0;        // tick at which it ran
uint64_t g_slTick = 0;            // on_frame counter (scan throttle)
uint64_t g_slScanTick = ~0ull;    // tick of the last locate scan

void SlReset(void)
{
    g_slUp = false;
    g_slDownFrames = 1 << 30;
    g_slParam = 0;
    g_slMode = 0;
    g_slCount = 0;
    g_slPos = -1;
    g_slLo = 0; g_slHi = -1;
    g_slSynced = false;
    g_slOwnIdx = -1;
    g_slSub = SlSub::List;
    g_slChoiceYes = false;
    g_slWrote = false;
    g_slDismissArmed = false;
    g_slDismissFrames = 0;
    g_slSeeded = false;
    g_slSeedTick = 0;
    g_slScanTick = ~0ull;
}

bool SlMatchAscii(uint32_t addr, const char* s)
{
    for (int i = 0; s[i]; i++)
        if (u8(addr + (uint32_t) i) != (uint8_t) s[i]) return false;
    return true;
}

bool SlMatchAt(uint32_t x)
{
    if (u32(x) != kSlParamSize) return false;
    if (!SlMatchAscii(x + 0x3C, "ULUS10505")) return false;
    if (!SlMatchAscii(x + 0x64, "SAVEDATA.DAT")) return false;
    uint32_t mode = u32(x + kSlOffMode);
    return mode == (uint32_t) kSlModeLoad || mode == (uint32_t) kSlModeSave;
}

uint32_t SlLocateParam(void)
{
    uint32_t found = 0;
    for (uint32_t x = 0x09800000; x + 0x600 <= 0x0A000000; x += 2) {
        if (u32(x + 0x3C) != 0x53554C55) continue;   // "ULUS"
        if (!SlMatchAt(x)) continue;
        if (found) return 0;   // second hit: ambiguous, trust neither
        found = x;
    }
    return found;
}

bool SlParamValid(void)
{
    return g_slParam && SlMatchAt(g_slParam);
}

// The gate: the dialog's work-area words say a utility dialog is
// up, and the game's savedata param verifies it is THIS dialog.
// Cheap when closed (two reads); the signature scan runs only
// while the pre-check is open and the cached block fails.
bool SaveLoadUp(void)
{
    if (u32(kSlGateAAddr) == 0 || u32(kSlGateBAddr) == 0) return false;
    if (SlParamValid()) return true;
    if (g_slScanTick == g_slTick) return false;   // one scan per frame
    g_slScanTick = g_slTick;
    g_slParam = SlLocateParam();
    return SlParamValid();
}

// A fixed-width name field from the param (""-terminated).
void SlField(uint32_t addr, char* buf, size_t n)
{
    size_t i = 0;
    for (; i + 1 < n && i < 20; i++) {
        uint8_t c = u8(addr + (uint32_t) i);
        if (!c) break;
        buf[i] = (char) c;
    }
    buf[i] = 0;
}

int SlNameCount(uint32_t list)
{
    int cnt = 0;
    while (cnt < 1000 && u8(list + (uint32_t) cnt * 20) != 0) cnt++;
    return cnt;
}

int SlNameIndex(uint32_t list, int cnt, const char* name)
{
    char entry[24];
    for (int i = 0; i < cnt; i++) {
        SlField(list + (uint32_t) i * 20, entry, sizeof(entry));
        if (strcmp(entry, name) == 0) return i;
    }
    return -1;
}

// Seed the tracked cursor from the dialog's own start rule, as
// far as guest RAM allows (see the section header). Reads the
// mode fresh; the focus inputs come from the param block.
void SlSeed(void)
{
    g_slSeeded = true;
    g_slSeedTick = g_slTick;
    g_slMode = (int) u32(g_slParam + kSlOffMode);
    g_slSub = SlSub::List;
    g_slChoiceYes = false;
    g_slWrote = false;
    g_slDismissArmed = false;
    uint32_t list = u32(g_slParam + kSlOffList);
    g_slCount = list ? SlNameCount(list) : 0;
    char focusName[24];
    SlField(g_slParam + kSlOffSaveName, focusName, sizeof(focusName));
    g_slOwnIdx = (list && focusName[0])
                     ? SlNameIndex(list, g_slCount, focusName) : -1;
    uint32_t focus = u32(g_slParam + kSlOffFocus);
    g_slSynced = false;
    g_slPos = -1;
    g_slLo = 0;
    g_slHi = g_slCount - 1;
    if (g_slCount <= 0) return;
    if (focus == 0) {          // FOCUS_NAME: the named slot, else 0
        g_slPos = g_slOwnIdx >= 0 ? g_slOwnIdx : 0;
        g_slSynced = true;
    } else if (focus == 1) {   // FOCUS_FIRSTLIST
        g_slPos = 0;
        g_slSynced = true;
    } else if (focus == 2) {   // FOCUS_LASTLIST
        g_slPos = g_slCount - 1;
        g_slSynced = true;
    }
    // LATEST / OLDEST / FIRSTDATA / LASTDATA / FIRSTEMPTY /
    // LASTEMPTY need memory-stick facts (modification times,
    // which slots hold data) that never reach guest RAM: the
    // position stays a range until the echoes collapse it.
}

void SlRowText(char* buf, size_t n)
{
    if (g_slSynced && g_slPos >= 0) {
        if (g_slMode == kSlModeSave && g_slPos == g_slOwnIdx)
            snprintf(buf, n, "Slot %d of %d, current save", g_slPos + 1, g_slCount);
        else
            snprintf(buf, n, "Slot %d of %d", g_slPos + 1, g_slCount);
    } else {
        buf[0] = 0;
    }
}

void SlSayRow(void)
{
    char row[96];
    SlRowText(row, sizeof(row));
    if (row[0]) Say(row, "menu", oga::Priority::High);
}

// Whether an echo arriving now belongs to THIS dialog. Two
// exclusions: the session must be seeded (an unseeded tracker
// has nothing to move), and the press that OPENED the dialog —
// whose echo can land a few frames after the rise, once the
// gate is already up — must not be read as a press inside it
// (khbbs-sl7: the game-over menu's Cross both opened the Load
// screen and, echoed late, started a phantom "Loading."). A
// deliberate in-dialog press comes after the player has seen
// the screen fade in, so the guard window costs nothing real;
// a press inside it that truly moved the cursor is recovered
// at the list's ends by the clamp, like every echo tracker.
bool SlEchoLive(void)
{
    return SaveLoadUp() && g_slSeeded &&
           (g_slTick - g_slSeedTick) > kSlEchoGuard;
}

// One nav echo (dir = +1 down, -1 up). In the list it moves the
// tracked cursor with the dialog's own clamp; a landed move is
// spoken when the position is proven. In the ambiguous sub-state
// the position is tracked silently (the press may have been a
// no-op on a completed screen instead of a move in the list).
void CmdSaveLoadNav(int dir)
{
    if (!SlEchoLive() || g_slCount <= 0) return;
    bool announce = g_slSub == SlSub::List;
    if (g_slSub != SlSub::List && g_slSub != SlSub::Ambig) return;
    if (g_slSynced) {
        int np = g_slPos + dir;
        if (np < 0) np = 0;
        if (np > g_slCount - 1) np = g_slCount - 1;
        if (np == g_slPos) return;    // clamped: the cursor never moved
        g_slPos = np;
    } else {
        g_slLo += dir; g_slHi += dir;
        if (g_slLo < 0) g_slLo = 0;
        if (g_slHi > g_slCount - 1) g_slHi = g_slCount - 1;
        if (g_slLo > g_slHi) g_slLo = g_slHi;
        if (g_slLo != g_slHi) return; // still a range: stay silent
        g_slPos = g_slLo;
        g_slSynced = true;
    }
    if (announce) SlSayRow();
}

// Left/Right echoes: they matter only inside the overwrite
// confirm (Left at No -> Yes, Right at Yes -> No; the host dialog
// ignores them everywhere else). In the Pending sub-state a Left
// is what proves the confirm is up at all.
void CmdSaveLoadFlip(bool left)
{
    if (!SlEchoLive()) return;
    if (g_slSub == SlSub::Pending) {
        if (!left) return;    // Right at the default No changes nothing
        g_slSub = SlSub::Confirm;
        g_slChoiceYes = true;
        Say("Overwrite this save? Yes.", "menu", oga::Priority::High);
        return;
    }
    if (g_slSub != SlSub::Confirm) return;
    if (left && !g_slChoiceYes) {
        g_slChoiceYes = true;
        Say("Yes.", "menu", oga::Priority::High);
    } else if (!left && g_slChoiceYes) {
        g_slChoiceYes = false;
        Say("No.", "menu", oga::Priority::High);
    }
}

void CmdSaveLoadCross(void)
{
    if (!SlEchoLive()) return;
    switch (g_slSub) {
        case SlSub::List:
            if (g_slMode == kSlModeLoad) {
                g_slSub = SlSub::Loading;
                Say("Loading.", "menu", oga::Priority::High);
            } else {
                g_slSub = SlSub::Pending;   // confirm or write: next input tells
                g_slChoiceYes = false;
            }
            break;
        case SlSub::Pending:
            if (g_slChoiceYes) {
                g_slSub = SlSub::Saving;
                g_slWrote = true;
                Say("Saving.", "menu", oga::Priority::High);
            } else {
                g_slSub = SlSub::Ambig;     // No answer, or an empty slot's write
            }
            break;
        case SlSub::Confirm:
            if (g_slChoiceYes) {
                g_slSub = SlSub::Saving;
                g_slWrote = true;
                Say("Saving.", "menu", oga::Priority::High);
            } else {
                g_slSub = SlSub::List;      // No: back to the list, provably
                SlSayRow();
            }
            break;
        case SlSub::Loading:
            // The completed screen takes Cross as well as Circle.
            g_slDismissArmed = true;
            g_slDismissFrames = 0;
            break;
        default:
            break;
    }
}

void CmdSaveLoadCircle(void)
{
    if (!SlEchoLive()) return;
    switch (g_slSub) {
        case SlSub::Confirm:
            g_slSub = SlSub::List;          // Circle backs out to the list
            SlSayRow();
            break;
        case SlSub::Pending:
            g_slSub = SlSub::Ambig;         // confirm exit, or a DONE dismiss
            g_slDismissArmed = true;
            g_slDismissFrames = 0;
            break;
        case SlSub::Ambig:
        case SlSub::Saving:
        case SlSub::Loading:
            g_slDismissArmed = true;
            g_slDismissFrames = 0;
            break;
        default:
            break;   // List: the dialog closes; the drop handler tidies up
    }
}

// The per-frame save/load watch: arrival on the rising edge (with
// the game-over watch's 30-frame debounce), completion on the
// falling edge, and the dismiss timeout that separates "the DONE
// screen is waiting" from "the dialog went back to its list".
void WatchSaveLoad(void)
{
    g_slTick++;
    bool up = SaveLoadUp();
    if (up) {
        if (!g_slSeeded && g_slParam) {
            // Seed once per session. The arrival LINE keeps the
            // game-over watch's 30-frame debounce (a quick close
            // and reopen does not re-announce), but the tracker
            // seeds either way, so echoes track the new session.
            bool announce = !g_slUp && g_slDownFrames >= 30;
            SlSeed();
            if (announce) {
                char line[160];
                char row[96];
                SlRowText(row, sizeof(row));
                if (row[0])
                    snprintf(line, sizeof(line), "%s. %s.",
                             g_slMode == kSlModeSave ? "Save" : "Load", row);
                else
                    snprintf(line, sizeof(line), "%s.",
                             g_slMode == kSlModeSave ? "Save" : "Load");
                Say(line, "menu", oga::Priority::High);
            }
        }
        g_slUp = true;
        g_slDownFrames = 0;
        if (g_slDismissArmed && g_slSub != SlSub::List) {
            if (++g_slDismissFrames >= kSlDismissFrames) {
                // The dialog is still up long after the press: it
                // was not a dismiss — a failed load/save returned
                // to the list, or the press landed mid-write.
                g_slDismissArmed = false;
                g_slSub = SlSub::List;
                SlSayRow();
            }
        }
    } else {
        if (g_slUp && g_slSub == SlSub::Saving && g_slWrote)
            Say("Saved.", "menu", oga::Priority::High);
        if (g_slUp) {
            g_slUp = false;
            g_slSeeded = false;
            g_slSub = SlSub::List;
            g_slDismissArmed = false;
            g_slWrote = false;
        }
        if (g_slDownFrames < (1 << 30)) g_slDownFrames++;
    }
}

// WhereAmI / MenuNav form for the dialog. The row rides along
// only when the position is proven; sub-states speak their own
// current line (the confirm's focused answer, the transfer in
// flight).
void SaveLoadLine(char* buf, size_t n)
{
    char row[96];
    SlRowText(row, sizeof(row));
    const char* screen = g_slMode == kSlModeSave ? "Save screen" : "Load screen";
    switch (g_slSub) {
        case SlSub::Confirm:
            snprintf(buf, n, "%s. Overwrite this save? %s.", screen,
                     g_slChoiceYes ? "Yes" : "No");
            break;
        case SlSub::Saving:
            snprintf(buf, n, "%s. Saving.", screen);
            break;
        case SlSub::Loading:
            snprintf(buf, n, "%s. Loading.", screen);
            break;
        default:
            if (row[0]) snprintf(buf, n, "%s. %s.", screen, row);
            else snprintf(buf, n, "%s.", screen);
            break;
    }
}

void CmdWhereAmI(void)
{
    char line[192];
    if (SaveLoadUp()) {
        // The savedata dialog owns the screen while it is up —
        // ahead of camp, whose witnesses are also set under it
        // when it opens from the save point (see the section
        // header's camp trap).
        SaveLoadLine(line, sizeof(line));
        Say(line, "menu", oga::Priority::High);
        return;
    }
    CampTrack();
    if (CampActive() || ReportsUp()) {
        // The camp menu opens ON TOP of the field; it wins over the
        // field readout whenever its witnesses say it is up.
        // Reports counts too: its module kills the camp witnesses,
        // but the menu still owns speech while it is open.
        CampWhere(line, sizeof(line));
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // The game-over and movie-pause menus sit on top of (or
    // instead of) the field, like camp: they win over the field
    // readout whenever their gates verify. A paused scene can
    // leave a plausible-looking actor parked under it, and a dead
    // one is no field at all, so both checks precede InField().
    if (GameOverUp()) {
        char row[96]; GameOverRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Game over. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (MoviePauseUp()) {
        char row[96]; MoviePauseRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Paused. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // The Mirage Arena event menu opens over the Hub field, so
    // it precedes the field branch like the menus above.
    if (EventMenuUp()) {
        char row[96]; EventMenuRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Mirage Arena. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // The world map: InField() excludes it, so it needs its own
    // branch — its two sub-menus first (they own the screen
    // while up), then the map form naming the selected world.
    if (OnWorldMap()) {
        if (VisitDialogUp()) {
            VisitArrivalLine(line, sizeof(line));
            Say(line, "menu", oga::Priority::High);
            return;
        }
        if (MapPauseUp()) {
            char row[96]; MapPauseRow(row, sizeof(row));
            snprintf(line, sizeof(line), "Paused. %s.", row);
            Say(line, "menu", oga::Priority::High);
            return;
        }
        WorldMapLine(line, sizeof(line));
        Say(line, "status", oga::Priority::High);
        return;
    }
    // The Trinity Archives: a child list first (it owns the
    // screen while open), then the top level. No world is loaded
    // here, so these precede the field branch like the menus
    // above; the title gate is closed inside the Archives (even
    // phase, submenu word set), so nothing is lost by the order.
    if (TheaterUp()) {
        char row[96]; TheaterRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Trinity Archives. Theater. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (ArchReportsUp()) {
        char row[96]; ArchReportsRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Trinity Archives. Secret Reports. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (ArchivesUp()) {
        char row[96]; ArchivesRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Trinity Archives. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (InField()) {
        char loc[96] = "";
        LocationClause(loc, sizeof(loc));
        char pos[48] = "";
        PositionClause(pos, sizeof(pos));
        FieldLine(line, sizeof(line));
        if (ActorIsTutorial()) g_lastDeck = (int) u32(kDeckCursorAddr);
        if (loc[0] || pos[0]) {
            char full[384];
            snprintf(full, sizeof(full), "%s%s%s", loc, pos, line);
            Say(full, "status", oga::Priority::High);
            return;
        }
        Say(line, "status", oga::Priority::High);
        return;
    }
    if (OnDifficulty()) {
        char row[96]; DiffRow(row, sizeof(row));
        snprintf(line, sizeof(line), "New Game. Select a game mode. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (OnTitleMenu()) {
        char row[96]; TitleRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Title menu. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    Say("Kingdom Hearts: Birth by Sleep. This screen is not mapped yet.",
        "status", oga::Priority::High);
}

void CmdMenuNav(void)
{
    // The host sends nav commands alongside the player's D-pad taps; the
    // adapter re-reads the cursor and speaks the row it LANDED on, the
    // same convention as the MK adapter. Untracked screens stay silent.
    char row[96];
    if (SaveLoadUp()) {
        // The savedata dialog's cursor is echo-tracked (it lives
        // nowhere in RAM); CmdSaveLoadNav moved it and spoke the
        // landed row. Nothing else may answer for this screen.
        return;
    }
    CampTrack();
    if (CampActive() || ReportsUp()) { CampRowFor(g_campScreen, row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    // The game-over and movie-pause menus own the D-pad while
    // their gates verify (same placement rule as WhereAmI: both
    // precede the field branch).
    if (GameOverUp())  { GameOverRow(row, sizeof(row));  Say(row, "menu", oga::Priority::High); return; }
    if (MoviePauseUp()) { MoviePauseRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (EventMenuUp()) { EventMenuRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (VisitDialogUp()) { VisitRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (MapPauseUp()) { MapPauseRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (TheaterUp()) { TheaterRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (ArchReportsUp()) { ArchReportsRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (ArchivesUp()) { ArchivesRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (InField()) {
        // Deck echo, CHANGE-ONLY: the cursor word reads 0 while the
        // deck is dormant and does not move, so a nav command that
        // changed nothing stays silent (hunts 12/25: D-pad taps in
        // the dormant window moved nothing). A real cycle speaks the
        // landed command. The deck word is verified only on the
        // tutorial lineage; a relocated actor stays silent here.
        if (!ActorIsTutorial()) return;
        int idx = (int) u32(kDeckCursorAddr);
        // First sighting proves nothing moved. Seed it silently, so the first
        // D-pad tap before any other query cannot announce a command that was
        // already highlighted (the deck word reads 0 while it is dormant).
        if (g_lastDeck < 0) { g_lastDeck = idx; return; }
        if (idx != g_lastDeck) {
            g_lastDeck = idx;
            DeckName((uint32_t) idx, row, sizeof(row));
            Say(row, "deck", oga::Priority::High);
        }
        return;
    }
    // Field first (above): if the actor is alive we are in the field,
    // even if the title structures still hold stale-but-valid values.
    if (OnDifficulty()) { DiffRow(row, sizeof(row));  Say(row, "menu", oga::Priority::High); return; }
    if (OnTitleMenu())  { TitleRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
}

void CmdNotMapped(void)
{
    Say("Not mapped yet in this game.", "status", oga::Priority::High);
}

// ---- the anchor scan (DumpState) ---------------------------------------------
//
// The game's own words, in the game's own spelling, hunted in user RAM as
// candidate text buffers for the next mapping passes. A hit is a CANDIDATE,
// never a proven structure — candidates get confirmed against the screen
// before anything speaks from them (the two cursors above were).

const char* const kAnchors[] = {
    "KINGDOM", "NEW GAME", "CONTINUE", "INSTALL GAME",
    "Beginner Mode", "Standard Mode", "Proud Mode", "Critical Mode",
    "Terra", "Ventus", "Aqua", "COMMAND",
};
constexpr int kAnchorCount = (int) (sizeof(kAnchors) / sizeof(kAnchors[0]));
constexpr int kMaxLoggedPerAnchor = 8;

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
             "[khbbs] Kingdom Hearts: Birth by Sleep (ULUS10505) anchor scan, user RAM 0x%08X-0x%08X",
             kUserRamBase, kUserRamEnd);
    Log(line);

    // The movement-derived facing, when a leg has been tracked:
    // the bench's verification surface for the directions work
    // (surveys read it alongside the position clause).
    float fdx, fdz;
    if (FacingVector(&fdx, &fdz)) {
        snprintf(line, sizeof(line),
                 "[khbbs] facing (%.2f, %.2f) (movement-derived)", fdx, fdz);
        Log(line);
    }

    int total = 0;
    int counts[kAnchorCount][2];
    int logged[kAnchorCount][2];
    for (int a = 0; a < kAnchorCount; a++)
        counts[a][0] = counts[a][1] = logged[a][0] = logged[a][1] = 0;

    for (uint32_t addr = kUserRamBase; addr < kUserRamEnd; addr++) {
        uint8_t c = u8(addr);
        if (c < 0x41 || c > 0x5A) continue;
        for (int a = 0; a < kAnchorCount; a++) {
            if (c != (uint8_t) kAnchors[a][0]) continue;
            if (MatchAscii(addr, kAnchors[a])) {
                counts[a][0]++; total++;
                if (logged[a][0] < kMaxLoggedPerAnchor) {
                    logged[a][0]++;
                    snprintf(line, sizeof(line), "[khbbs] \"%s\" ascii @ 0x%08X",
                             kAnchors[a], addr);
                    Log(line);
                }
            }
            if (u8(addr + 1) == 0 && MatchUtf16Le(addr, kAnchors[a])) {
                counts[a][1]++; total++;
                if (logged[a][1] < kMaxLoggedPerAnchor) {
                    logged[a][1]++;
                    snprintf(line, sizeof(line), "[khbbs] \"%s\" utf16 @ 0x%08X",
                             kAnchors[a], addr);
                    Log(line);
                }
            }
        }
    }

    for (int a = 0; a < kAnchorCount; a++) {
        if (!counts[a][0] && !counts[a][1]) continue;
        snprintf(line, sizeof(line), "[khbbs] \"%s\": %d ascii, %d utf16",
                 kAnchors[a], counts[a][0], counts[a][1]);
        Log(line);
    }
    snprintf(line, sizeof(line), "[khbbs] scan done: %d hits", total);
    Log(line);
}

// ---- field dialogue boxes (VERIFIED 2026-10-24, bench dlg1-3) ------
//
// Field text boxes (NPC talk, examines, the tutorial's prompt and
// info boxes) are issued by event scripts as MESSAGE COMMANDS. The
// displayed text itself never sits in a live buffer the adapter
// could read: it lives, encoded, in the area's message table (a
// "CTD" container, below) and the game typesets it on demand.
//
// The live signal is the event system's command-record pool. The
// records are 0x70 bytes apart in a heap pool whose base floats per
// area and boot (bench: 0x099A04B0.. for the plateau's first lesson
// set, 0x099A5A20.. after the lesson-2 teleport, same lineage). One
// record, offsets from its base:
//   +0x00  pointer to the record's own +0x24 word (self-check)
//   +0x24  the current command's first parameter — for a message
//          command, the message id
//   +0x28  second parameter (5 on the tutorial's messages, 0 on
//          talk lines; not part of the gate)
//   +0x2C  activation serial: 0xFFFFFFFF while the record is
//          dormant; when a message command ACTIVATES, exactly one
//          record's serial becomes a small positive number from a
//          global counter (bench lineage: ring prompt 3, prizes
//          info 5, Command Deck info 6). The id stays in the record
//          for the whole display and the record is recycled when
//          the script moves on, so a NEW (record, serial) pair is
//          exactly "a new box just opened". Successive boxes in one
//          conversation are successive commands: each activation is
//          spoken once, in serial order. What reads as "pages" of
//          one box is either the box's full text (shown at once,
//          up to three lines) or the next command's box.
// The pool holds dormant copies of every message the area's scripts
// can show (several records share an id), so the serial is the
// load-bearing field: dormant records never carry one.
//
// The text comes from the resident message tables. A CTD sits in
// RAM with magic 0x44544340 ("@CTD"); header words give the entry
// range (+0x10 start, always 0x20; +0x14 end) and the string pool
// start (+0x18). Entries are 12 bytes {id, offset, stringIndex};
// the string at table+offset is up to 3 NUL bytes, then ASCII with
// escapes, NUL-terminated: 0x0A is a newline, 0xF9 + a letter is a
// color/format code (both bytes are formatting, never text), and
// 0xF1 + a code byte is a button/control icon, decoded through
// kDlgIcons (every entry there is proven against a screenshot of
// the actual box; an unproven code is skipped, never guessed). A
// string carrying any other byte fails closed — the US text is
// plain ASCII plus those escapes, so an unexpected byte means the
// id resolved to something that is not field dialogue (one such
// leftover entry holds Shift-JIS bytes and stays silent).
//
// The INFORMATION banner is NOT this channel (bench dlg1): while
// the banner ("Attack the single hanging ring!") was up, the pool
// was already freed and the objective's message id appeared
// nowhere in RAM outside the table itself — the banner is the
// objective HUD and stays unspoken. Voiced movie scenes are a
// different system too: a playing scene's dump (bench mp1) holds
// no command records at all, so this gate cannot fire there.
//
// Speech shape was checked against the corpus before wiring: in
// every bench dump with a box on screen exactly one record carried
// a serial, and its id's text matched the screenshot word for word
// (ring prompt, prizes info, Command Deck info). A resolved line
// must also look like dialogue — at least a few characters, with a
// space or sentence punctuation — so a stray record whose
// parameter happens to be a name-table id (the resident tables
// include command/keyblade name lists) cannot speak a lone name.

constexpr uint32_t kDlgCtdMagic = 0x44544340;  // "@CTD"
constexpr int kDlgMaxTables = 24;
constexpr int kDlgMaxIds = 8192;
constexpr int kDlgMaxSlots = 96;
constexpr uint32_t kDlgSerialDormant = 0xFFFFFFFFu;

uint32_t g_dlgTableBase[kDlgMaxTables];
int g_dlgTableCount = 0;
uint32_t g_dlgIds[kDlgMaxIds];
uint32_t g_dlgStrAddr[kDlgMaxIds];   // absolute address of each id's string
int g_dlgIdCount = 0;
bool g_dlgTablesValid = false;
uint8_t g_dlgWorldAtCensus = 0xFF, g_dlgRoomAtCensus = 0xFF;

struct DlgSlot { uint32_t idAddr; uint32_t id; uint32_t serial; uint32_t consumed; };
DlgSlot g_dlgSlots[kDlgMaxSlots];
int g_dlgSlotCount = 0;
int g_dlgSweepFrames = 1 << 30;   // frames since the last census sweep
uint32_t g_dlgCandSlot = 0, g_dlgCandId = 0, g_dlgCandSerial = 0;
int g_dlgCandStable = 0;             // consecutive frames the candidate held

const struct { uint8_t code; const char* name; } kDlgIcons[] = {
    { 0xAE, "Triangle" },  // deck info box screenshot, 2026-10-24
    { 0xB0, "Square" },    // "Use <B0> to defend yourself... block if
                           // stationary, slide if moving" — BBS's Square;
                           // font order AE/B0/B1 = triangle/square/cross
    { 0xB1, "Cross" },     // ring prompt screenshot: "Press <B1> to attack"
    { 0xB3, "R" },         // lock-on info screenshot (hunt49): "switch
    { 0xB4, "L" },         // targets using <B4> or <B3>" — the glyphs
                           // are lettered L then R, in string order
    { 0xC5, "D-pad" },     // deck info box screenshot: "Use <C5> to
                           // select one" — pad glyph with up/down arrows
};

// Decode the message string at `addr` into `out` (NUL-terminated,
// escapes resolved, newlines folded to spaces, runs of spaces
// collapsed). Returns false — fail closed — on any byte outside
// the proven encoding, or if the result is empty.
bool DlgDecode(uint32_t addr, char* out, size_t n)
{
    size_t len = 0;
    // Up to 3 leading NUL bytes precede the text (the entry's
    // staggered variant offsets can also land inside them).
    for (int i = 0; i < 4 && u8(addr) == 0; i++) addr++;
    bool pendingSpace = false;
    for (int i = 0; i < 600; i++) {
        uint8_t b = u8(addr++);
        if (b == 0) break;
        if (b == 0x0A) { pendingSpace = len > 0; continue; }
        if (b == 0xF9) { addr++; continue; }        // color/format pair
        const char* word = nullptr;
        char single = 0;
        if (b == 0xF1) {                            // icon pair
            uint8_t code = u8(addr++);
            for (size_t k = 0; k < sizeof(kDlgIcons) / sizeof(kDlgIcons[0]); k++)
                if (kDlgIcons[k].code == code) word = kDlgIcons[k].name;
            if (!word) continue;                    // unproven icon: skip the pair
        } else if (b >= 0x20 && b < 0x7F) {
            single = (char) b;
        } else {
            return false;                           // not proven text encoding
        }
        if (pendingSpace && len > 0) {
            if (len + 1 >= n) return false;
            out[len++] = ' ';
            pendingSpace = false;
        }
        if (word) {
            size_t wl = strlen(word);
            if (len + wl >= n) return false;
            memcpy(out + len, word, wl);
            len += wl;
        } else if (single == ' ') {
            pendingSpace = len > 0;
        } else {
            if (len + 1 >= n) return false;
            out[len++] = single;
        }
    }
    out[len] = 0;
    return len > 0;
}

// A line worth speaking: long enough to be a sentence fragment and
// carrying a space or terminal punctuation (see the header note —
// this is what keeps a lone name-table word out).
bool DlgSpeakable(const char* s)
{
    size_t len = strlen(s);
    if (len < 4) return false;
    if (strchr(s, ' ')) return true;
    char last = s[len - 1];
    return last == '.' || last == '!' || last == '?';
}

// One table's entry range, parsed from its header. Adds plausible
// entries to the id map (first table to claim an id wins — the
// resident duplicates are byte-identical copies).
void DlgAddTable(uint32_t base)
{
    uint32_t estart = u32(base + 0x10), eend = u32(base + 0x14);
    uint32_t pool = u32(base + 0x18);
    if (estart != 0x20 || eend <= estart || ((eend - estart) % 12) != 0) return;
    if (pool < eend || pool >= 0x40000) return;
    int count = (int) ((eend - estart) / 12);
    if (count < 3 || count > 4096) return;
    // Text-table check: at least half the entries must start on
    // plausible message text (the container magic is shared with
    // non-text files; their "entries" point at binary payload).
    int checked = 0, ok = 0;
    for (int i = 0; i < count; i++) {
        uint32_t off = u32(base + estart + (uint32_t) i * 12 + 4);
        if (off < pool || off >= 0x40000) continue;
        uint32_t a = base + off;
        for (int z = 0; z < 4 && u8(a) == 0; z++) a++;
        uint8_t b = u8(a);
        checked++;
        if ((b >= 0x20 && b < 0x7F) || b == 0xF9 || b == 0xF1 || b == 0x0A) ok++;
    }
    if (checked == 0 || ok * 2 < checked) return;
    if (g_dlgTableCount < kDlgMaxTables) g_dlgTableBase[g_dlgTableCount++] = base;
    for (int i = 0; i < count && g_dlgIdCount < kDlgMaxIds; i++) {
        uint32_t id = u32(base + estart + (uint32_t) i * 12);
        uint32_t off = u32(base + estart + (uint32_t) i * 12 + 4);
        if (!id || off < pool || off >= 0x40000) continue;
        bool dup = false;
        for (int j = 0; j < g_dlgIdCount; j++) if (g_dlgIds[j] == id) { dup = true; break; }
        if (dup) continue;
        g_dlgIds[g_dlgIdCount] = id;
        g_dlgStrAddr[g_dlgIdCount] = base + off;
        g_dlgIdCount++;
    }
}

void DlgRebuildTables(void)
{
    g_dlgTableCount = 0;
    g_dlgIdCount = 0;
    for (uint32_t a = kUserRamBase; a + 0x20 <= kUserRamEnd; a += 4)
        if (u32(a) == kDlgCtdMagic) DlgAddTable(a);
    g_dlgTablesValid = g_dlgIdCount > 0;
    g_dlgWorldAtCensus = u8(kWorldAddrA);
    g_dlgRoomAtCensus = u8(kWorldAddrA + 1);
}

// Census the command-record pool: records whose base word points
// at their own +0x24 word, with a resolvable message id there.
// A record that was already censused keeps its consumed serial:
// activation tracking is PER RECORD (a record speaks the serial
// it currently carries exactly once, whatever the global counter
// does — records recycle after every box, and a save load can
// restart the counter, so no global ordering is trustworthy).
void DlgCensus(void)
{
    DlgSlot prev[kDlgMaxSlots];
    int prevCount = g_dlgSlotCount;
    for (int i = 0; i < prevCount; i++) prev[i] = g_dlgSlots[i];
    g_dlgSlotCount = 0;
    for (uint32_t a = 0x09800000; a + 0x30 <= kUserRamEnd && g_dlgSlotCount < kDlgMaxSlots; a += 4) {
        if (u32(a) != a + 0x24) continue;
        uint32_t id = u32(a + 0x24);
        bool known = false;
        for (int j = 0; j < g_dlgIdCount; j++) if (g_dlgIds[j] == id) { known = true; break; }
        if (!known) continue;
        DlgSlot* s = &g_dlgSlots[g_dlgSlotCount];
        s->idAddr = a + 0x24;
        s->id = id;
        s->serial = u32(a + 0x2C);
        s->consumed = kDlgSerialDormant;
        for (int i = 0; i < prevCount; i++)
            if (prev[i].idAddr == s->idAddr && prev[i].id == id) {
                s->consumed = prev[i].consumed;
                break;
            }
        g_dlgSlotCount++;
    }
}

bool DlgTablesStale(void)
{
    if (!g_dlgTablesValid) return true;
    if (u8(kWorldAddrA) != g_dlgWorldAtCensus ||
        u8(kWorldAddrA + 1) != g_dlgRoomAtCensus) return true;
    for (int i = 0; i < g_dlgTableCount; i++)
        if (u32(g_dlgTableBase[i]) != kDlgCtdMagic) return true;
    return false;
}

// A field dialogue box is currently activated (a census record
// carries a serial). Used by ready(); speech itself is edge-driven
// in WatchDialogue.
bool DialogueUp(void)
{
    for (int i = 0; i < g_dlgSlotCount; i++)
        if (g_dlgSlots[i].serial != kDlgSerialDormant) return true;
    return false;
}

void DlgReset(void)
{
    g_dlgTableCount = 0; g_dlgIdCount = 0; g_dlgTablesValid = false;
    g_dlgSlotCount = 0; g_dlgSweepFrames = 1 << 30;
    g_dlgCandSlot = 0; g_dlgCandId = 0; g_dlgCandSerial = 0; g_dlgCandStable = 0;
}

void WatchDialogue(void)
{
    if (g_dlgSweepFrames < (1 << 30)) g_dlgSweepFrames++;
    // Other screens own speech while they are up; a field dialogue
    // cannot activate under them, and the pool census would only
    // find their furniture. (The movie-scene system never touches
    // this channel at all — see the header note.)
    if (CampActive() || SaveLoadUp() || ArchivesUp() || TheaterUp() ||
        ArchReportsUp() || GameOverUp() || MoviePauseUp() || EventMenuUp() ||
        OnWorldMap() || OnTitleMenu() || OnDifficulty())
        return;
    // Census sweep on a frame cadence (the first frame after
    // attach sweeps immediately, so a box already up speaks
    // promptly); between sweeps the known records are polled
    // every frame, which is a handful of reads.
    if (g_dlgSweepFrames >= 45) {
        g_dlgSweepFrames = 0;
        if (DlgTablesStale()) DlgRebuildTables();
        if (g_dlgTablesValid) DlgCensus();
    }
    // Poll the census records: refresh serials, drop recycled ones.
    for (int i = 0; i < g_dlgSlotCount; i++) {
        uint32_t a = g_dlgSlots[i].idAddr;
        if (u32(a - 0x24) != a || u32(a) != g_dlgSlots[i].id) {
            g_dlgSlots[i] = g_dlgSlots[--g_dlgSlotCount];
            i--;
            continue;
        }
        g_dlgSlots[i].serial = u32(a + 8);
    }
    // Pick the earliest unconsumed activation.
    int best = -1;
    for (int i = 0; i < g_dlgSlotCount; i++) {
        uint32_t s = g_dlgSlots[i].serial;
        if (s == kDlgSerialDormant || s == g_dlgSlots[i].consumed) continue;
        if (best < 0 || s < g_dlgSlots[best].serial) best = i;
    }
    if (best < 0) { g_dlgCandSlot = 0; g_dlgCandStable = 0; return; }
    DlgSlot* c = &g_dlgSlots[best];
    if (c->idAddr == g_dlgCandSlot && c->id == g_dlgCandId &&
        c->serial == g_dlgCandSerial) {
        g_dlgCandStable++;
    } else {
        g_dlgCandSlot = c->idAddr; g_dlgCandId = c->id; g_dlgCandSerial = c->serial;
        g_dlgCandStable = 1;
    }
    if (g_dlgCandStable < 2) return;   // one-frame ghosts don't speak
    // Resolve and speak. Even an unresolvable or unspeakable line
    // consumes the activation: the box was real, its text just is
    // not provable, and re-trying every frame would only churn.
    c->consumed = c->serial;
    g_dlgCandSlot = 0; g_dlgCandStable = 0;
    uint32_t strAddr = 0;
    for (int j = 0; j < g_dlgIdCount; j++)
        if (g_dlgIds[j] == c->id) { strAddr = g_dlgStrAddr[j]; break; }
    if (!strAddr) return;
    char line[512];
    if (DlgDecode(strAddr, line, sizeof(line)) && DlgSpeakable(line))
        Say(line, "dialogue", oga::Priority::High);
}

} // namespace

static bool khbbs_attach(const Host* host)
{
    g_host = host;
    g_actorBase = 0; g_lastScanMs = 0; g_charId = -1; g_lastLock = -1; g_lockRec = 0; g_nodeBase = 0; g_nodeActor = 0; g_lastNodeScanMs = 0;  // actor + character located per boot
    g_lastLockScanMs = 0;
    g_lastDeck = -1;
    g_lowHpWarned = false; g_lowHpActor = 0; g_lowHpMax = 0;
    g_lastActorBase = 0; g_goStruct = 0; g_lastGoScanMs = 0; g_goWasUp = false; g_goDownFrames = 1 << 30; g_mpUp = false;  // game-over / movie-pause gates
    g_evUp = false; g_evDownFrames = 1 << 30;  // Mirage Arena event menu gate
    g_visitObj = 0; g_lastVisitScanMs = 0; g_wmUp = false; g_wmSel = -1; g_wmSelCand = -1; g_wmSelFrames = 0; g_wmDlgUp = false; g_wmDlgDownFrames = 1 << 30; g_wmPauseUp = false;  // world map
    g_archTopUp = false; g_archTopStable = 0; g_theaterUp = false; g_theaterStable = 0; g_reportsUp = false; g_reportsStable = 0;  // trinity archives
    SlReset();  // save/load dialog
    DlgReset();  // field dialogue
    g_campScreen = CampScreen::None;
    g_lastMain = g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
    g_lastSlotRaw = -1;
    FaceReset(); g_faceNode = 0;
    return true;   // the game code check already happened in the registry
}

// The low-health watch (see the kLowHpPct note for the game's own
// danger line and its proof). Runs every frame the field actor is
// live. The latch is keyed to the actor and max HP it was set
// under, so a new boot, save, or character re-arms it; leaving the
// field leaves it untouched, so walking through a door at low
// health does not re-warn on the other side. Death is the
// game-over flow's territory, not this watch's: ActorPlausible
// already fails at cur HP 0 (so InField drops and the game-over
// menu owns the screen), and the cur == 0 guard below covers a
// same-frame race — it clears the latch silently and never speaks,
// so a retry at full health (or a load back into low health)
// starts from a sane armed state. The camp menu and Reports sit
// ON TOP of the field with live HP words: the latch still tracks
// there (a potion used in camp re-arms silently), but the spoken
// line only fires with the field in front — a warning first
// noticed under a menu would be menu noise. HP does not drop
// during this game's cutscenes, and a player who enters one
// already low was warned during play, so the field gate is the
// cutscene gate too. The line is High priority: the latch and the
// re-arm gap already make chatter impossible, and High keeps a
// re-armed warning from being swallowed by the consecutive-line
// dedup that status echoes go through.
void WatchLowHp(void)
{
    unsigned cur = (unsigned) u16(g_actorBase + kActorOffCurHp);
    unsigned mx = (unsigned) u16(g_actorBase + kActorOffMaxHp);
    if (g_actorBase != g_lowHpActor || mx != (unsigned) g_lowHpMax) {
        g_lowHpActor = g_actorBase;
        g_lowHpMax = (uint16_t) mx;
        g_lowHpWarned = false;
    }
    if (mx == 0) return;
    if (cur == 0) { g_lowHpWarned = false; return; }  // death: silent
    int pct = (int) ((cur * 100) / mx);
    if (g_lowHpWarned) {
        if (pct >= kLowHpRearmPct) g_lowHpWarned = false;  // recovered: re-arm
        return;
    }
    if (pct < kLowHpPct) {
        g_lowHpWarned = true;
        if (!CampActive() && !ReportsUp())
            Say("Low health.", "health", oga::Priority::High);
    }
}

static void khbbs_on_frame(void)
{
    // Follow the camp screens every frame: CampTrack keys on
    // TRANSITIONS (the slot word's garbage phase, cursor moves),
    // and a player who dives several screens between two spoken
    // commands would otherwise skip the intermediate values the
    // tracker needs to see (live13: a silent four-Cross dive left
    // the tracker stranded at the decks submenu). CampActive()
    // gates the cost: outside the camp menu this is two word reads.
    if (!SaveLoadUp() && (CampActive() || ReportsUp())) CampTrack();
    // The game-over and movie-pause arrival watches: both menus
    // appear without a command from the player (death, a START
    // press the host doesn't translate), so their arrival lines
    // hang on on_frame's rising edge, not on a command. Both
    // gates fail cheap outside their screens.
    WatchGameOver();
    WatchMoviePause();
    // The Mirage Arena event menu's arrival watch (the Log In
    // console in the Hub lobby). Same rising-edge pattern; its
    // gate fails cheap outside the Hub.
    WatchEventMenu();
    // The save/load dialog's arrival and completion watch. The
    // dialog also appears without a translated command (it is
    // the firmware utility, opened from menus the host drives
    // with raw buttons), so its lines hang on the same edge.
    WatchSaveLoad();
    // The world map's arrival, selection and sub-menu watches.
    // Runs before the field gate below: the map is excluded
    // from InField, so this is the only speech it gets.
    WatchWorldMap();
    // The Trinity Archives arrival watches (top level and the
    // two wired child lists). Also before the field gate: no
    // world is loaded while the Archives own the screen.
    WatchArchives();
    // Field dialogue boxes: the arrival watch for the event
    // system's message commands (bench dlg1-3). Runs with the
    // other watches; its own gate keeps it out of every screen
    // that owns speech, and the movie-scene system never touches
    // its channel.
    WatchDialogue();
    // Lock-on edges (VERIFIED 2026-10-18, battle30/31): the player
    // actor's spawn-mark word reads 2 exactly while a lock-on is
    // held and 1 otherwise, across lock, unlock, and relock tests.
    // Announce acquiring a lock; losing one stays silent (it is
    // usually deliberate, and the field line reports current state).
    if (!InField()) { g_lastLock = -1; g_lockRec = 0; g_lastLockScanMs = 0; g_nodeBase = 0; g_nodeActor = 0; FaceReset(); g_faceNode = 0; return; }
    FaceTrack();
    WatchLowHp();
    int lock = (u16(g_actorBase + kActorOffMark) == 2) ? 1 : 0;
    if (g_lastLock == 0 && lock == 1)
        Say("Locked on.", "lock", oga::Priority::Normal);
    g_lastLock = lock;
}

static void khbbs_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();  break;
        case Command::NextAlly:        if (InField() && !CampActive() && !ReportsUp()) CmdFoe();       else CmdNotMapped(); break;
        case Command::NextEnemy:       if (InField() && !CampActive() && !ReportsUp()) CmdLockQuery(); else CmdNotMapped(); break;
        case Command::PrevAlly:
        case Command::PrevEnemy:
        case Command::NextUnactedAlly: if (InField() && !CampActive() && !ReportsUp()) CmdSelfLine();  else CmdNotMapped(); break;
        case Command::DumpState:       CmdDump();      break;
        case Command::MenuState:       CmdWhereAmI();  break;
        case Command::MenuNext:
            if (SaveLoadUp()) { CmdSaveLoadNav(+1); break; }
            CmdMenuNav();
            break;
        case Command::MenuPrev:
            if (SaveLoadUp()) { CmdSaveLoadNav(-1); break; }
            CmdMenuNav();
            break;
        case Command::MenuLeft:
        case Command::MenuRight:
            if (SaveLoadUp()) { CmdSaveLoadFlip(cmd == Command::MenuLeft); break; }
            if (g_meldDialog) { CmdMeldFlip(); break; }
            CmdMenuNav();
            break;
        case Command::Guide:           CmdHubGuide();  break;
        case Command::OskType:
            if (SaveLoadUp()) { CmdSaveLoadCross(); break; }
            CmdMeldCross();
            break;
        case Command::OskDelete:
            if (SaveLoadUp()) { CmdSaveLoadCircle(); break; }
            CmdMeldCircle();
            break;
        // OSK and cue commands are ignored silently: this adapter is not
        // on a tracked keyboard, and adapter.h is explicit that adapters
        // in that position do exactly that.
        default: break;
    }
}

static bool khbbs_ready(void)
{
    // Ready means "the adapter currently has usable game state" — true on
    // the screens whose gates verify, using the same gates speech uses.
    return OnTitleMenu() || OnDifficulty() || InField() || CampActive() ||
           GameOverUp() || MoviePauseUp() || EventMenuUp() || OnWorldMap() || SaveLoadUp() ||
           ArchivesUp() || TheaterUp() || ArchReportsUp() || DialogueUp();
}

static void khbbs_detach(void)
{
    g_host = nullptr; g_lastDeck = -1;
    g_actorBase = 0; g_lastScanMs = 0; g_charId = -1; g_lastLock = -1; g_lockRec = 0;
    g_lowHpWarned = false; g_lowHpActor = 0; g_lowHpMax = 0;
    g_lastActorBase = 0; g_goStruct = 0; g_lastGoScanMs = 0; g_goWasUp = false; g_goDownFrames = 1 << 30; g_mpUp = false;  // game-over / movie-pause gates
    g_evUp = false; g_evDownFrames = 1 << 30;  // Mirage Arena event menu gate
    g_visitObj = 0; g_lastVisitScanMs = 0; g_wmUp = false; g_wmSel = -1; g_wmSelCand = -1; g_wmSelFrames = 0; g_wmDlgUp = false; g_wmDlgDownFrames = 1 << 30; g_wmPauseUp = false;  // world map
    g_archTopUp = false; g_archTopStable = 0; g_theaterUp = false; g_theaterStable = 0; g_reportsUp = false; g_reportsStable = 0;  // trinity archives
    SlReset();  // save/load dialog
    DlgReset();  // field dialogue
    g_lastLockScanMs = 0;
    g_nodeBase = 0; g_nodeActor = 0; g_lastNodeScanMs = 0;
    FaceReset(); g_faceNode = 0;
    g_campScreen = CampScreen::None;
    g_lastMain = g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
}

static bool khbbs_cue_snapshot(CueSnapshot* out)
{
    // The beacon's data feed (docs/design/cue-synth.md): silent,
    // read-only, polled every frame by the host's cue synth once
    // the adapter has attached. The synth turns (locked, dist)
    // into the centred beep whose pulse quickens as the target
    // closes; silence means nothing is locked. BBS locks only
    // ever target enemies, so is_core stays false, and there is
    // no EX-pickup analog to report, so core_gain stays false.
    // "battle" here means the field actor is live: BBS has no
    // separate battle screen — enemies roam the field itself —
    // and the synth is silent for battle-without-lock anyway.
    // Fail closed exactly like the speech path: every field is
    // cleared up front, and a held lock whose target cannot be
    // verified reports no lock rather than a distance nobody
    // checked.
    if (!out) return false;
    out->battle = false;
    out->locked = false;
    out->is_core = false;
    out->dist = 0.0f;
    out->core_gain = false;
    if (!g_host || !InField() || CampActive() || ReportsUp()) return false;
    out->battle = true;
    if (u16(g_actorBase + kActorOffMark) != 2) return true;
    unsigned foeCur, foeMx;
    float dist, dy;
    if (!ReadFoe(&foeCur, &foeMx, &dist, &dy)) return true;
    out->locked = true;
    out->dist = dist;
    return true;
}

// NOTE: the instance lives at oga scope (like kMortalKombatUnchained), NOT
// inside an anonymous namespace — otherwise the registry's
// `oga::kKingdomHeartsBirthBySleep` declaration will not link.
extern const Adapter kKingdomHeartsBirthBySleep;
const Adapter kKingdomHeartsBirthBySleep = {
    "khbbs",
    "Kingdom Hearts: Birth by Sleep",
    "ULUS10505",            // PSP game code for Kingdom Hearts: Birth by Sleep (USA), from PARAM.SFO
    khbbs_attach,
    khbbs_on_frame,
    khbbs_command,
    khbbs_ready,
    khbbs_detach,
    khbbs_cue_snapshot,
    "Title and difficulty menu speech; Trinity Archives speech; camp menu speech; field health, command and gauge speech; low-health warning; game-over menu speech; movie pause menu speech; world map speech; Mirage Arena event menu speech; save and load screen speech; lock-on beacon cue; Mirage Arena Hub landmark guide",
    "Spoken: the title menu (New Game, Continue, Install Game — plus "
    "Trinity Archives once clear data exists) and the New Game "
    "difficulty select. The Trinity Archives speak too: the top "
    "level (Timeline, Theater, Game Stats, Trophies, Secret "
    "Reports), the Theater story list (Terra, Ventus, Aqua, Final "
    "Episode, Where the Heart Goes, Blank Points), and the Secret "
    "Reports list (Xehanort's Letter, then the numbered reports). "
    "Inside Timeline, Game Stats and Trophies the screens are a "
    "chart, a progress page and a picture grid, and are not "
    "spoken. In the field, Where Am I speaks your health, "
    "the currently selected deck command, and the Command Gauge. It "
    "also names the world and the room you are in (for example, "
    "Radiant Garden, Central Square), and speaks your position "
    "as two numbers, your coordinates within the room. The "
    "player's field data is located fresh each boot; on boots where "
    "the deck layout is not the verified one, Where Am I speaks "
    "health and gauge only. When your health falls into the "
    "danger zone — below a quarter of your health bar, the same "
    "point where the game itself switches the gauge to its "
    "low-health state — the reader says \"Low health.\" once. "
    "It stays quiet while you remain low, and warns again only "
    "after you have healed well clear of the zone and dropped "
    "back in. When you die, the game-over menu is spoken: "
    "\"Game over,\" then the highlighted choice — Continue, "
    "Retry, or Load Game. Pausing a cutscene with Start is "
    "spoken too: Resume or Skip Scene. On the world map, "
    "arriving is announced, the world you fly to is spoken by "
    "name, the visit dialog (Visit This World or Leave) is "
    "spoken, and the map's own Start menu (Resume, Open Menu, "
    "Save) is spoken. Saving and loading are spoken too: the "
    "Save and Load screens announce themselves, each slot is "
    "spoken by its number and position (\"Slot 4 of 200\"), "
    "saving over an existing file asks \"Overwrite this "
    "save?\" with No as the safe default, a save in progress "
    "says \"Saving.\", and a finished save says \"Saved.\" "
    "Two honest limits: a slot's stored details — its "
    "character, location, level, munny and play time — are "
    "read by the system straight off the memory stick and "
    "never pass through memory the reader can see, so slots "
    "are spoken by number, not by contents; and the Load "
    "screen opens on your most recent save, which the reader "
    "cannot identify until you move, so on that screen rows "
    "are named once your position is certain (walking to "
    "either end of the list settles it). The camp menu (opened "
    "with Start in the field) is spoken: "
    "Command Decks, Items, Abilities, D-Links, Stats, Reports, "
    "Command Board, Config, Game Help.\n"
    "Controls: Cross executes the Attack row. The Command Deck "
    "(Quick Blitz, Aero, Strike Raid at the start) is cycled with the "
    "D-pad, and the highlighted command is executed with Triangle. The "
    "deck selection is spoken only when it actually changes.\n"
    "Also spoken: the camp menu's Command Decks tree — the Decks "
    "submenu, the Edit Deck group focus (Battle Commands, Action "
    "Commands, Shotlock Command), the deck slot list, and the "
    "command inventory by name: each row speaks its command, its "
    "level (or how many you hold, for items), and any ability it "
    "carries, following the screen's filter tabs (All, Battle, "
    "Magic, Items, Friendship).\n"
    "Lock-on: acquiring a lock (L and R together) is announced. "
    "While locked on, Where Am I also reports the target's "
    "health and how far away it is, and two queries answer on "
    "demand: Next Ally speaks the foe (its health, distance, and "
    "whether it is above or below you), and Next Enemy speaks "
    "the lock state. The target's name is not read yet.\n"
    "Mirage Arena: in the Hub, the Guide control reads the "
    "landmarks on your floor of the room. On the entrance "
    "platform it names the save point and the transporter; in "
    "the lobby, the arena terminal, the Moogle Medal Shop, "
    "the Log In terminal and the transporter back. Each comes "
    "with its distance, and "
    "once you have walked a step and the reader knows which "
    "way you are facing, a clock direction too (straight "
    "ahead, 3 o'clock, and so on). A bearing is not a route: "
    "the two floors are joined only by the transporter, so "
    "the other floor's landmarks are never named. The Log In "
    "terminal's own menu is spoken too — \"Mirage Arena,\" "
    "then New Event, Active Events, or D-Links. Choosing New "
    "Event opens a list of event types (Versus Mode, Arena "
    "Mode, Command Board, Rumble Racing) that is not spoken "
    "yet.\n"
    "Battle cue: with the app's battle cues turned on in "
    "Settings, locking on also starts the beacon, a low beep "
    "that pulses faster as your target gets closer and stops "
    "when the lock ends. No lock, no beep.\n"
    "Not mapped yet: the other camp sub-screens (Items, Abilities, "
    "and the rest), and battle state beyond the field readouts "
    "above. The tutorial is still being mapped.\n"
    "Supported version: Kingdom Hearts: Birth by Sleep, USA retail "
    "v1.00 (ULUS10505). Other pressings are not verified.",
};

} // namespace oga
