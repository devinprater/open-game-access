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
 *     extended 2026-10-24 to the surveyed Olympus Coliseum and
 *     Radiant Garden rooms (CmdRoomGuide),
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

// ---- The sound-first HUD seam (2026-10-24) -----------------------------------
// The owner's sound-first rule splits the silent HUD events in two: BIG
// state changes keep their speech (a D-Link activating, a link ending),
// while GAUGE-FULL moments (Focus full, D-Link ready) become designed
// sounds instead of spoken lines. Which half a player gets is a
// PREFERENCE, so the split is switchable here: true (the default) =
// gauge-full moments raise cue-synth earcon pulses through the cue
// snapshot and stay unspoken; false = the spoken lines come back and no
// pulses are raised.
//
// This flag is THE SEAM the future per-game settings panel binds to:
// the panel's "HUD gauge cues: sounds / speech" control for this game
// calls KhbbsHudGaugeCues() with the player's choice (the app-side
// settings plumbing does not exist yet — this is the adapter end of
// it, and the host tests drive it directly). It lives at oga scope,
// outside the adapter's anonymous namespace, so other translation
// units can reach it; it is deliberately NOT reset by attach/detach —
// a setting is not session state.
bool g_hudGaugeCuesSound = true;
void KhbbsHudGaugeCues(bool useSound) { g_hudGaugeCuesSound = useSound; }

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

// ---- battle gauges: Focus / shotlock + D-Link (VERIFIED 2026-10-24, --------
// ---- bench khbbs-foc1..foc7, decomp-led) -----------------------------------
//
// One heap object carries both battle resources. Its address
// floats per boot (0x099842F0 hub/camp lineage, 0x09994700 LoD,
// 0x099A4720 the Final Episode fight lineage), but a FIXED global
// holds it: u32 @ 0x08B73BB8 (the constructor FUN_08919c88 stores
// it there; the destructor FUN_08919cdc zeroes it). Layout, from
// decompiling its per-frame updater FUN_0891db48 and confirming
// every field live in the bench:
//   +0x3C f32  D-Link gauge, 0..100
//   +0x44 f32  Focus gauge, 0..100
//   +0x50 u32  state: 1 while shotlock aiming, 2 while a fired
//              shotlock resolves, 0 otherwise
//   +0x54 u32  flags: 0x10000 Focus is full (set by the game at
//              the fill crossing, clear while Focus < 100);
//              0x100 D-Link primed (set at the fill crossing,
//              held through a link, cleared when the drain
//              empties the gauge); 0x20000 shotlock aiming;
//              0x200 a D-Link is active
//   +0x80 u16  the active link partner's def id (family 7 sub 14,
//              the camp D-Links roster's own id set: 372 Mickey,
//              373 Donald, 374 Goofy, 375 Experiment 626,
//              376 Peter Pan, 377 Maleficent, 378 Cinderella,
//              379 Snow White, 380 Zack, 381 Pete, 382 Aqua,
//              383 Ventus, 384 Terra); 0 while unlinked. Names
//              resolve through DefName(), the camp path's table.
// The object exists wherever the player does (field, hub, camp,
// battle) — events are gated to the field picture by the watch,
// not by the object's presence.
//
// The mechanics the speech keys on (all bench-driven on the
// Radiant Garden fight, Aqua vs the Red Hot Chili):
//   * Shotlock aim = hold L+R: the aim flag rises, the actor's
//     state goes 24 and its spawn mark goes 4 (see ActorPlausible),
//     Focus drains as locks accumulate (Lightbloom caps at 20)
//     and stops at the cap. Releasing L+R CANCELS with a full
//     Focus refund; pressing Cross FIRES — state +0x50 goes 2 the
//     same frame the aim flag falls, Focus stays spent, and the
//     hits land over the following seconds. Taking damage can
//     knock the player out of the aim (a cancel, refund included).
//   * D-Link: D-pad RIGHT opens the battle list (roster, cursor),
//     Triangle confirms. Activation sets flag 0x200, refills HP
//     to full, swaps in the partner's deck and portrait, and the
//     gauge drains ~3/second; at empty the primed bit clears and
//     ~2 frames later 0x200 clears and the own deck returns.
//   * Focus refills from a budget the updater converts over time;
//     taking hits feeds it (+4 per hit observed).
// The lock-on PIP COUNT is deliberately not spoken: exact-pattern
// scans (u8/u16/u32/float) across the f0/aim45/aim120/aim240 dump
// series found no word holding 0/3/20/20 — the count the HUD draws
// is a per-target list length recomputed per frame, not a word.
// The max-lock moment has no unique memory witness either (the
// Focus drain also pauses when the reticle leaves every enemy),
// so no milestone line is wired.
constexpr uint32_t kGaugesPtrAddr = 0x08B73BB8;  // u32 -> gauges object
constexpr uint32_t kGaugesOffDLink = 0x3C;       // f32 0..100
constexpr uint32_t kGaugesOffFocus = 0x44;       // f32 0..100
constexpr uint32_t kGaugesOffState = 0x50;       // u32: 1 aim, 2 firing
constexpr uint32_t kGaugesOffFlags = 0x54;       // u32, bits below
constexpr uint32_t kGaugesOffPartner = 0x80;     // u16 def id, 0 = none
constexpr uint32_t kGaugesFlagFocusFull = 0x10000;
constexpr uint32_t kGaugesFlagDLinkPrimed = 0x100;
constexpr uint32_t kGaugesFlagAiming = 0x20000;
constexpr uint32_t kGaugesFlagLinked = 0x200;

// ---- lock-on record + locked foe (VERIFIED 2026-10-19, battle41) -----------
// The player's lock-on component record floats per boot; it is
// LOCATED by its back-pointer to the actor (see LocateLockRecord).
//   +0x38 u32 ... the player actor
//   +0x3C u32 ... the target's scene object (0 while unlocked)
//   +0x40 f32[3]  the player's position (moves as the player walks)
//   +0x50 f32[3]  the target's position (tracks it live)
// The target's entity block starts 0x1B8 BEFORE its scene object;
// enemies share the actor layout for HP (cur/max at +0x380/+0x382,
// damage-verified on the Final Episode Radiant Garden target,
// object code b51ex00, 1200 max). The scene object's first word is
// its scene id, which the actor mirrors at +0x33C — that match is
// the locator's strongest check. The scene id is a per-spawn
// INSTANCE index, not a type id; the type identity is the object
// code string embedded at scene object +0x88 (see FoeTypeName).
constexpr uint32_t kLockRecOffActor = 0x38;
constexpr uint32_t kLockRecOffTgt   = 0x3C;
constexpr uint32_t kLockRecOffPPos  = 0x40;
constexpr uint32_t kLockRecOffTPos  = 0x50;
constexpr uint32_t kFoeBlockBack    = 0x1B8;
constexpr uint32_t kFoeObjCodeOff   = 0x88;  // object code, from the scene object
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
uint32_t g_blockBase = 0;   // player-block base found alongside the character
int g_lastLock = -1;        // last polled lock state: -1 unknown, 0 off, 1 on
bool g_lowHpWarned = false; // low-health warning fired for this drop
uint32_t g_lowHpActor = 0;  // actor the low-HP latch belongs to
uint16_t g_lowHpMax = 0;    // max HP the low-HP latch belongs to
// Battle-gauges watch state (Focus / shotlock + D-Link; see the
// constants block). The primed flag is the previous frame's flag
// word; edges are state changes of the game's own bits, so nothing
// repeats without the game itself changing state first.
bool g_gaugesPrimed = false;   // first live sight primes silently
uint32_t g_gaugesFlags = 0;    // flag word at the last live sight
bool g_aimFired = false;       // a firing state seen for this aim
// Sound-first one-shot events (declared here because the gauges
// watch raises the first two; the health-cue watch further down
// raises the beeps and owns the design note). The cue snapshot
// copies them out and clears them: one event, one sound.
bool  g_pendFoeBeep = false, g_pendOwnBeep = false;
float g_pendFoeHz = 0.0f, g_pendOwnHz = 0.0f;
bool  g_pendFocusCue = false, g_pendDLinkCue = false;
// Sound-first health-cue tracking (the watch itself is further
// down, beside WatchLowHp; the design note lives with it). One
// track per side: the subject the latches belong to, its max HP,
// last frame's HP, and the four milestone latches (75/50/25/10).
struct HealthTrack {
    uint32_t who = 0;       // actor base (own) or foe entity block; 0 = none
    uint16_t max = 0;       // the max HP the latches belong to
    int prev = -1;          // last frame's HP; -1 = unprimed
    bool m75 = false, m50 = false, m25 = false, m10 = false;
};
HealthTrack g_ownTrack, g_foeTrack;
constexpr int   kHealthMilestoneRearm = 5;    // percent points above a milestone
constexpr float kFoeBeepHiHz = 1200.0f, kFoeBeepLoHz = 300.0f;
constexpr float kOwnBeepHiHz = 800.0f,  kOwnBeepLoHz = 200.0f;
constexpr float kFoeBeepPan = 0.8f, kOwnBeepPan = -0.8f;
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
// spoken as clock positions relative to this delta. The landmark
// tables — the Mirage Arena Hub first, then the surveyed Olympus
// Coliseum and Radiant Garden rooms — speak through the Guide
// query (CmdHubGuide / CmdRoomGuide below), and the tracker stays
// bench-visible via DumpState.
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
    if (found) { g_charId = (int) u8(found + kBlockOffChar); g_blockBase = found; }
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
    // made the field vanish for the whole lock-on. It reads 4
    // through the whole shotlock — the aim AND the firing
    // animation that follows it (VERIFIED 2026-10-24, bench
    // khbbs-foc1/foc2: mark 4 + actor state 24 from the aim's
    // first frame to the animation's end; the gauges object's
    // aim flag covers only the aim itself, clearing the frame
    // the shot fires while the mark is still 4) — accept it, or
    // the field gate drops for exactly the seconds the shotlock
    // watch must see. But mark 4 is NOT the player's alone: a
    // full-corpus sweep (2,760 bench dumps, offline replica of
    // this fingerprint) found mark-4 structs the old rules
    // rejected — an enemy block in the Radiant Garden fight
    // dumps (battle27/28: state 35, HP 5/512; an old=1 dump
    // would have gone ambiguous and several old=0 dumps would
    // have located the foe) and UI structs on screens with no
    // gauges object at all (ad91: state 50; arch3: state 102).
    // The player's mark 4 is therefore accepted ONLY with the
    // shotlock's own corroboration: the actor state is 24 (the
    // shotlock action state, constant through aim and firing)
    // and the battle-gauges object is live. Both held in every
    // true shotlock dump (foc1/foc2/battle40) and failed in
    // every false one.
    uint32_t state = u32(base + kActorOffState);
    uint16_t mark = u16(base + kActorOffMark);
    if (mark == 4) {
        if (state != 24) return false;
        uint32_t g = u32(kGaugesPtrAddr);
        if (g < kUserRamBase || g + 0x58 > kUserRamEnd) return false;
    } else if (mark != 1 && mark != 2) return false;
    if ((u32(base + kActorOffFlags) & 0x20) == 0) return false;
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
    g_blockBase = 0;
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

// The locked target's scene object, located and validated:
// the lock record's owner back-pointer must name the actor, its
// target word must be nonzero, and the target's first word must
// equal the actor's engagement mirror. 0 when there is no
// verifiable target. (Extracted from ReadFoe; the foe NAME read
// below shares the same validation.)
uint32_t FoeSceneObject(void)
{
    if (!g_lockRec || u32(g_lockRec + kLockRecOffActor) != g_actorBase) {
        // The record sweep is 2M reads. ReadFoe() runs inside the per-frame
        // cue snapshot, so an unlocatable record must NOT be re-swept every
        // frame: throttle it like the player-node locate does.
        uint64_t now = g_host ? g_host->now_ms : 0;
        if (g_lastLockScanMs && now >= g_lastLockScanMs &&
            now - g_lastLockScanMs < 1000)
            return 0;
        g_lastLockScanMs = now;
        g_lockRec = LocateLockRecord();
    }
    if (!g_lockRec) return 0;
    uint32_t tgt = u32(g_lockRec + kLockRecOffTgt);
    if (!tgt) return 0;
    if (u32(tgt) != u32(g_actorBase + kActorOffEngage)) return 0;
    return tgt;
}

// Validated read of the locked target: its health pair, the 3D
// distance, and the height difference (target minus player).
// Every input is re-validated on each call; any failure returns
// false and the caller degrades honestly.
bool ReadFoe(unsigned* curOut, unsigned* mxOut, float* distOut, float* dyOut)
{
    uint32_t tgt = FoeSceneObject();
    if (!tgt) return false;
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

// ---- the foe's NAME (VERIFIED 2026-10-24, bench name1-5 + the
// journal census in bench sub1/s5) ----
//
// What carries the TYPE: every scene object embeds its object
// code at +0x88 — "m03ex00" for a Bruiser, 7 chars + NUL. The
// word at +0x00 (the "scene id") cannot do it: it is a per-spawn
// instance index — the Mirage Arena lobby's two g12vs00
// transporter rings carry scene ids 20 and 21, and one fight's
// ids run in spawn order across types (player 15, gimmicks 17/18,
// the boss 19, its keyblade 21 in battle41's census).
//
// The display names are the game's own words. The journal's
// Unversed entries (message category 0x3800, name part) are
// keyed by entry = enemy number + 0x80: 0x81 Flood, 0x82
// Scrapper, 0x83 Bruiser, 0x84 Red Hot Chili, ... 0x99 Blobmob —
// the same numbering as the object codes m01..m25, in the same
// order, dumped from a Reports-open RAM image. That table is
// NOT resident during a fight (it loads with the Reports
// screen; a fight dump carries five message tables, none of
// them 0x3800), so the proven mapping ships as data here — the
// pattern the harvested shop prices already use. Staged types
// are additionally correlated live (lock the foe, screenshot its
// visible form, read its code, hear the name).
//
// Fail closed: only 'm' codes with the ex/vs infix and a proven
// number 1..25 resolve. Boss ('b') codes have no proven key —
// the journal's boss entries (0x9A..0xA4) are sequenced, not
// numbered by the b-code — and the Jellyshade variants 27/28/32
// and the arena Heartless 40/41/42 have no journal entry of
// their own; all stay unnamed rather than guessed.
static const char* const kFoeTypeNames[] = {
    nullptr,          // 0
    "Flood",          // 1
    "Scrapper",       // 2
    "Bruiser",        // 3
    "Red Hot Chili",  // 4
    "Monotrucker",    // 5
    "Thornbite",      // 6
    "Shoegazer",      // 7
    "Spiderchest",    // 8
    "Archraven",      // 9
    "Hareraiser",     // 10
    "Jellyshade",     // 11
    "Tank Toppler",   // 12
    "Vile Phial",     // 13
    "Sonic Blaster",  // 14
    "Triple Wrecker", // 15
    "Wild Bruiser",   // 16
    "Blue Sea Salt",  // 17
    "Yellow Mustard", // 18
    "Mandrake",       // 19
    "Buckle Bruiser", // 20
    "Chrono Twister", // 21
    "Axe Flapper",    // 22
    "Prize Pod",      // 23
    "Glidewinder",    // 24 (the ex01/ex02 color variants share it)
    "Blobmob",        // 25
};

// Map a raw object code to its proven display name, or nullptr.
// The code's shape is validated strictly: [a-z] digit digit
// [a-z] [a-z] digit digit NUL — anything else is not a code.
const char* FoeTypeName(const char* code)
{
    if (code[0] != 'm') return nullptr;
    if (code[1] < '0' || code[1] > '9' || code[2] < '0' || code[2] > '9')
        return nullptr;
    bool ex = code[3] == 'e' && code[4] == 'x';
    bool vs = code[3] == 'v' && code[4] == 's';
    if (!ex && !vs) return nullptr;
    if (code[5] < '0' || code[5] > '9' || code[6] < '0' || code[6] > '9' ||
        code[7] != 0)
        return nullptr;
    int num = (code[1] - '0') * 10 + (code[2] - '0');
    if (num < 1 || num > 25) return nullptr;
    return kFoeTypeNames[num];
}

// The locked foe's proven type name, read live from its scene
// object. False (speak no name) on any doubt.
bool ReadFoeName(char* buf, size_t n)
{
    uint32_t tgt = FoeSceneObject();
    if (!tgt) return false;
    char code[8];
    for (int i = 0; i < 8; i++)
        code[i] = (char) u8(tgt + kFoeObjCodeOff + (uint32_t) i);
    const char* name = FoeTypeName(code);
    if (!name) return false;
    snprintf(buf, n, "%s", name);
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

// Speak one landmark table from (px, pz): each mark with its
// distance and, once facing is known, its clock bearing. The
// spoken forms are the Guide's everywhere (see CmdHubGuide).
static void SpeakMarks(const HubLandmark* marks, int count,
                       float px, float pz)
{
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
    SpeakMarks(marks, count, px, pz);
}

// ---- landmark Guide beyond the Hub (tables SURVEYED 2026-10-24) -------------
//
// The same Guide, room by room, for the rooms the bench has
// walked end to end (guide-progress survey; per-room notes in
// games/kh-bbs/). Every position below was measured by walking
// to the thing itself — a position log at contact, and a
// screenshot, an interaction prompt, or a walked room flip
// proving it is what the line names. Positions are the objects'
// own origins from the room's spawn data (the Hub precedent);
// exits use the exit trigger's center and are named for the
// room they lead to, exactly as kRoomNames names it — a door
// counts only when the flip through it was walked and the
// destination read directly. Anything that could not be walked
// to is NOT in a table: the Guide would rather say less than
// guess. Features verified per room:
//
//   Vestibule (8,2): the save point matched its walked stand to
//   0.27 with its Save prompt on screen; the Moogle by its Shop
//   prompt and the earlier end-to-end shop proof; the big chest
//   by contact (it is 1.8 long, so contact is ~1.5 from its
//   center); Hercules and Phil by their Talk prompts and
//   screenshots. Hades stands in this room's story-pattern
//   spawn data only — two live censuses of the finished save
//   list exactly the five residents above plus the player, so
//   he is not named. The south alcove is decoration, not a
//   door: the north door is the room's only exit.
//   Coliseum Gates (8,1): the northwest chest by contact. Two
//   more chests sit in a strip south of the terrace wall that
//   walking cannot reach from this room — excluded.
//   Town Near Thebes (8,5): the street was walked end to end;
//   it holds nothing but hittable pots and Unversed spawns, so
//   its table is the single door back.
//   Entryway (6,2): the room holds only the save point (stand
//   matched the arrival position to 0.01) and its two doors.
//   Central Square (6,3): a two-level room — entrance terrace,
//   upper ring, center basin — spoken with ground-plane
//   distances, as in the Hub. Both small chests by contact
//   (west ring 0.98; east chest 1.01 by the south ring). The
//   west door is shut for the finished save: the player was
//   stood inside its trigger and pushed to the wall with no
//   flip, so it is not named. Where it leads was never read.
//   Castle Town (6,5): the Moogle stands behind its shop
//   counter — the "gate" an earlier bench could not pass; the
//   Shop prompt arms at the counter's west end, 2.26 from the
//   Moogle's origin. The small west door flips to Merlin's
//   House; the south door to Fountain Court.
//   Merlin's House (6,8): Merlin by his Talk prompt and
//   screenshot; the small chest by contact. The exit point is
//   the west door's trigger, bisected live (safe stand at
//   x -5.87, flip walking to -6.40) and centered by the
//   arrival-offset pattern the other rooms show.
//   Fountain Court (6,7): only the entrance balcony is tabled —
//   it is fully railed, with no drop or stair down, and its
//   door back to Castle Town is proven by the arrival itself.
//   The court's main floor is entered from a west door whose
//   other side this lineage never reached: unsurveyed.
//   Outer Gardens (6,12): a long garden corridor. Entryway's
//   north door lands at the FAR end (arrival stand (0,-119.24)
//   against the (0,-123.25) trigger); the near door flips to
//   the Purification Facility. Both chests by contact (~1.0).
//   There is no save point in this room.
//
// The Purification Facility (6,11) and the Land of Departure
// rooms stay un-Guided: the facility is a vertical maze the
// survey entered but did not finish, and Mountain Path's exits
// are story-blocked in the only state that reaches them. The
// Guide fails closed in both, exactly as everywhere unsurveyed.
static const HubLandmark kVestibuleMarks[] = {
    {"Save point", -2.25f, 2.97f},
    {"Moogle Shop", 2.87f, 3.50f},
    {"Large chest", 3.98f, -1.10f},
    {"Hercules", -2.50f, -4.33f},
    {"Phil", 1.26f, -3.90f},
    {"Coliseum Gates", 0.00f, 5.60f},
};
static const HubLandmark kColiseumGatesMarks[] = {
    {"Small chest", -21.41f, 17.28f},
    {"Vestibule", 0.00f, -15.40f},
    {"Town Near Thebes", 0.00f, 20.80f},
};
static const HubLandmark kThebesMarks[] = {
    {"Coliseum Gates", 0.00f, -15.50f},
};
static const HubLandmark kRgEntrywayMarks[] = {
    {"Save point", -5.87f, 3.28f},
    {"Outer Gardens", 0.00f, 13.50f},
    {"Central Square", 0.00f, -12.75f},
};
static const HubLandmark kRgCentralSquareMarks[] = {
    {"Small chest", -32.40f, 13.56f},
    {"Small chest", 32.40f, -17.31f},
    {"Entryway", 0.00f, 43.25f},
    {"Castle Town", 34.00f, 0.00f},
};
static const HubLandmark kRgCastleTownMarks[] = {
    {"Save point", 14.58f, -0.23f},
    {"Moogle Shop", 15.69f, 9.87f},
    {"Large chest", 0.75f, -9.73f},
    {"Merlin's House", -14.15f, -5.20f},
    {"Central Square", -13.00f, 20.00f},
    {"Fountain Court", 9.00f, -24.00f},
};
static const HubLandmark kRgMerlinsHouseMarks[] = {
    {"Merlin", 1.83f, 3.87f},
    {"Small chest", -1.94f, 6.89f},
    {"Castle Town", -7.50f, 4.00f},
};
static const HubLandmark kRgFountainCourtMarks[] = {
    {"Castle Town", 42.75f, 36.25f},
};
static const HubLandmark kRgOuterGardensMarks[] = {
    {"Small chest", 32.76f, -28.41f},
    {"Small chest", -36.46f, 22.63f},
    {"Purification Facility", 0.00f, -3.00f},
    {"Entryway", 0.00f, -123.25f},
};

struct RoomGuide { unsigned world; unsigned room;
                   const HubLandmark* marks; int count; };
#define ROOM_GUIDE(w, r, t) { w, r, t, (int) (sizeof(t) / sizeof(t[0])) }
static const RoomGuide kRoomGuides[] = {
    ROOM_GUIDE(8, 2, kVestibuleMarks),
    ROOM_GUIDE(8, 1, kColiseumGatesMarks),
    ROOM_GUIDE(8, 5, kThebesMarks),
    ROOM_GUIDE(6, 2, kRgEntrywayMarks),
    ROOM_GUIDE(6, 3, kRgCentralSquareMarks),
    ROOM_GUIDE(6, 5, kRgCastleTownMarks),
    ROOM_GUIDE(6, 8, kRgMerlinsHouseMarks),
    ROOM_GUIDE(6, 7, kRgFountainCourtMarks),
    ROOM_GUIDE(6, 12, kRgOuterGardensMarks),
};

// The Guide query in a surveyed room beyond the Hub: the same
// gating shape as CmdHubGuide — silent in camp and menus,
// silent while the two location copies disagree, silent when
// the position cannot be located, silent in any room with no
// surveyed table.
void CmdRoomGuide(void)
{
    if (CampActive() || ReportsUp()) return;
    unsigned wA = u8(kWorldAddrA), rA = u8(kWorldAddrA + 1);
    unsigned wB = u8(kWorldAddrB), rB = u8(kWorldAddrB + 1);
    if (wA != wB || rA != rB) return;
    const RoomGuide* g = nullptr;
    for (size_t i = 0; i < sizeof(kRoomGuides) / sizeof(kRoomGuides[0]); i++)
        if (kRoomGuides[i].world == wA && kRoomGuides[i].room == rA) {
            g = &kRoomGuides[i];
            break;
        }
    if (!g) return;
    if (!InField() || !EnsureNode()) return;
    float px = f32(g_nodeBase + kLockRecOffPPos);
    float pz = f32(g_nodeBase + kLockRecOffPPos + 8);
    SpeakMarks(g->marks, g->count, px, pz);
}

// The spoken foe clause for a held lock: the target's name (when
// proven), health and distance. Any validation failure leaves
// the plain suffix.
bool LockClause(char* buf, size_t n)
{
    unsigned cur, mx;
    float dist, dy;
    if (!ReadFoe(&cur, &mx, &dist, &dy)) return false;
    unsigned units = (unsigned) (dist + 0.5f);
    char name[40];
    if (ReadFoeName(name, sizeof(name)))
        snprintf(buf, n, " Locked on. %s health %u of %u, %u unit%s away.",
                 name, cur, mx, units, units == 1 ? "" : "s");
    else
        snprintf(buf, n, " Locked on. Enemy health %u of %u, %u unit%s away.",
                 cur, mx, units, units == 1 ? "" : "s");
    return true;
}

// ---- multi-enemy awareness: the foe census (VERIFIED 2026-10-25, bench multi1) ----
//
// ReadFoe answers for ONE foe — the locked one. A fight usually
// holds several, and until now the player had no way to learn how
// many, or where the others stood. The census below enumerates
// every live foe in the room, on demand.
//
// WHO IS A FOE (each gate mirrors a proven exclusion):
//   - a scene object: signature 0x00011000 at +0x08, a small scene
//     id at +0x00, and an object code at +0x88 in the strict shape
//     (the census pattern the name work proved over the dump
//     corpus: every scene object in a fight dump matches exactly
//     once — player, gimmicks, NPCs, weapons and all);
//   - code prefix 'm' or 'b': monsters and bosses. The prefix is
//     what keeps the player's 'p' object, the 'g' gimmicks (the
//     transporter rings, the arena terminal), the 'n' NPCs and the
//     'w' weapon objects out — several of those carry HP-shaped
//     words of their own (the boss's keyblade reads 1/1, the arena
//     gimmick 0/0) and are excluded by the HP gate as well;
//   - a live HP pair on the entity block (obj − 0x1B8, the
//     ReadFoe layout): max in [10, 20000], 0 < cur <= max. Dead
//     and dying blocks (cur 0) do not count — a corpse is not an
//     enemy the player still has to fight.
//
// WHERE A FOE STANDS: no fixed offset holds it — the scene object
// embeds a palette of bone world matrices whose layout shifts per
// instance. The position is the translation row of the palette's
// root matrix (the 4th well-formed matrix, index 3): a translation
// row (x, y, z, exactly 1.0f) preceded by three rotation rows
// (unit-length, w = 0). Against the lock record's target position
// — ground truth — index 3 matched EXACTLY in 17 of 20 locked-foe
// checks across the name8/9/10 dump corpus (175 foe instances);
// the other 3 (a Scrapper mid-spawn, a flying Archraven, a
// Thornbite at 1 HP mid-death) had no well-formed palette at all
// and fail closed here: the foe still counts and is named, it
// just claims no distance. 153 of the 175 corpus instances carry
// a position. No check ever put the true position at a different
// index.
//
// COST: one sweep of the same span LocateLockRecord already walks
// (0x09800000..RAM end), one signature read per word with rare
// follow-ups — a query-time cost, never per frame: results are
// cached for 1.5 s and keyed to the actor, so the field line, the
// foe query and the roster share one sweep when asked together.
//
// SPEECH (the sound-first doctrine: queries only, nothing is
// announced unasked — a foe dying is already carried by the hit
// beeps and milestones, and a new wave by the game's own spawn
// sounds): the Enemies command speaks the count and a roster
// (nearest first, name when proven, distance, a clock bearing
// once facing is known, the locked foe marked); the field line
// and the foe query gain a bare count. No wave event is wired:
// it would need this sweep on a cadence inside the frame loop
// to announce a fact the player can ask for in one press.
constexpr uint32_t kFoeObjSigOff = 0x08;
constexpr uint32_t kFoeObjSig    = 0x00011000;
constexpr int kMaxFoes = 16;
constexpr uint64_t kCensusCacheMs = 1500;

struct FoeEntry {
    uint32_t obj;
    const char* name;   // proven type name, or nullptr
    bool hasPos;
    float dist;         // 3D distance from the player (hasPos only)
    float dx, dz;       // ground-plane offset from the player (hasPos only)
    bool locked;
};

FoeEntry g_foes[kMaxFoes];
int g_foeCount = -1;              // -1: no census for this actor yet
uint32_t g_foeCensusActor = 0;
uint64_t g_foeCensusMs = 0;

// The strict object-code shape (the same shape FoeTypeName
// validates before its journal-number rules): [a-z] digit digit
// [a-z] [a-z] digit digit NUL.
bool FoeCodeShape(const char* code)
{
    if (code[0] < 'a' || code[0] > 'z') return false;
    if (code[1] < '0' || code[1] > '9' || code[2] < '0' || code[2] > '9')
        return false;
    if (code[3] < 'a' || code[3] > 'z' || code[4] < 'a' || code[4] > 'z')
        return false;
    if (code[5] < '0' || code[5] > '9' || code[6] < '0' || code[6] > '9')
        return false;
    return code[7] == 0;
}

// The foe's world position: the translation row of the 4th
// well-formed bone matrix in its scene object (see the census
// header for the proof and the fail-closed cases).
bool FoePalettePos(uint32_t obj, float* xOut, float* yOut, float* zOut)
{
    float cand[8][3];
    int n = 0;
    for (uint32_t o = 0x400; o < 0xC00; o += 4) {
        if (u32(obj + o + 12) != 0x3F800000) continue;
        bool ok = true;
        for (int r = 0; r < 3 && ok; r++) {
            uint32_t ro = o - 0x30 + (uint32_t) r * 0x10;
            if (u32(obj + ro + 12) != 0) { ok = false; break; }
            float a = f32(obj + ro), b = f32(obj + ro + 4),
                  c = f32(obj + ro + 8);
            float len = sqrtf(a * a + b * b + c * c);
            if (!(len >= 0.97f && len <= 1.03f)) ok = false;
        }
        if (!ok) continue;
        float x = f32(obj + o), y = f32(obj + o + 4), z = f32(obj + o + 8);
        if (!(x >= -300.0f && x <= 300.0f && y >= -300.0f && y <= 300.0f &&
              z >= -300.0f && z <= 300.0f))
            continue;
        if (n < 8) { cand[n][0] = x; cand[n][1] = y; cand[n][2] = z; }
        n++;
    }
    if (n < 4) return false;
    *xOut = cand[3][0]; *yOut = cand[3][1]; *zOut = cand[3][2];
    return true;
}

int RunCensus(void)
{
    g_foeCount = -1;
    if (!g_actorBase) return -1;
    // The player's position from the engagement component the
    // position clause reads. Without it the census still counts
    // and names — it claims no distances.
    bool havePlayer = EnsureNode();
    float px = 0, py = 0, pz = 0;
    if (havePlayer) {
        px = f32(g_nodeBase + kLockRecOffPPos);
        py = f32(g_nodeBase + kLockRecOffPPos + 4);
        pz = f32(g_nodeBase + kLockRecOffPPos + 8);
    }
    uint32_t lockedObj = FoeSceneObject();
    int n = 0;
    for (uint32_t x = 0x09800000; x + 0xC00 < kUserRamEnd && n < kMaxFoes;
         x += 4) {
        if (u32(x + kFoeObjSigOff) != kFoeObjSig) continue;
        if (u32(x) > 100000) continue;   // the scene id is a small index
        char code[8];
        for (int i = 0; i < 8; i++)
            code[i] = (char) u8(x + kFoeObjCodeOff + (uint32_t) i);
        if (code[0] != 'm' && code[0] != 'b') continue;
        if (!FoeCodeShape(code)) continue;
        uint32_t blk = x - kFoeBlockBack;
        unsigned cur = (unsigned) u16(blk + kActorOffCurHp);
        unsigned mx = (unsigned) u16(blk + kActorOffMaxHp);
        if (mx < 10 || mx > 20000 || cur == 0 || cur > mx) continue;
        FoeEntry& e = g_foes[n];
        e.obj = x;
        e.name = FoeTypeName(code);
        e.locked = (x == lockedObj);
        e.hasPos = false;
        e.dist = 0; e.dx = 0; e.dz = 0;
        float fx, fy, fz;
        if (havePlayer && FoePalettePos(x, &fx, &fy, &fz)) {
            float ddx = fx - px, ddy = fy - py, ddz = fz - pz;
            e.dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            e.dx = ddx; e.dz = ddz;
            e.hasPos = true;
        }
        n++;
    }
    // Nearest first; foes without a verified position sort last
    // and keep their scan order among themselves (insertion sort
    // is stable).
    for (int i = 1; i < n; i++) {
        FoeEntry t = g_foes[i];
        float key = t.hasPos ? t.dist : 1e30f;
        int j = i - 1;
        while (j >= 0) {
            float jk = g_foes[j].hasPos ? g_foes[j].dist : 1e30f;
            if (jk <= key) break;
            g_foes[j + 1] = g_foes[j];
            j--;
        }
        g_foes[j + 1] = t;
    }
    g_foeCount = n;
    g_foeCensusActor = g_actorBase;
    g_foeCensusMs = g_host ? g_host->now_ms : 0;
    return n;
}

// The foe count, from a census no older than kCensusCacheMs.
// -1 when there is no located actor to census around. (A census
// timestamp of 0 means "no cache" — the same convention the
// lock-record throttle uses — so hosts whose clock starts at 0
// simply re-census per query.)
int CensusCount(void)
{
    if (!g_actorBase) return -1;
    uint64_t now = g_host ? g_host->now_ms : 0;
    if (g_foeCount >= 0 && g_foeCensusActor == g_actorBase && g_foeCensusMs &&
        now >= g_foeCensusMs && now - g_foeCensusMs < kCensusCacheMs)
        return g_foeCount;
    return RunCensus();
}

// " 3 enemies." for the field line. Spoken when there is more to
// know than the one tracked foe, or a single foe the player is
// not locked onto; a lone locked foe is the lock clause's story.
// Silent whenever the census has nothing to add — it never claims
// "no enemies" from the field line.
void EnemyCountClause(char* buf, size_t n)
{
    buf[0] = 0;
    int c = CensusCount();
    if (c < 0) return;
    if (c >= 2 || (c == 1 && u16(g_actorBase + kActorOffMark) != 2))
        snprintf(buf, n, " %d enem%s.", c, c == 1 ? "y" : "ies");
}

// "straight ahead" / "3 o'clock" for a ground-plane offset, in
// the Hub Guide's terms: relative to the movement-derived
// facing, 12 is straight ahead and 3 is to the right. The caller
// guarantees a live facing and a non-trivial offset.
void ClockPhrase(float fdx, float fdz, float dx, float dz, char* buf, size_t n)
{
    float dist = sqrtf(dx * dx + dz * dz);
    float inv = 1.0f / dist;
    float sinA = fdz * (dx * inv) - fdx * (dz * inv);
    float cosA = fdx * (dx * inv) + fdz * (dz * inv);
    float rel = atan2f(sinA, cosA) * 57.2957795f;
    if (rel < 0.0f) rel += 360.0f;
    int clock = (int) (rel / 30.0f + 0.5f);
    if (clock == 0) clock = 12;
    if (clock == 12) snprintf(buf, n, "straight ahead");
    else snprintf(buf, n, "%d o'clock", clock);
}

// The roster query (the Enemies command): the count, then each
// foe nearest first — its name when proven, its distance, a
// clock bearing once the player's facing is known from movement,
// and the locked foe marked so the roster reconciles with the
// beeps and milestones that track it. A foe whose position did
// not verify is named without a distance rather than given a
// wrong one. At most 8 entries are spoken; the rest are counted
// in a closing line, so a big fight stays one short answer.
void CmdRoster(void)
{
    int c = CensusCount();
    if (c < 0) return;
    if (c == 0) { Say("No enemies.", "foe", oga::Priority::High); return; }
    char line[160];
    if (c == 1) Say("1 enemy.", "foe", oga::Priority::High);
    else {
        snprintf(line, sizeof(line), "%d enemies.", c);
        Say(line, "foe", oga::Priority::High);
    }
    float fdx = 0, fdz = 0;
    bool facing = FacingVector(&fdx, &fdz);
    int speak = c < 8 ? c : 8;
    for (int i = 0; i < speak; i++) {
        const FoeEntry& e = g_foes[i];
        const char* nm = e.name ? e.name : "Enemy";
        const char* lockSuffix = e.locked ? ", locked on" : "";
        if (!e.hasPos) {
            snprintf(line, sizeof(line), "%s%s.", nm, lockSuffix);
        } else {
            unsigned units = (unsigned) (e.dist + 0.5f);
            float flat = sqrtf(e.dx * e.dx + e.dz * e.dz);
            if (facing && flat >= 1.0f) {
                char clock[24];
                ClockPhrase(fdx, fdz, e.dx, e.dz, clock, sizeof(clock));
                snprintf(line, sizeof(line), "%s, %u away, %s%s.",
                         nm, units, clock, lockSuffix);
            } else {
                snprintf(line, sizeof(line), "%s, %u away%s.",
                         nm, units, lockSuffix);
            }
        }
        Say(line, "foe", oga::Priority::High);
    }
    if (c > speak) {
        snprintf(line, sizeof(line), "And %d more.", c - speak);
        Say(line, "foe", oga::Priority::High);
    }
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

// The live battle-gauges object, or 0 when the game has none
// (its destructor zeroes the global). The float sanity check
// keeps a stale or half-built object from speaking: both gauges
// sit within 0..100 for the object's whole life.
uint32_t GaugesObj(void)
{
    uint32_t obj = u32(kGaugesPtrAddr);
    if (obj < kUserRamBase || obj + 0x84 > kUserRamEnd) return 0;
    float dl = f32(obj + kGaugesOffDLink);
    float foc = f32(obj + kGaugesOffFocus);
    if (!(dl >= 0.0f && dl <= 100.0f)) return 0;
    if (!(foc >= 0.0f && foc <= 100.0f)) return 0;
    return obj;
}

// " Focus 73. D-Link 45." for the field line, with the full and
// linked forms (" Focus full. D-Link full." / " Focus 73.
// Linked to Ventus, D-Link 62."). Empty when the gauges object
// does not resolve — the line then reads as it did before.
void GaugesClause(char* buf, size_t n)
{
    buf[0] = 0;
    uint32_t obj = GaugesObj();
    if (!obj) return;
    float foc = f32(obj + kGaugesOffFocus);
    float dl = f32(obj + kGaugesOffDLink);
    char focBuf[24], dlBuf[72];
    if (foc >= 99.5f) snprintf(focBuf, sizeof(focBuf), " Focus full.");
    else snprintf(focBuf, sizeof(focBuf), " Focus %d.", (int) (foc + 0.5f));
    uint16_t partner = u16(obj + kGaugesOffPartner);
    char pname[40];
    if (partner && DefName(partner, pname, sizeof(pname)))
        snprintf(dlBuf, sizeof(dlBuf), " Linked to %s, D-Link %d.",
                 pname, (int) (dl + 0.5f));
    else if (dl >= 99.5f) snprintf(dlBuf, sizeof(dlBuf), " D-Link full.");
    else snprintf(dlBuf, sizeof(dlBuf), " D-Link %d.", (int) (dl + 0.5f));
    snprintf(buf, n, "%s%s", focBuf, dlBuf);
}

void FieldLine(char* buf, size_t n)
{
    unsigned cur = (unsigned) u16(g_actorBase + kActorOffCurHp);
    unsigned max = (unsigned) u16(g_actorBase + kActorOffMaxHp);
    unsigned gauge = (unsigned) u16(g_actorBase + kActorOffGauge);
    char gauges[128];
    GaugesClause(gauges, sizeof(gauges));
    // The character name comes from the player block (all lineages);
    // only the deck cursor word is tutorial-lineage-only.
    const char* name = (g_charId >= 0 && g_charId < kCharKnown)
                           ? kCharNames[g_charId] : nullptr;
    char lockBuf[96] = "";
    if (u16(g_actorBase + kActorOffMark) == 2 &&
        !LockClause(lockBuf, sizeof(lockBuf)))
        snprintf(lockBuf, sizeof(lockBuf), " Locked on.");
    const char* lockSuffix = lockBuf;
    char count[24];
    EnemyCountClause(count, sizeof(count));
    if (!ActorIsTutorial()) {
        // Relocated actor: health and gauge are struct-relative and
        // proven; add the name when the player block identified the
        // character, but never the tutorial lineage's deck facts.
        if (name)
            snprintf(buf, n, "%s, health %u of %u. Gauge %u.%s%s%s", name, cur, max, gauge, lockSuffix, gauges, count);
        else
            snprintf(buf, n, "Health %u of %u. Gauge %u.%s%s%s", cur, max, gauge, lockSuffix, gauges, count);
        return;
    }
    char cmd[64];
    uint32_t idx = u32(kDeckCursorAddr);
    if (idx < (uint32_t) kDeckKnown)
        snprintf(cmd, sizeof(cmd), "%s", kDeckItems[idx]);
    else
        snprintf(cmd, sizeof(cmd), "command %d", (int) idx + 1);
    snprintf(buf, n, "%s, health %u of %u. Command: %s. Gauge %u.%s%s%s",
             name ? name : "Ventus", cur, max, cmd, gauge, lockSuffix, gauges, count);
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
        // Unlocked, the honest refusal gains the census count when
        // there is a fight to know about: "Not locked on. 3 enemies."
        int c = CensusCount();
        if (c > 0) {
            char line[64];
            snprintf(line, sizeof(line), "Not locked on. %d enem%s.",
                     c, c == 1 ? "y" : "ies");
            Say(line, "foe", oga::Priority::High);
        } else {
            Say("Not locked on.", "foe", oga::Priority::High);
        }
        return;
    }
    unsigned cur, mx;
    float dist, dy;
    if (!ReadFoe(&cur, &mx, &dist, &dy)) {
        Say("Lock lost.", "foe", oga::Priority::High);
        return;
    }
    const char* vert = dy > 5.0f ? ", above you" : dy < -5.0f ? ", below you" : "";
    // The rest of the fight, in one clause: how many OTHER foes
    // the census sees beyond the locked one this line reports.
    char others[32] = "";
    int c = CensusCount();
    if (c >= 2) {
        uint32_t lo = FoeSceneObject();
        bool lockedIn = false;
        for (int i = 0; i < c; i++)
            if (g_foes[i].obj == lo) { lockedIn = true; break; }
        int other = c - (lockedIn ? 1 : 0);
        if (other > 0)
            snprintf(others, sizeof(others), " %d other enem%s.",
                     other, other == 1 ? "y" : "ies");
    }
    char line[224];
    char name[40];
    if (ReadFoeName(name, sizeof(name)))
        snprintf(line, sizeof(line), "%s. Health %u of %u. %u away%s.%s",
                 name, cur, mx, (unsigned) (dist + 0.5f), vert, others);
    else
        snprintf(line, sizeof(line), "Enemy health %u of %u. %u away%s.%s",
                 cur, mx, (unsigned) (dist + 0.5f), vert, others);
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
    char name[40];
    if (ReadFoeName(name, sizeof(name)))
        snprintf(line, sizeof(line), "Locked on: %s. %u away.",
                 name, (unsigned) (dist + 0.5f));
    else
        snprintf(line, sizeof(line), "Locked on. %u away.", (unsigned) (dist + 0.5f));
    Say(line, "lock", oga::Priority::High);
}

// Self readout for the remaining unit-cycling commands in the
// field (Dissidia maps them to its battle self query).
void CmdSelfLine(void)
{
    char line[256];
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

// ---- the Mirage Arena event menus (VERIFIED 2026-10-24, bench g10;
//      the mode child WIRED 2026-10-25, bench khbbs-cb1) ----
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
// Choosing New Event opens the mode child: Versus Mode /
// Arena Mode / Command Board / Rumble Racing, opening on
// Arena Mode (Versus is greyed while offline but the cursor
// still lands on it), wrapping both ways — wired below as
// the mode menu. Active Events and D-Links would list other
// players' sessions; with nobody in range both are no-ops
// that leave the top menu up.
// Both menus are ONE widget with one layout (base = the
// screen word; offsets +0x00 screen, +0x0E cursor, +0x10
// previous-cursor tell, +0x12 count), told apart by the
// screen word: 2 = the top menu (count word == 3), 3 = the
// mode child (its count word reads 0; the rows are the four
// above, proven by the full cb1 walk: start at Arena Mode,
// Down cycles Command Board, Rumble Racing, Versus Mode,
// Arena Mode; Up wraps to Versus Mode). The constant
// signature words are identical for both: +0x04 == 15,
// +0x08 == 0xDBFF, +0x0A == 0xFFF8, +0x0C == 0.
// The widget FLOATS. Bench g10 found two homes (fresh opens
// in the menu-system statics at 0x09919208; the menu rebuilt
// after a mode-child trip at 0x0991BCB8), and bench cb1 found
// the rest of the story: after an Arena Mode setup-screen
// round trip the whole family relocates again (the top menu
// rebuilt at 0x0991E278, the mode child at 0x0991B5B8, the
// top menu again at 0x0991E348) — a hardcoded home pair goes
// silently blind there (cb1's WhereAmI answered the field
// line over the visibly open rebuilt menu). So the widget is
// located the way the shop widget is: a signature scan over
// the menu heap window with a sticky cache, uniqueness
// required — in every cb1/g10 dump exactly one family
// widget is live at a time, and across the bench's 2,478
// RAM dumps the full signature matches only in event-menu
// states and in post-close field dumps where the flow word
// (below) is 0. The scan runs only behind the cheap
// pre-terms; with the cache warm it costs the same handful
// of reads the old two-home checks did.
// The conjuncts, unchanged from g10: the menu-system active
// word (u32 @ 0x08B5BEF8 == 8, the camp gate's witness A:
// these menus are built on the same menu system as camp,
// and the word is up through every phase of the event flow)
// and camp itself excluded (camp shares the word, but its
// witness B reads 0 throughout the event flow, so
// CampActive() stays false here). The active word is
// load-bearing, not decoration: when a menu closes, its
// widget block is never torn down — the whole block lingers
// with the signature intact while the player walks the
// field (proven by the g10c/g10f end dumps and cb1's closed
// dump, which finds the last top-menu widget still shaped)
// — and only the active word (8 through the flow, 0 in the
// Hub field before and after, bench g10k's two full
// open/dive/return/close cycles) separates "menu up" from
// "menu gone, widget stale".
// The arrival edges are debounced like the game-over
// menu's: a rise speaks only after the gate was down for
// 30+ frames, so a flicker cannot re-fire the line mid-menu,
// while the mode-screen round trip (hundreds of frames
// down) still re-announces on return. One opening transient
// from g10 stands: the widget's +0x0C word reads garbage
// for the first ~30 frames after Cross (a half-built
// widget), so an arrival line fires a beat after the menu
// becomes visible.
constexpr uint32_t kEvFlowAddr = 0x08B5BEF8;  // menu-system active word (camp witness A), u32 == 8
constexpr uint32_t kEvHeapLo   = 0x09910000;  // widget scan window (the shop heap window)
constexpr uint32_t kEvHeapHi   = 0x09960000;
const char* const kEventMenuItems[] = { "New Event", "Active Events", "D-Links" };
constexpr int kEventMenuCount = 3;
const char* const kModeMenuItems[] = { "Versus Mode", "Arena Mode", "Command Board", "Rumble Racing" };
constexpr int kModeMenuCount = 4;

uint32_t g_evWidget = 0;        // located family widget base (sticky)
bool g_evUp = false;            // top-menu gate state last frame
int g_evDownFrames = 1 << 30;   // frames since the top gate was last up
bool g_modeSpoken = false;      // mode arrival spoken for this visit
bool g_modeArmed = true;        // a 30+ frame down-stretch has armed the next arrival
int g_modeRow = -1;             // mode cursor at the last stable check
int g_modeStable = 0;           // frames the mode cursor has held still
int g_modeDownFrames = 1 << 30; // frames since the mode gate was last up

// One block's family signature; on a match, *screen receives
// the screen word (2 top / 3 mode) and *row the cursor. The
// mode child's count word reads 0, so its cursor is bounded
// by the walked row count instead.
bool EvWidgetShape(uint32_t base, int* screen, int* row)
{
    if (u16(base + 0x08) != 0xDBFF) return false;
    if (u16(base + 0x0A) != 0xFFF8) return false;
    if (u16(base + 0x04) != 15) return false;
    if (u16(base + 0x0C) != 0) return false;
    uint16_t scr = u16(base + 0x00);
    uint16_t cur = u16(base + 0x0E);
    if (scr == 2) {
        if (u16(base + 0x12) != (uint16_t) kEventMenuCount) return false;
        if (cur >= (uint16_t) kEventMenuCount) return false;
    } else if (scr == 3) {
        if (cur >= (uint16_t) kModeMenuCount) return false;
    } else return false;
    if (screen) *screen = (int) scr;
    if (row) *row = (int) cur;
    return true;
}

// The live family widget's base, or 0. The sticky cache is
// re-validated on every call; a miss scans the heap window
// and demands exactly one shaped block.
uint32_t EvLocate(void)
{
    if (g_evWidget && EvWidgetShape(g_evWidget, nullptr, nullptr)) return g_evWidget;
    g_evWidget = 0;
    uint32_t found = 0; int hits = 0;
    for (uint32_t a = kEvHeapLo; a + 0x14 <= kEvHeapHi; a += 2) {
        if (!EvWidgetShape(a, nullptr, nullptr)) continue;
        hits++; found = a;
        if (hits > 1) break;   // ambiguous: fail closed below
    }
    if (hits != 1) return 0;
    g_evWidget = found;
    return found;
}

// The event family's current screen: 0 = neither menu up,
// 2 = the top menu, 3 = the New Event mode list. On a hit,
// *row (if given) receives the cursor. The cheap pre-terms
// run first, so the locator's scan is only ever paid inside
// the event flow.
int EvScreen(int* row)
{
    if (u32(kEvFlowAddr) != 8) return 0;
    if (CampActive()) return 0;   // camp shares the active word; not these menus
    uint32_t w = EvLocate();
    if (!w) return 0;
    int scr = 0;
    if (!EvWidgetShape(w, &scr, row)) return 0;
    return scr;
}

bool EventMenuUp(void) { return EvScreen(nullptr) == 2; }
bool ModeMenuUp(void) { return EvScreen(nullptr) == 3; }

void EventMenuRow(char* buf, size_t n)
{
    int cur = 0;
    EvScreen(&cur);
    if (cur < 0 || cur >= kEventMenuCount) cur = 0;
    snprintf(buf, n, "%s, %d of %d", kEventMenuItems[cur], cur + 1, kEventMenuCount);
}

void ModeMenuRow(char* buf, size_t n)
{
    int cur = 0;
    EvScreen(&cur);
    if (cur < 0 || cur >= kModeMenuCount) cur = 0;
    snprintf(buf, n, "%s, %d of %d", kModeMenuItems[cur], cur + 1, kModeMenuCount);
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

void WatchModeMenu(void)
{
    int row = 0;
    bool up = (EvScreen(&row) == 3);
    if (up) {
        // The arrival waits for the cursor to SETTLE: bench
        // cb3's per-frame poll showed the widget validating
        // with cursor 0 for ~4 frames before the game writes
        // its start row (Arena Mode) — an arrival on the first
        // valid frame names Versus Mode, a row the player is
        // not on. Ten stable frames clears the transient with
        // margin; a player already moving the cursor just
        // delays the line to the row they land on.
        if (row != g_modeRow) { g_modeRow = row; g_modeStable = 0; }
        else if (g_modeStable < (1 << 30)) g_modeStable++;
        if (!g_modeSpoken && g_modeArmed && g_modeStable >= 10) {
            char r[96]; ModeMenuRow(r, sizeof(r));
            char line[160];
            snprintf(line, sizeof(line), "New Event. %s.", r);
            Say(line, "menu", oga::Priority::High);
            g_modeSpoken = true;
            g_modeArmed = false;
        }
        g_modeDownFrames = 0;
    } else {
        if (g_modeDownFrames < (1 << 30)) g_modeDownFrames++;
        if (g_modeDownFrames >= 30) {
            g_modeSpoken = false; g_modeRow = -1; g_modeStable = 0;
            g_modeArmed = true;
        }
    }
}

// ---- the Arena Mode setup screen (VERIFIED 2026-10-25, benches cb1/cb3/cb4) --
//
// Cross on Arena Mode in the mode list replaces it with the
// event setup screen: a "NEW EVENT / ARENA MODE" form with
// three focus rows — Battle (Left/Right cycles the event),
// Players, OK — above a footer with the medal count and
// Arena LV. The screen is drawn by a list widget of the
// Trinity Archives class (header +0x00 0xDFFF, +0x02 0xFFF8,
// cursor at +0x06, count at +0x0A), here with count 3; every
// proven sighting sits at 0x0991BD60, but it is located by
// the same scan discipline as the event family rather than
// trusted at that address. The battle index rides at +0xD8
// from the widget (0..12, wraps; cb4's full Right cycle)
// and the Players count at +0x18. Focus wraps too (Down
// from OK lands back on Battle, cb3's walk).
// The battle's facts are read from the game's own data,
// never shipped as adapter constants: the name is walked
// from the packed string table at 0x08E1CC30 (the thirteen
// names in list order, the table the screen typesets from;
// the walk validates the table by its first entry, so a
// misread can never invent a name), and Risk/Rounds come
// from the static mission table at 0x08E17AA0 (13 records
// of 16 bytes: id at +0x00 == the index, a constant
// 100/28 pair at +0x02/+0x03, Risk at +0x06, Rounds at
// +0x0C; cross-checked against cb4's thirteen screenshots).
// The Players selector is inert offline — its arrows draw
// dimmed and Left/Right change nothing (cb4's probe) — so
// the row speaks the count word as drawn (1 on the proven
// fixture); in an ad-hoc session the same word is what the
// screen shows. Cross on OK closes the menu back to the
// Hub field with the event set; the pad/Ready/Begin tail
// of the flow is covered by the event-state watch below.
constexpr uint32_t kSetupNameTable = 0x08E1CC30;    // packed battle-name strings
constexpr uint32_t kSetupMissionTable = 0x08E17AA0; // 13 x 16-byte mission records
constexpr int kArenaBattleCount = 13;
constexpr int kSetupFocusCount = 3;                 // Battle / Players / OK

uint32_t g_setupWidget = 0;        // located setup widget base (sticky)
bool g_setupSpoken = false;        // setup arrival spoken for this visit
bool g_setupArmed = true;          // a 30+ frame down-stretch arms the arrival
int g_setupPairA = -1;             // focus/battle pair at the last stable check
int g_setupPairB = -1;
int g_setupStable = 0;             // frames the pair has held still
int g_setupDownFrames = 1 << 30;   // frames since the setup gate was last up
int g_setupBattleLatch = -1;       // last battle seen on the setup screen
int g_eventKindLatch = 0;          // what the last setup visit was: 0 none, 1 arena battle, 2 rumble race
int g_raceCourseLatch = -1;        // last course seen on the rumble setup screen

bool SetupWidgetShape(uint32_t base, int* focus)
{
    if (u16(base + 0x00) != 0xDFFF) return false;
    if (u16(base + 0x02) != 0xFFF8) return false;
    if (u16(base + 0x0A) != (uint16_t) kSetupFocusCount) return false;
    uint16_t cur = u16(base + 0x06);
    if (cur >= (uint16_t) kSetupFocusCount) return false;
    if (focus) *focus = (int) cur;
    return true;
}

uint32_t SetupLocate(void)
{
    if (g_setupWidget && SetupWidgetShape(g_setupWidget, nullptr)) return g_setupWidget;
    g_setupWidget = 0;
    uint32_t found = 0; int hits = 0;
    for (uint32_t a = kEvHeapLo; a + 0x0C <= kEvHeapHi; a += 2) {
        if (!SetupWidgetShape(a, nullptr)) continue;
        hits++; found = a;
        if (hits > 1) break;   // ambiguous: fail closed below
    }
    if (hits != 1) return 0;
    g_setupWidget = found;
    return found;
}

// The Rumble Racing setup (below) is the same widget
// class with a different row count; the two forms exclude
// each other so a stale sibling can never double-speak.
bool RumbleShapeLive(void);

// The setup screen's state, or false when it is not up.
// The pre-terms keep it out of the sibling screens: the
// top menu and mode list are excluded explicitly (their
// family widget can coexist-shaped in stale blocks), and
// the Archives widget — the same class — never coexists
// with the event flow word at 8. The Hub term is
// load-bearing, not decoration: when an arena event ends
// (win or lose), the event module raises the same flow
// word for a few hundred frames inside the arena room
// while the stale setup widget still validates — benches
// cb5b/cb9 both caught the gate naming that window
// "Arena Mode setup. OK." The real screen exists only in
// the Hub, where the terminal is.
bool SetupState(int* focus, int* battle)
{
    if (u32(kEvFlowAddr) != 8) return false;
    if (u8(0x08B61CA0) != 15 || u8(0x08B61CA1) != 1) return false;  // the Hub
    if (CampActive()) return false;
    if (EventMenuUp() || ModeMenuUp()) return false;
    if (RumbleShapeLive()) return false;
    uint32_t w = SetupLocate();
    if (!w) return false;
    int f = 0;
    if (!SetupWidgetShape(w, &f)) return false;
    uint16_t idx = u16(w + 0xD8);
    if (idx >= (uint16_t) kArenaBattleCount) return false;
    if (focus) *focus = f;
    if (battle) *battle = (int) idx;
    return true;
}

bool SetupUp(void) { return SetupState(nullptr, nullptr); }

// One name from the packed table, walked by index. Every
// step is sanity-checked (printable ASCII, bounded), and
// the first entry must be the proven anchor name.
bool ArenaBattleName(int idx, char* buf, size_t n)
{
    if (idx < 0 || idx >= kArenaBattleCount) return false;
    uint32_t a = kSetupNameTable;
    for (int i = 0; i <= idx; i++) {
        char s[48]; int len = 0; bool term = false;
        for (int k = 0; k < 47; k++) {
            uint8_t c = u8(a + (uint32_t) k);
            if (c == 0) { term = true; break; }
            if (c < 0x20 || c > 0x7E) return false;
            s[len++] = (char) c;
        }
        if (!term) return false;
        s[len] = 0;
        if (i == 0 && strcmp(s, "Day of Reckoning") != 0) return false;
        if (i == idx) {
            if (len == 0) return false;
            snprintf(buf, n, "%s", s);
            return true;
        }
        a += (uint32_t) len + 1;
    }
    return false;
}

// Risk and Rounds from the mission record, each behind
// the record's own witnesses (its id and constant pair).
bool ArenaBattleFacts(int idx, int* risk, int* rounds)
{
    if (idx < 0 || idx >= kArenaBattleCount) return false;
    uint32_t rec = kSetupMissionTable + (uint32_t) idx * 16;
    if (u16(rec + 0x00) != (uint16_t) idx) return false;
    if (u8(rec + 0x02) != 100 || u8(rec + 0x03) != 28) return false;
    int r = (int) u8(rec + 0x06);
    int rd = (int) u8(rec + 0x0C);
    if (r < 1 || r > 5 || rd < 1 || rd > 20) return false;
    if (risk) *risk = r;
    if (rounds) *rounds = rd;
    return true;
}

// "Day of Reckoning. Risk 1 of 5. Rounds 4." — the name
// and the facts degrade independently; a part that fails
// its witnesses drops out rather than blocking the rest.
void ArenaBattleLine(int idx, char* buf, size_t n)
{
    char name[48];
    bool hasName = ArenaBattleName(idx, name, sizeof(name));
    int risk = 0, rounds = 0;
    bool hasFacts = ArenaBattleFacts(idx, &risk, &rounds);
    if (hasName && hasFacts)
        snprintf(buf, n, "%s. Risk %d of 5. Rounds %d.", name, risk, rounds);
    else if (hasName)
        snprintf(buf, n, "%s.", name);
    else if (hasFacts)
        snprintf(buf, n, "Risk %d of 5. Rounds %d.", risk, rounds);
    else
        snprintf(buf, n, "Battle.");
}

void SetupFocusLine(char* buf, size_t n)
{
    int focus = 0, battle = 0;
    if (!SetupState(&focus, &battle)) { snprintf(buf, n, "Arena Mode setup."); return; }
    if (focus == 0) {
        char bl[128]; ArenaBattleLine(battle, bl, sizeof(bl));
        snprintf(buf, n, "Battle. %s", bl);
    } else if (focus == 1) {
        uint32_t w = SetupLocate();
        int players = w ? (int) u16(w + 0x18) : 0;
        if (players >= 1 && players <= 4) snprintf(buf, n, "Players. %d.", players);
        else snprintf(buf, n, "Players.");
    } else {
        snprintf(buf, n, "OK.");
    }
}

void WatchSetupMenu(void)
{
    int focus = 0, battle = 0;
    bool up = SetupState(&focus, &battle);
    if (up) {
        g_setupBattleLatch = battle;
        g_eventKindLatch = 1;
        // The arrival waits for the focus/battle pair to
        // settle, the mode list's discipline: the widget
        // can validate a beat before the game writes the
        // rows it opens on.
        if (focus != g_setupPairA || battle != g_setupPairB) {
            g_setupPairA = focus; g_setupPairB = battle; g_setupStable = 0;
        } else if (g_setupStable < (1 << 30)) g_setupStable++;
        if (!g_setupSpoken && g_setupArmed && g_setupStable >= 10) {
            char fl[192]; SetupFocusLine(fl, sizeof(fl));
            char line[288];
            snprintf(line, sizeof(line), "Arena Mode setup. %s", fl);
            Say(line, "menu", oga::Priority::High);
            g_setupSpoken = true;
            g_setupArmed = false;
        }
        g_setupDownFrames = 0;
    } else {
        if (g_setupDownFrames < (1 << 30)) g_setupDownFrames++;
        if (g_setupDownFrames >= 30) {
            g_setupSpoken = false; g_setupPairA = -1; g_setupPairB = -1;
            g_setupStable = 0; g_setupArmed = true;
        }
    }
}

// ---- the Rumble Racing setup (VERIFIED 2026-10-25, benches cb11/cb12) --
//
// The mode list's fourth row opens a sibling form in the
// same widget class: banner "NEW EVENT / RUMBLE RACING",
// four focus rows — Course, Laps, Players, OK — over the
// same medals / Arena LV footer. The widget sits at the
// same home as the arena form's (0x0991BD60 in both bench
// sessions) with row count 4. The three values pack into
// the word at +0xD8, one byte each (crowned across
// cb12's sixteen dumps — course cycles with wrap, laps
// walked 5→6→7→6, players wrapped 6→2 and back):
//   byte0 = course index (0..3), byte1 = laps − 1,
//   byte2 = players − 2, byte3 = 0xFF sentinel.
// The Players selector is LIVE here (2..6 — solo play
// fills the grid with CPU racers), unlike the arena
// form's inert offline selector. The four course names
// ship as adapter data: the game's own name table for
// them is heap-built at session addresses (seen at
// 0x0931E5C8, packed and consecutive), so there is no
// static table to read live the way the arena battles
// have; the names were verified against the setup
// screenshots and the game's mission texts.
const char* const kRumbleCourses[4] = {
    "Country Chase", "Disney Drive", "Grand Spree", "Castle Circuit"};

uint32_t g_rumbleWidget = 0;       // located rumble widget base (sticky)
bool g_rumbleSpoken = false;       // rumble arrival spoken for this visit
bool g_rumbleArmed = true;         // a 30+ frame down-stretch arms the arrival
int g_rumblePairA = -1;            // focus/values pair at the last stable check
int g_rumblePairB = -1;
int g_rumbleStable = 0;
int g_rumbleDownFrames = 1 << 30;

bool RumbleWidgetShape(uint32_t base, int* focus)
{
    if (u16(base + 0x00) != 0xDFFF) return false;
    if (u16(base + 0x02) != 0xFFF8) return false;
    if (u16(base + 0x0A) != 4) return false;         // row count: Course/Laps/Players/OK
    int f = (int) u16(base + 0x06);
    if (f < 0 || f > 3) return false;
    if (u8(base + 0xDB) != 0xFF) return false;       // the packed word's sentinel byte
    if (focus) *focus = f;
    return true;
}

uint32_t RumbleLocate(void)
{
    int f = 0;
    if (g_rumbleWidget && RumbleWidgetShape(g_rumbleWidget, &f)) return g_rumbleWidget;
    g_rumbleWidget = 0;
    uint32_t found = 0;
    int hits = 0;
    for (uint32_t a = 0x09910000; a < 0x09960000; a += 2) {
        if (RumbleWidgetShape(a, &f)) { found = a; hits++; }
    }
    if (hits == 1) g_rumbleWidget = found;
    return g_rumbleWidget;
}

bool RumbleShapeLive(void)
{
    return RumbleLocate() != 0;
}

// The rumble setup's state; the value outs are the
// course index and the DECODED laps/players numbers.
bool RumbleState(int* focus, int* course, int* laps, int* players)
{
    if (u32(kEvFlowAddr) != 8) return false;
    if (u8(0x08B61CA0) != 15 || u8(0x08B61CA1) != 1) return false;  // the Hub
    if (CampActive()) return false;
    if (EventMenuUp() || ModeMenuUp()) return false;
    if (SetupLocate() != 0) return false;            // the arena form excludes this one
    uint32_t w = RumbleLocate();
    if (!w) return false;
    int f = 0;
    if (!RumbleWidgetShape(w, &f)) return false;
    int c = (int) u8(w + 0xD8);
    int l = (int) u8(w + 0xD9) + 1;
    int p = (int) u8(w + 0xDA) + 2;
    if (c < 0 || c > 3) return false;
    if (l < 1 || l > 30) return false;
    if (p < 2 || p > 6) return false;
    if (focus) *focus = f;
    if (course) *course = c;
    if (laps) *laps = l;
    if (players) *players = p;
    return true;
}

bool RumbleUp(void)
{
    int f = 0, c = 0, l = 0, p = 0;
    return RumbleState(&f, &c, &l, &p);
}

void RumbleFocusLine(char* buf, size_t n)
{
    int focus = 0, course = 0, laps = 0, players = 0;
    if (!RumbleState(&focus, &course, &laps, &players)) { snprintf(buf, n, "Rumble Racing setup."); return; }
    if (focus == 0) snprintf(buf, n, "Course. %s.", kRumbleCourses[course]);
    else if (focus == 1) snprintf(buf, n, "Laps. %d.", laps);
    else if (focus == 2) snprintf(buf, n, "Players. %d.", players);
    else snprintf(buf, n, "OK.");
}

void WatchRumbleMenu(void)
{
    int focus = 0, course = 0, laps = 0, players = 0;
    bool up = RumbleState(&focus, &course, &laps, &players);
    if (up) {
        g_raceCourseLatch = course;
        g_eventKindLatch = 2;
        // Settle discipline as the sibling forms: the
        // arrival waits for the focus/values pair to hold
        // still (values ride the same int as a packed key).
        int pairB = (course << 16) | (laps << 8) | players;
        if (focus != g_rumblePairA || pairB != g_rumblePairB) {
            g_rumblePairA = focus; g_rumblePairB = pairB; g_rumbleStable = 0;
        } else if (g_rumbleStable < (1 << 30)) g_rumbleStable++;
        if (!g_rumbleSpoken && g_rumbleArmed && g_rumbleStable >= 10) {
            char fl[128]; RumbleFocusLine(fl, sizeof(fl));
            char line[224];
            snprintf(line, sizeof(line), "Rumble Racing setup. %s", fl);
            Say(line, "menu", oga::Priority::High);
            g_rumbleSpoken = true;
            g_rumbleArmed = false;
        }
        g_rumbleDownFrames = 0;
    } else {
        if (g_rumbleDownFrames < (1 << 30)) g_rumbleDownFrames++;
        if (g_rumbleDownFrames >= 30) {
            g_rumbleSpoken = false; g_rumblePairA = -1; g_rumblePairB = -1;
            g_rumbleStable = 0; g_rumbleArmed = true;
        }
    }
}

// ---- the arena event state (VERIFIED 2026-10-25, bench cb4) --
//
// After OK, the flow leaves the menu system: the player
// walks the Hub to the sunken center pad, presses Cross
// ("Ready"), then START ("Begin"). The game's own state
// for this tail is an info-text pointer at 0x09F3A38C:
// the address of the hint line the event flow currently
// wants the player to read, or 0 when no event is pending.
// Proven values (cb4's dump chain): 0 at the plain field
// and in every menu; the "move to the middle" hint string
// (at 0x08E1BE70) from the moment OK lands until Ready is
// pressed; the "press START" string (at 0x08E1BEB0) while
// the player stands ready. The watch keys on the pointer
// but speaks only when the string at it matches the proven
// text, so a reused slot in some other state fails closed.
// Scoped to the Hub field (world 15, room 1): other worlds
// have their own info lines, which stay unspoken.
constexpr uint32_t kArenaInfoPtrAddr = 0x09F3A38C;
constexpr uint32_t kArenaHintPending = 0x08E1BE70;  // "Move to the middle of the hub..."
constexpr uint32_t kArenaHintReady = 0x08E1BEB0;    // "Once everyone is ready..."

int g_arenaInfoCat = 0;   // 0 none, 1 event pending, 2 ready, 3 other

bool ArenaInfoTextIs(uint32_t addr, const char* prefix)
{
    for (int i = 0; prefix[i]; i++)
        if (u8(addr + (uint32_t) i) != (uint8_t) prefix[i]) return false;
    return true;
}

int ArenaInfoCategory(void)
{
    uint32_t p = u32(kArenaInfoPtrAddr);
    if (p == 0) return 0;
    if (p == kArenaHintPending && ArenaInfoTextIs(p, "Move to the middle")) return 1;
    if (p == kArenaHintReady && ArenaInfoTextIs(p, "Once everyone is ready")) return 2;
    return 3;
}

void WatchArenaEvent(void)
{
    if (u8(0x08B61CA0) != 15 || u8(0x08B61CA1) != 1) return;  // the Hub only
    if (!InField()) return;
    int cat = ArenaInfoCategory();
    if (cat == g_arenaInfoCat) return;
    int prev = g_arenaInfoCat;
    g_arenaInfoCat = cat;
    if (cat == 1 && prev != 2) {
        // A fresh event was set at the terminal (or the
        // adapter attached over a pending one). Name what
        // was set when the setup visit latched it — an
        // arena battle or a rumble race; the pending hint
        // text is the same string for both.
        char name[48];
        char line[256];
        if (g_eventKindLatch == 2 && g_raceCourseLatch >= 0 && g_raceCourseLatch <= 3)
            snprintf(line, sizeof(line),
                     "Event set: Rumble Racing, %s. Move to the middle of the hub and press Cross when ready.",
                     kRumbleCourses[g_raceCourseLatch]);
        else if (g_eventKindLatch == 1 && g_setupBattleLatch >= 0 &&
            ArenaBattleName(g_setupBattleLatch, name, sizeof(name)))
            snprintf(line, sizeof(line),
                     "Event set: %s. Move to the middle of the hub and press Cross when ready.", name);
        else
            snprintf(line, sizeof(line),
                     "Event set. Move to the middle of the hub and press Cross when ready.");
        Say(line, "field", oga::Priority::High);
    } else if (cat == 2 && prev == 1) {
        Say("Ready. Press START to begin.", "field", oga::Priority::High);
    } else if (cat == 1 && prev == 2) {
        Say("Ready cancelled.", "field", oga::Priority::High);
    }
}

// ---- the event-end SUMMARY (VERIFIED 2026-10-25, bench cb9b) --
//
// When an arena event finishes — win or lose — the game
// shows a SUMMARY screen in the arena room: the battle
// name, the play time, category rankings (Damage,
// Friendship, Style), an obtained-medals count-up, an
// MVP line. It captured cleanly in cb9b (endwindow
// dumps): world 15, room 3, the event flow word raised —
// the same window that used to trip the setup gate. The
// screen is NON-interactive: it counts up and returns
// the player to the Hub unaided, so the adapter marks
// its arrival with one neutral line and leaves the
// summary's contents unspoken (their data source is
// unhunted; the screen is up only seconds). The watch
// arms on presence in the arena room with the flow word
// down — the room is reachable only by starting an
// event — and fires once when the word rises.
bool g_evEndArmed = false;
int g_evEndArmFrames = 0;

void WatchEventEnd(void)
{
    if (u8(0x08B61CA0) != 15 || u8(0x08B61CA1) != 3) {
        g_evEndArmed = false; g_evEndArmFrames = 0;
        return;
    }
    if (u32(kEvFlowAddr) == 8) {
        if (g_evEndArmed) {
            Say("Event over.", "field", oga::Priority::High);
            g_evEndArmed = false;
        }
        g_evEndArmFrames = 0;
        return;
    }
    if (g_evEndArmFrames < (1 << 30)) g_evEndArmFrames++;
    if (g_evEndArmFrames >= 30) g_evEndArmed = true;
}

// ---- the Command Board (VERIFIED 2026-10-25, benches cb7d and
//      cbplay1-cbplay13) -------------------------------------------------------
//
// The board game runs in its own module, entered from the camp
// Command Board row or the Mirage Arena Log In terminal's mode
// list. The module replaces the world (the world/room words read
// the Hub's 15/1 throughout, and the flow word reads 0), so every
// gate below is board-native and signature-based: the module's
// widgets FLOAT per lineage, and each is found by a signature
// scan over the module heap window with a sticky cache, the
// EvLocate discipline — uniqueness required, fail closed.
//
// The screens, in flow order:
//   * The board TITLE (New Game / Continue / How to Play,
//     wrapping) — a list widget of the camp menu family (class
//     word 0x08B77D68; cursor pair at +0x14/+0x18; the row count
//     at +0x1C as count<<16 and at +0x20 as 0xFFFF0000|count; a
//     per-widget row-table pointer at +0x24). Every board list
//     shares the shape and differs by (count, table): title
//     (3, 0x08BB1550), the turn menu (4, 0x08BBE088), the Hand
//     screen's plays list (5, 0x08BCEE60), the Options list
//     (5, 0x08BCF5A8) and the command-card strip (5, 0x08B8DB78).
//   * The SETUP widget (its own signature: the class word plus
//     three constant pointers at +0x30/+0x34/+0x38): a focus
//     word at +0x20 steps 1 = board select, 3 = goal select,
//     4 = the "Play on this board?" dialog is up. The board
//     index pair (+0x3C/+0x40) cycles the seven boards; the
//     goal index pair (+0x60/+0x64) maps 0/1/2 to
//     5,000/10,000/15,000 GP.
//   * Yes/No DIALOGS are a second widget family (no class word;
//     count words 0x00020000 / 0xFFFF0002, table 0x08BCD828,
//     choice pair at +0x14/+0x18, 0 = Yes, 1 = No, toggled by
//     Left/Right): the setup confirm, the playing-order
//     shuffle prompt, the card-placement "Are you sure?" and
//     the panel purchase prompt share it. Which question is up
//     is told by context (below), never by the widget alone.
//   * The GAME itself: a fixed liveness global (u32 @
//     0x08BC58FC) points at the game object exactly while a
//     game exists (it flips at the setup confirm's Yes and is
//     0 in every other state sampled — camp, field, title,
//     shops, arena). Each player has a record (found by scan,
//     stride 0x24, CHARACTER order): GP on hand at +0x00;
//     bytes at +0x08: character id (Ventus 1, Aqua 2, Terra 3;
//     a guest opponent's id is unnamed here), a human flag
//     (0 for the player, 1 for CPU), a second human tell
//     (0xFF), and the player's turn-order position (0-based);
//     the def of the panel the player stands on as a u16 at
//     +0x0E (names resolve through DefName: the start panel
//     reads Sweet Memory's 38, a panel Ventus bought Cyclone's
//     23, Terra's Rising Rock 1's 66). No word names the
//     player to move (a frame poll across three AI turns
//     killed the one candidate — it was the setup goal index
//     persisting), so turns are ESTIMATED: the turn menu
//     witnesses the human's turn directly, and an opponent's
//     turn is attributed to the order-successor record whose
//     GP, panel or hand first changes. GP totals on the Stats screen add
//     owned panels' prices to the hand GP; prices live in no
//     crowned word, so totals and prices are NOT spoken.
//   * A human turn runs: turn menu (Roll / Hand / Stats /
//     Options) -> Roll -> the walk (Cross advances; junctions
//     steer with the stick, which the reader only narrates)
//     -> the landing: a command panel raises the purchase
//     prompt, Yes opens the card strip ("Select a command
//     card."), a card choice raises "Are you sure?", and Yes
//     places the card ("Command Panel acquired!", GP drops by
//     the price). The Hand screen lists the five special
//     plays (fixed names; the hand's own card names live in
//     no crowned structure and stay silent). Options offers
//     Walking Speed, Message Speed, Help, Take a Break and
//     End Game (row names spoken; the two speed values are
//     not crowned). Result banners wait for a Cross press.
// Opponents' turns are summarized from the records at turn
// end (clean single deltas only): a paired fall/rise is a
// toll paid, a lone fall a purchase or toll to the bank, a
// lone rise a bonus. Anything messier stays silent.

constexpr uint32_t kCbLiveAddr  = 0x08BC58FC;  // u32 -> game object while a board game exists
constexpr uint32_t kCbHeapLo    = 0x09820000;  // board widget scan window
constexpr uint32_t kCbHeapHi    = 0x09880000;
constexpr uint32_t kCbClassPtr  = 0x08B77D68;  // the menu widget class word
constexpr uint32_t kCbRecWinLo  = 0x09F00000;  // player-record scan window
constexpr uint32_t kCbRecWinHi  = 0x09F60000;
constexpr uint32_t kCbRecStride = 0x24;

const char* const kCbTitleItems[] = { "New Game", "Continue", "How to Play" };
const char* const kCbTurnItems[]  = { "Roll", "Hand", "Stats", "Options" };
const char* const kCbPlayItems[]  = { "Stun", "Two Dice", "Navigator", "Three Dice", "Double Toll" };
const char* const kCbOptItems[]   = { "Walking Speed", "Message Speed", "Help", "Take a Break", "End Game" };
// The seven boards, carousel order. Names are the game's own
// (the first three read off the select screen, the rest the
// trophy-list spellings); goal defaults read 5,000 GP on the
// boards checked. Descriptions were verified for the first
// two boards only, so only those are ever spoken.
struct CbBoardInfo { const char* name; const char* desc; };
const CbBoardInfo kCbBoards[] = {
    { "Keyblade Board",  "The thrills are never more than a roll away on this compact board. (Land of Departure)" },
    { "Royal Board",     "The perfect fit, if you're looking to play against Cinderella. (Castle of Dreams)" },
    { "Spaceship Board", nullptr },
    { "Toon Board",      nullptr },
    { "Hunny Pot Board", nullptr },
    { "Skull Board",     nullptr },
    { "Secret Board",    nullptr },
};
constexpr int kCbBoardCount = 7;
const char* const kCbCharNames[] = { nullptr, "Ventus", "Aqua", "Terra" };  // by character id byte

// -- widget location ----------------------------------------------------------

struct CbWidgetSlot { uint32_t base = 0; int lastScan = -100; };
CbWidgetSlot g_cbTitleW, g_cbTurnW, g_cbHandW, g_cbOptW, g_cbStripW, g_cbSetupW, g_cbDlgW;
int g_cbFrame = 0;        // WatchBoard call counter (scan throttle)
int g_cbLastRecScan = -100;  // frame of the last cold records scan

bool CbListShape(uint32_t base, uint32_t count, uint32_t table, int* cursor)
{
    if (u32(base) != kCbClassPtr) return false;
    if (u32(base + 0x1C) != (count << 16)) return false;
    if (u32(base + 0x20) != (0xFFFF0000u | count)) return false;
    if (u32(base + 0x24) != table) return false;
    uint32_t cur = u32(base + 0x14);
    if (cur != u32(base + 0x18) || cur >= count) return false;
    if (cursor) *cursor = (int) cur;
    return true;
}

bool CbSetupShape(uint32_t base, int* focus, int* board, int* goal)
{
    if (u32(base) != kCbClassPtr) return false;
    if (u32(base + 0x30) != 0x08BAF330) return false;
    if (u32(base + 0x34) != 0x08BAF3B0) return false;
    if (u32(base + 0x38) != 0x08BAF490) return false;
    uint32_t b = u32(base + 0x3C);
    if (b != u32(base + 0x40) || b >= (uint32_t) kCbBoardCount) return false;
    uint32_t g = u32(base + 0x60);
    if (g != u32(base + 0x64) || g > 8) return false;
    if (focus) *focus = (int) u32(base + 0x20);
    if (board) *board = (int) b;
    if (goal) *goal = (int) g;
    return true;
}

bool CbDialogShape(uint32_t base, int* choice)
{
    if (u32(base + 0x1C) != 0x00020000) return false;
    if (u32(base + 0x20) != 0xFFFF0002) return false;
    if (u32(base + 0x24) != 0x08BCD828) return false;
    uint32_t c = u32(base + 0x14);
    if (c != u32(base + 0x18) || c > 1) return false;
    if (choice) *choice = (int) c;
    return true;
}

bool CbShape(int kind, uint32_t base, uint32_t count, uint32_t table)
{
    return kind == 0 ? CbListShape(base, count, table, nullptr)
         : kind == 1 ? CbSetupShape(base, nullptr, nullptr, nullptr)
                     : CbDialogShape(base, nullptr);
}

// The live widget's base, or 0. The sticky cache is
// re-validated on every call; a miss scans the heap window
// (throttled) and demands exactly one shaped block.
uint32_t CbLocate(CbWidgetSlot& slot, int kind, uint32_t count, uint32_t table)
{
    if (slot.base) {
        if (CbShape(kind, slot.base, count, table)) return slot.base;
        slot.base = 0;
    }
    if (g_cbFrame - slot.lastScan < 10) return 0;
    slot.lastScan = g_cbFrame;
    uint32_t found = 0; int hits = 0;
    for (uint32_t b = kCbHeapLo; b + 0x68 <= kCbHeapHi; b += 4) {
        if (CbShape(kind, b, count, table)) { found = b; if (++hits > 1) break; }
    }
    if (hits == 1) slot.base = found;
    return slot.base;
}

// The board module's cheap presence probe: the menu-system
// "current screen" global points into the module heap exactly
// while the module owns the screen (title, setup and game);
// in the Hub field and camp it points at 0x099xxxxx objects.
bool CbModulePlausible(void)
{
    // Deliberately a few direct reads only: this gate runs
    // every frame, and the camp/field locators are far too
    // heavy to consult here. The widget and record shapes
    // remain the real arbiters.
    uint32_t scr = u32(0x08B5BE50);
    if (scr >= 0x09800000 && scr < 0x09860000) return true;
    uint32_t live = u32(kCbLiveAddr);
    if (live >= kUserRamBase && live < kUserRamEnd) return true;
    return g_cbTitleW.base || g_cbSetupW.base || g_cbTurnW.base || g_cbDlgW.base;
}

// -- the player records --------------------------------------------------------

uint32_t g_cbRecords = 0;      // base of the first player record
int g_cbHumans = -1;           // record index of the human player
int g_cbPlayers = 0;           // records present (2 or 3)

bool CbRecPresent(uint32_t r, bool* human, int* id, int* ord)
{
    if (u32(r) > 999999) return false;
    int i = (int) u8(r + 8);
    if (i == 0) return false;
    if (u16(r + 0x0E) >= kDefCount) return false;   // standing panel's def
    for (int s = 0; s < 5; s++) {
        uint16_t h = u16(r + 0x18 + (uint32_t) s * 2);
        if (h != 0xFFFF && h >= kDefCount) return false;
    }
    if (human) *human = u8(r + 9) == 0;
    if (id) *id = i;
    if (ord) *ord = (int) u8(r + 0x0B);
    return true;
}

bool CbRecordsShape(uint32_t base, int* players, int* humans)
{
    bool present[3], human[3];
    int id[3], ord[3];
    for (int i = 0; i < 3; i++) {
        uint32_t r = base + (uint32_t) i * kCbRecStride;
        present[i] = CbRecPresent(r, &human[i], &id[i], &ord[i]);
        if (!present[i] && u32(r) == 0 && u32(r + 8) == 0 && u32(r + 0x18) == 0)
            present[i] = false;  // an empty slot: simply absent
        else if (!present[i]) return false;
    }
    if (!present[0] || !present[1]) return false;
    int n = present[2] ? 3 : 2;
    int hum = 0;
    for (int i = 0; i < n; i++) if (human[i]) hum++;
    if (hum != 1) return false;
    for (int i = 0; i < n; i++) {
        if (ord[i] < 0 || ord[i] >= n) return false;
        for (int j = i + 1; j < n; j++) {
            if (id[i] == id[j] || ord[i] == ord[j]) return false;
        }
    }
    if (players) *players = n;
    if (humans) {
        *humans = -1;
        for (int i = 0; i < n; i++) if (human[i]) *humans = i;
    }
    return true;
}

uint32_t CbRecords(void)
{
    int p, h;
    if (g_cbRecords && CbRecordsShape(g_cbRecords, &p, &h)) {
        g_cbPlayers = p; g_cbHumans = h;
        return g_cbRecords;
    }
    g_cbRecords = 0;
    if (g_cbFrame - g_cbLastRecScan < 30) return 0;
    g_cbLastRecScan = g_cbFrame;
    uint32_t found = 0; int hits = 0;
    for (uint32_t b = kCbRecWinLo; b + 3 * kCbRecStride <= kCbRecWinHi; b += 4) {
        if (CbRecordsShape(b, nullptr, nullptr)) { found = b; if (++hits > 1) break; }
    }
    if (hits != 1) return 0;
    g_cbRecords = found;
    CbRecordsShape(found, &g_cbPlayers, &g_cbHumans);
    return g_cbRecords;
}

bool CbGameUp(void)
{
    uint32_t live = u32(kCbLiveAddr);
    if (live < kUserRamBase || live >= kUserRamEnd) return false;
    return CbRecords() != 0;
}

int CbRecGP(int i)   { return (int) u32(g_cbRecords + (uint32_t) i * kCbRecStride); }
int CbRecChar(int i) { return (int) u8(g_cbRecords + (uint32_t) i * kCbRecStride + 8); }
int CbRecOrd(int i)  { return (int) u8(g_cbRecords + (uint32_t) i * kCbRecStride + 0x0B); }
const char* CbRecName(int i)
{
    int id = CbRecChar(i);
    return (id >= 1 && id <= 3) ? kCbCharNames[id] : "Opponent";
}
// The record whose turn-order position is `ord`.
int CbByOrder(int ord)
{
    for (int i = 0; i < g_cbPlayers; i++) if (CbRecOrd(i) == ord) return i;
    return -1;
}
// The panel the record stands on, named through its def.
bool CbPanelName(int i, char* buf, size_t n)
{
    uint16_t def = u16(g_cbRecords + (uint32_t) i * kCbRecStride + 0x0E);
    return DefName(def, buf, n);
}

// -- screen gates and row texts -------------------------------------------------

bool CbTitleUp(int* row)
{
    if (!CbModulePlausible()) return false;
    uint32_t b = CbLocate(g_cbTitleW, 0, 3, 0x08BB1550);
    if (!b) return false;
    return CbListShape(b, 3, 0x08BB1550, row);
}

bool CbSetupUp(int* focus, int* board, int* goal)
{
    if (!CbModulePlausible()) return false;
    uint32_t b = CbLocate(g_cbSetupW, 1, 0, 0);
    if (!b) return false;
    return CbSetupShape(b, focus, board, goal);
}

bool CbDialogUp(int* choice)
{
    if (!CbModulePlausible()) return false;
    uint32_t b = CbLocate(g_cbDlgW, 2, 0, 0);
    if (!b) return false;
    return CbDialogShape(b, choice);
}

// The turn menu's verdict needs more than the bare shape.
// At a turn's start the game builds the menu object over a
// few frames: class, counts, table and cursor already
// match while its row-render sub-objects at +0x60 and
// +0x70 are still null — a shape-valid phantom that lived
// under 120 frames at the first turn of a live game
// (bench cbplay16) and fired a false "Your turn." The
// gate therefore also requires both sub-pointers to be
// attached and the object to hold for 15 consecutive
// frames. The frame stamp keeps the count at one per
// watcher frame; commands between frames read the last
// verdict.
uint32_t g_cbMenuBase = 0;
int g_cbMenuFrames = 0;
int g_cbMenuStamp = -1;

bool CbTurnMenuUp(int* row)
{
    if (!CbGameUp()) return false;
    uint32_t b = CbLocate(g_cbTurnW, 0, 4, 0x08BBE088);
    bool raw = b && CbListShape(b, 4, 0x08BBE088, row);
    if (raw) {
        uint32_t p60 = u32(b + 0x60), p70 = u32(b + 0x70);
        raw = p60 >= kUserRamBase && p60 < kUserRamEnd &&
              p70 >= kUserRamBase && p70 < kUserRamEnd;
    }
    if (g_cbFrame != g_cbMenuStamp) {
        g_cbMenuStamp = g_cbFrame;
        if (raw && b == g_cbMenuBase) g_cbMenuFrames++;
        else { g_cbMenuBase = raw ? b : 0; g_cbMenuFrames = raw ? 1 : 0; }
    }
    return raw && g_cbMenuFrames >= 15;
}

bool CbHandUp(int* row)
{
    if (!CbGameUp()) return false;
    uint32_t b = CbLocate(g_cbHandW, 0, 5, 0x08BCEE60);
    if (!b) return false;
    return CbListShape(b, 5, 0x08BCEE60, row);
}

bool CbOptionsUp(int* row)
{
    if (!CbGameUp()) return false;
    uint32_t b = CbLocate(g_cbOptW, 0, 5, 0x08BCF5A8);
    if (!b) return false;
    return CbListShape(b, 5, 0x08BCF5A8, row);
}

bool CbStripUp(int* row)
{
    if (!CbGameUp()) return false;
    uint32_t b = CbLocate(g_cbStripW, 0, 5, 0x08B8DB78);
    if (!b) return false;
    return CbListShape(b, 5, 0x08B8DB78, row);
}

void CbRowText(const char* const* items, int count, int row, char* buf, size_t n)
{
    if (row < 0 || row >= count) { snprintf(buf, n, "%d of %d.", row + 1, count); return; }
    snprintf(buf, n, "%s, %d of %d.", items[row], row + 1, count);
}

void CbBoardLine(int board, int goalIdx, char* buf, size_t n)
{
    if (board < 0 || board >= kCbBoardCount) { snprintf(buf, n, "Board %d of %d.", board + 1, kCbBoardCount); return; }
    int goal = 5000 + 5000 * (goalIdx < 0 ? 0 : goalIdx);
    if (kCbBoards[board].desc)
        snprintf(buf, n, "%s. Goal %d GP. %s.", kCbBoards[board].name, goal, kCbBoards[board].desc);
    else
        snprintf(buf, n, "%s. Goal %d GP.", kCbBoards[board].name, goal);
}

void CbGoalLine(int goalIdx, char* buf, size_t n)
{
    snprintf(buf, n, "GP Goal: %d.", 5000 + 5000 * goalIdx);
}

// The order line: "Playing order: Ventus. Terra. Aqua."
void CbOrderLine(char* buf, size_t n)
{
    char tmp[160]; tmp[0] = 0;
    for (int o = 0; o < g_cbPlayers; o++) {
        int r = CbByOrder(o);
        if (r < 0) continue;
        char part[40];
        snprintf(part, sizeof(part), "%s%s", o ? ". " : "", CbRecName(r));
        strncat(tmp, part, sizeof(tmp) - strlen(tmp) - 1);
    }
    snprintf(buf, n, "Playing order: %s.", tmp);
}

// -- watcher state ---------------------------------------------------------------

bool g_cbTitleUp = false;  int g_cbTitleDown = 1 << 30;
bool g_cbSetupUp = false;  int g_cbSetupDown = 1 << 30;
int g_cbSetupFocus = -1;
bool g_cbDlgUp = false;    int g_cbDlgDown = 1 << 30;
char g_cbDlgQuestion[128] = "";   // the arrival line's question, for queries/echoes
bool g_cbGameUp = false;   int g_cbGameDown = 1 << 30;
bool g_cbOrderSpoken = false;
int g_cbOrderPerm[3] = { -1, -1, -1 };
int g_cbOrderCand[3] = { -1, -1, -1 };  int g_cbOrderCandFrames = 0;
bool g_cbMenuUp = false;   int g_cbMenuDown = 1 << 30;
bool g_cbHandUp = false;   int g_cbHandDown = 1 << 30;
bool g_cbOptUp = false;    int g_cbOptDown = 1 << 30;
bool g_cbStripUp = false;  int g_cbStripDown = 1 << 30;
int g_cbTurn = -1;                 // record index whose turn it is (estimated)
int g_cbTurnGP[3] = { 0, 0, 0 };   // GP snapshot at the current turn's start
int g_cbSeenGP[3] = { 0, 0, 0 };   // last-seen record state (turn attribution)
int g_cbSeenDef[3] = { -1, -1, -1 };
int g_cbSeenHand[3] = { -1, -1, -1 };
int g_cbLandDef = -1;              // human's standing-panel def, settled
int g_cbLandCand = -1;     int g_cbLandCandFrames = 0;
bool g_cbPromptArmed = false; int g_cbPromptArmFrames = 0;
bool g_cbLandedSpoken = true;      // the current landing already announced

void CbReset(void)
{
    g_cbTitleW.base = g_cbTurnW.base = g_cbHandW.base = g_cbOptW.base = 0;
    g_cbStripW.base = g_cbSetupW.base = g_cbDlgW.base = 0;
    g_cbTitleW.lastScan = g_cbTurnW.lastScan = g_cbHandW.lastScan = -100;
    g_cbOptW.lastScan = g_cbStripW.lastScan = g_cbSetupW.lastScan = -100;
    g_cbDlgW.lastScan = -100;
    g_cbRecords = 0; g_cbHumans = -1; g_cbPlayers = 0;
    g_cbTitleUp = g_cbSetupUp = g_cbDlgUp = g_cbGameUp = false;
    g_cbTitleDown = g_cbSetupDown = g_cbDlgDown = g_cbGameDown = 1 << 30;
    g_cbSetupFocus = -1;
    g_cbDlgQuestion[0] = 0;
    g_cbOrderSpoken = false;
    g_cbOrderPerm[0] = g_cbOrderPerm[1] = g_cbOrderPerm[2] = -1;
    g_cbOrderCand[0] = g_cbOrderCand[1] = g_cbOrderCand[2] = -1;
    g_cbOrderCandFrames = 0;
    g_cbMenuUp = g_cbHandUp = g_cbOptUp = g_cbStripUp = false;
    g_cbMenuDown = g_cbHandDown = g_cbOptDown = g_cbStripDown = 1 << 30;
    g_cbTurn = -1;
    g_cbMenuBase = 0; g_cbMenuFrames = 0; g_cbMenuStamp = -1;
    for (int i = 0; i < 3; i++) {
        g_cbTurnGP[i] = g_cbSeenGP[i] = 0;
        g_cbSeenDef[i] = g_cbSeenHand[i] = -1;
    }
    g_cbLandDef = -1; g_cbLandCand = -1; g_cbLandCandFrames = 0;
    g_cbPromptArmed = false; g_cbPromptArmFrames = 0;
    g_cbLandedSpoken = true;
}

// How many hand cards a record currently holds (non-empty
// slots of its five-card register).
int CbHandCount(int i)
{
    int n = 0;
    uint32_t r = g_cbRecords + (uint32_t) i * kCbRecStride;
    for (int s = 0; s < 5; s++)
        if (u16(r + 0x18 + (uint32_t) s * 2) != 0xFFFF) n++;
    return n;
}

void CbSummarizeTurn(int who, const int before[3], const int after[3]);

// Call a new current player: speak the playing order first
// when it has not been spoken (a continued game), summarize
// the outgoing opponent, then name the incoming turn.
void CbCallTurn(int turn)
{
    if (!g_cbOrderSpoken) {
        char line[160]; CbOrderLine(line, sizeof(line));
        Say(line, "menu", oga::Priority::High);
        for (int i = 0; i < 3; i++) g_cbOrderPerm[i] = i < g_cbPlayers ? CbByOrder(i) : -1;
        g_cbOrderSpoken = true;
    }
    int after[3];
    for (int i = 0; i < 3; i++) after[i] = i < g_cbPlayers ? CbRecGP(i) : 0;
    if (g_cbTurn >= 0 && g_cbTurn != g_cbHumans)
        CbSummarizeTurn(g_cbTurn, g_cbTurnGP, after);
    g_cbTurn = turn;
    for (int i = 0; i < 3; i++) g_cbTurnGP[i] = after[i];
    char line[128];
    if (turn == g_cbHumans) snprintf(line, sizeof(line), "Your turn.");
    else snprintf(line, sizeof(line), "%s's turn.", CbRecName(turn));
    Say(line, "menu", oga::Priority::High);
}

// Summarize a finished non-human turn from the GP deltas
// (clean single patterns only; anything else stays silent).
void CbSummarizeTurn(int who, const int before[3], const int after[3])
{
    int d[3];
    for (int i = 0; i < 3; i++) d[i] = after[i] - before[i];
    if (d[who] == 0) return;
    char line[192];
    if (d[who] < 0) {
        int gain = -1;
        for (int i = 0; i < g_cbPlayers; i++) if (i != who && d[i] == -d[who]) gain = i;
        if (gain >= 0) {
            snprintf(line, sizeof(line), "%s paid %d GP to %s.",
                     CbRecName(who), -d[who], CbRecName(gain));
            Say(line, "menu", oga::Priority::Normal);
        } else {
            bool othersQuiet = true;
            for (int i = 0; i < g_cbPlayers; i++) if (i != who && d[i] != 0) othersQuiet = false;
            if (othersQuiet) {
                snprintf(line, sizeof(line), "%s spent %d GP.", CbRecName(who), -d[who]);
                Say(line, "menu", oga::Priority::Normal);
            }
        }
    } else {
        bool othersQuiet = true;
        for (int i = 0; i < g_cbPlayers; i++) if (i != who && d[i] != 0) othersQuiet = false;
        if (othersQuiet) {
            snprintf(line, sizeof(line), "%s gained %d GP.", CbRecName(who), d[who]);
            Say(line, "menu", oga::Priority::Normal);
        }
    }
}

void WatchBoard(void)
{
    g_cbFrame++;
    // Title.
    int row = -1;
    bool titleUp = CbTitleUp(&row);
    if (titleUp) {
        if (!g_cbTitleUp && g_cbTitleDown >= 30) {
            char r[96]; CbRowText(kCbTitleItems, 3, row, r, sizeof(r));
            char line[160];
            snprintf(line, sizeof(line), "Command Board. %s", r);
            Say(line, "menu", oga::Priority::High);
        }
        g_cbTitleUp = true; g_cbTitleDown = 0;
    } else {
        g_cbTitleUp = false;
        if (g_cbTitleDown < (1 << 30)) g_cbTitleDown++;
    }
    // Setup (the arrival and focus transitions speak; cursor
    // moves echo through the nav commands).
    int focus = -1, board = -1, goal = -1;
    bool setupUp = CbSetupUp(&focus, &board, &goal);
    if (setupUp) {
        if (!g_cbSetupUp && g_cbSetupDown >= 30) {
            char bl[256]; CbBoardLine(board, goal, bl, sizeof(bl));
            char line[320];
            snprintf(line, sizeof(line), "Command Board. Select a board. %s", bl);
            Say(line, "menu", oga::Priority::High);
            g_cbSetupFocus = focus;
        } else if (focus != g_cbSetupFocus) {
            if (focus == 3) {
                char gl[64]; CbGoalLine(goal, gl, sizeof(gl));
                char line[128];
                snprintf(line, sizeof(line), "Select a goal. %s", gl);
                Say(line, "menu", oga::Priority::High);
            }
            g_cbSetupFocus = focus;
        }
        g_cbSetupUp = true; g_cbSetupDown = 0;
    } else {
        g_cbSetupUp = false;
        if (g_cbSetupDown < (1 << 30)) g_cbSetupDown++;
    }
    // Game state prime.
    bool gameUp = CbGameUp();
    if (!gameUp) {
        if (g_cbGameUp) CbReset();
        g_cbGameUp = false;
        if (g_cbGameDown < (1 << 30)) g_cbGameDown++;
    } else {
        if (!g_cbGameUp) {
            g_cbTurn = -1;
            for (int i = 0; i < 3; i++) {
                g_cbTurnGP[i] = g_cbSeenGP[i] = i < g_cbPlayers ? CbRecGP(i) : 0;
                g_cbSeenDef[i] = i < g_cbPlayers
                    ? (int) u16(g_cbRecords + (uint32_t) i * kCbRecStride + 0x0E) : -1;
                g_cbSeenHand[i] = i < g_cbPlayers ? CbHandCount(i) : -1;
            }
            if (g_cbHumans >= 0)
                g_cbLandDef = (int) u16(g_cbRecords + (uint32_t) g_cbHumans * kCbRecStride + 0x0E);
        }
        g_cbGameUp = true; g_cbGameDown = 0;
    }
    if (gameUp) {
        // Turn estimation. The turn menu witnesses the human's
        // turn directly. Otherwise the turn is attributed to
        // the order-successor record whose GP, panel or hand
        // first changes; a change to the current player's own
        // record is their turn continuing, and a change to
        // anyone else's is an effect of it (a toll paid to
        // them), never a new turn.
        if (CbTurnMenuUp(nullptr)) {
            if (g_cbTurn != g_cbHumans) CbCallTurn(g_cbHumans);
        } else {
            int changed = 0, moved = 0;
            for (int i = 0; i < g_cbPlayers; i++) {
                int gp = CbRecGP(i);
                int def = (int) u16(g_cbRecords + (uint32_t) i * kCbRecStride + 0x0E);
                int hand = CbHandCount(i);
                if (gp != g_cbSeenGP[i] || def != g_cbSeenDef[i] || hand != g_cbSeenHand[i])
                    changed |= 1 << i;
                // A turn opens with a move or a card play,
                // never with money alone: payments change GP
                // only, so attribution requires a panel or
                // hand change, not a GP-only one.
                if (def != g_cbSeenDef[i] || hand != g_cbSeenHand[i])
                    moved |= 1 << i;
                g_cbSeenGP[i] = gp; g_cbSeenDef[i] = def; g_cbSeenHand[i] = hand;
            }
            if (changed && g_cbTurn >= 0 && !(changed & (1 << g_cbTurn))) {
                int only = -1, count = 0;
                for (int i = 0; i < g_cbPlayers; i++)
                    if (changed & (1 << i)) { only = i; count++; }
                if (count == 1 && (moved & (1 << only))) {
                    int succ = CbByOrder((CbRecOrd(g_cbTurn) + 1) % g_cbPlayers);
                    if (only == succ) CbCallTurn(only);
                }
            } else if (changed && g_cbTurn < 0) {
                int only = -1, count = 0;
                for (int i = 0; i < g_cbPlayers; i++)
                    if (changed & (1 << i)) { only = i; count++; }
                if (count == 1 && (moved & (1 << only))) CbCallTurn(only);
            }
        }
        // A shuffled order replaces the announced one: speak
        // the final order once the new permutation holds.
        if (g_cbOrderSpoken) {
            bool same = true, candSame = true;
            for (int i = 0; i < 3; i++) {
                int cur = i < g_cbPlayers ? CbByOrder(i) : -1;
                if (cur != g_cbOrderPerm[i]) same = false;
                if (cur != g_cbOrderCand[i]) candSame = false;
            }
            if (same) {
                g_cbOrderCandFrames = 0;
            } else if (!candSame) {
                for (int i = 0; i < 3; i++) g_cbOrderCand[i] = i < g_cbPlayers ? CbByOrder(i) : -1;
                g_cbOrderCandFrames = 0;
            } else if (++g_cbOrderCandFrames >= 45) {
                char line[160]; CbOrderLine(line, sizeof(line));
                Say(line, "menu", oga::Priority::High);
                for (int i = 0; i < 3; i++) g_cbOrderPerm[i] = g_cbOrderCand[i];
                g_cbOrderCandFrames = 0;
            }
        }
        // The human's landing watch: the standing-panel def,
        // settled for 45 frames after a change on their turn.
        if (g_cbHumans >= 0) {
            int def = (int) u16(g_cbRecords + (uint32_t) g_cbHumans * kCbRecStride + 0x0E);
            if (def != g_cbLandCand) {
                g_cbLandCand = def; g_cbLandCandFrames = 0;
                if (g_cbTurn == g_cbHumans && g_cbLandDef >= 0 && def != g_cbLandDef) {
                    g_cbPromptArmed = true; g_cbPromptArmFrames = 0;
                    g_cbLandedSpoken = false;
                }
            } else if (g_cbLandCandFrames < (1 << 30)) g_cbLandCandFrames++;
            if (g_cbLandCandFrames == 45 && def != g_cbLandDef) {
                g_cbLandDef = def;
                if (!g_cbLandedSpoken && !g_cbDlgUp) {
                    char panel[48];
                    if (CbPanelName(g_cbHumans, panel, sizeof(panel))) {
                        char line[128];
                        snprintf(line, sizeof(line), "You landed on %s.", panel);
                        Say(line, "menu", oga::Priority::High);
                    }
                    g_cbLandedSpoken = true;
                }
            }
            if (g_cbPromptArmed && g_cbPromptArmFrames++ > 1200) g_cbPromptArmed = false;
        }
    }
    // Dialogs: the question is told by context.
    int choice = -1;
    bool dlgUp = CbDialogUp(&choice);
    if (dlgUp && !g_cbDlgUp && g_cbDlgDown >= 30) {
        char line[256];
        line[0] = 0;
        if (g_cbSetupUp && g_cbSetupFocus == 4) {
            snprintf(g_cbDlgQuestion, sizeof(g_cbDlgQuestion), "Play on this board?");
            snprintf(line, sizeof(line), "%s %s", g_cbDlgQuestion, choice == 0 ? "Yes." : "No.");
        } else if (gameUp && g_cbStripUp) {
            snprintf(g_cbDlgQuestion, sizeof(g_cbDlgQuestion), "Are you sure?");
            snprintf(line, sizeof(line), "%s %s", g_cbDlgQuestion, choice == 0 ? "Yes." : "No.");
        } else if (gameUp && g_cbPromptArmed) {
            char panel[48];
            if (CbPanelName(g_cbHumans, panel, sizeof(panel)))
                snprintf(g_cbDlgQuestion, sizeof(g_cbDlgQuestion),
                         "You have landed on a Command Panel: %s. Place a command card on this panel?", panel);
            else
                snprintf(g_cbDlgQuestion, sizeof(g_cbDlgQuestion), "Place a command card on this panel?");
            snprintf(line, sizeof(line), "%s %s", g_cbDlgQuestion, choice == 0 ? "Yes." : "No.");
            g_cbLandedSpoken = true;
            g_cbPromptArmed = false;
        } else if (gameUp && !g_cbOrderSpoken) {
            char ol[160]; CbOrderLine(ol, sizeof(ol));
            snprintf(g_cbDlgQuestion, sizeof(g_cbDlgQuestion), "Shuffle the playing order?");
            snprintf(line, sizeof(line), "%s %s %s", ol, g_cbDlgQuestion, choice == 0 ? "Yes." : "No.");
            for (int i = 0; i < 3; i++) g_cbOrderPerm[i] = i < g_cbPlayers ? CbByOrder(i) : -1;
            g_cbOrderSpoken = true;
        }
        if (line[0]) Say(line, "menu", oga::Priority::High);
        g_cbDlgUp = true; g_cbDlgDown = 0;
    } else if (dlgUp) {
        g_cbDlgUp = true; g_cbDlgDown = 0;
    } else {
        g_cbDlgUp = false;
        if (g_cbDlgDown < (1 << 30)) g_cbDlgDown++;
    }
    if (!gameUp) return;
    // Turn menu / Hand / Options / strip arrivals.
    int mrow = -1;
    bool menuUp = CbTurnMenuUp(&mrow);
    if (menuUp) {
        if (!g_cbMenuUp && g_cbMenuDown >= 30) {
            char r[96]; CbRowText(kCbTurnItems, 4, mrow, r, sizeof(r));
            char line[160];
            snprintf(line, sizeof(line), "Command Board. %s", r);
            Say(line, "menu", oga::Priority::High);
        }
        g_cbMenuUp = true; g_cbMenuDown = 0;
    } else {
        g_cbMenuUp = false;
        if (g_cbMenuDown < (1 << 30)) g_cbMenuDown++;
    }
    int hrow = -1;
    bool handUp = CbHandUp(&hrow);
    if (handUp) {
        // Arrivals speak only on the human's own turn: the
        // same screens open for the AI players' picks, and
        // narrating those as if they were yours misleads.
        if (!g_cbHandUp && g_cbHandDown >= 30 && g_cbTurn == g_cbHumans) {
            char r[96]; CbRowText(kCbPlayItems, 5, hrow, r, sizeof(r));
            char line[160];
            snprintf(line, sizeof(line), "Hand. %s", r);
            Say(line, "menu", oga::Priority::High);
        }
        g_cbHandUp = true; g_cbHandDown = 0;
    } else {
        g_cbHandUp = false;
        if (g_cbHandDown < (1 << 30)) g_cbHandDown++;
    }
    int orow = -1;
    bool optUp = CbOptionsUp(&orow);
    if (optUp) {
        if (!g_cbOptUp && g_cbOptDown >= 30 && g_cbTurn == g_cbHumans) {
            char r[96]; CbRowText(kCbOptItems, 5, orow, r, sizeof(r));
            char line[160];
            snprintf(line, sizeof(line), "Options. %s", r);
            Say(line, "menu", oga::Priority::High);
        }
        g_cbOptUp = true; g_cbOptDown = 0;
    } else {
        g_cbOptUp = false;
        if (g_cbOptDown < (1 << 30)) g_cbOptDown++;
    }
    int srow = -1;
    bool stripUp = CbStripUp(&srow);
    if (stripUp) {
        if (!g_cbStripUp && g_cbStripDown >= 30 && g_cbTurn == g_cbHumans) {
            char line[128];
            snprintf(line, sizeof(line), "Select a command card. Card %d of 5.", srow + 1);
            Say(line, "menu", oga::Priority::High);
        }
        g_cbStripUp = true; g_cbStripDown = 0;
    } else {
        g_cbStripUp = false;
        if (g_cbStripDown < (1 << 30)) g_cbStripDown++;
    }
}

// The board game's WhereAmI line. Returns false when no board
// screen owns the moment (the caller falls through).
bool CbWhereAmI(char* line, size_t n)
{
    int choice = -1;
    if (CbDialogUp(&choice)) {
        const char* yn = choice == 0 ? "Yes." : "No.";
        if (g_cbDlgQuestion[0]) {
            snprintf(line, n, "%s %s", g_cbDlgQuestion, yn);
            return true;
        }
        if (g_cbSetupUp && g_cbSetupFocus == 4) { snprintf(line, n, "Play on this board? %s", yn); return true; }
        snprintf(line, n, "Command Board. %s", yn);
        return true;
    }
    int trow = -1;
    if (CbTitleUp(&trow)) {
        char r[96]; CbRowText(kCbTitleItems, 3, trow, r, sizeof(r));
        snprintf(line, n, "Command Board. %s", r);
        return true;
    }
    int focus = -1, board = -1, goal = -1;
    if (CbSetupUp(&focus, &board, &goal)) {
        if (focus == 3) {
            char gl[64]; CbGoalLine(goal, gl, sizeof(gl));
            snprintf(line, n, "Command Board. Select a goal. %s", gl);
        } else {
            char bl[256]; CbBoardLine(board, goal, bl, sizeof(bl));
            snprintf(line, n, "Command Board. Select a board. %s", bl);
        }
        return true;
    }
    if (!CbGameUp()) return false;
    int srow = -1;
    if (CbStripUp(&srow)) {
        snprintf(line, n, "Command Board. Select a command card. Card %d of 5.", srow + 1);
        return true;
    }
    int hrow = -1;
    if (CbHandUp(&hrow)) {
        char r[96]; CbRowText(kCbPlayItems, 5, hrow, r, sizeof(r));
        snprintf(line, n, "Command Board. Hand. %s", r);
        return true;
    }
    int orow = -1;
    if (CbOptionsUp(&orow)) {
        char r[96]; CbRowText(kCbOptItems, 5, orow, r, sizeof(r));
        snprintf(line, n, "Command Board. Options. %s", r);
        return true;
    }
    int mrow = -1;
    if (CbTurnMenuUp(&mrow)) {
        char r[96]; CbRowText(kCbTurnItems, 4, mrow, r, sizeof(r));
        snprintf(line, n, "Command Board. Your turn. %s", r);
        return true;
    }
    // No sub-screen: the position line — whose turn, the
    // panel you stand on, and every player's GP in turn order.
    char who[64];
    int turn = g_cbTurn;
    if (turn == g_cbHumans) snprintf(who, sizeof(who), "Your turn. ");
    else if (turn >= 0) snprintf(who, sizeof(who), "%s's turn. ", CbRecName(turn));
    else who[0] = 0;
    char panel[48];
    char gpList[128]; gpList[0] = 0;
    for (int o = 0; o < g_cbPlayers; o++) {
        int r = CbByOrder(o);
        if (r < 0) continue;
        char part[48];
        snprintf(part, sizeof(part), "%s%s: %d GP", o ? ". " : "", CbRecName(r), CbRecGP(r));
        strncat(gpList, part, sizeof(gpList) - strlen(gpList) - 1);
    }
    if (CbPanelName(g_cbHumans, panel, sizeof(panel)))
        snprintf(line, n, "Command Board. %sYou are on %s. %s.", who, panel, gpList);
    else
        snprintf(line, n, "Command Board. %s%s.", who, gpList);
    return true;
}

// The board game's nav echo (MenuNext/Prev/Left/Right): the
// row the player landed on, in the same convention as the
// other menus. Returns false when no board screen owns the
// D-pad (the caller falls through).
bool CbMenuNav(char* row, size_t n)
{
    int choice = -1;
    if (CbDialogUp(&choice)) {
        snprintf(row, n, "%s", choice == 0 ? "Yes." : "No.");
        return true;
    }
    int trow = -1;
    if (CbTitleUp(&trow)) { CbRowText(kCbTitleItems, 3, trow, row, n); return true; }
    int focus = -1, board = -1, goal = -1;
    if (CbSetupUp(&focus, &board, &goal)) {
        if (focus == 3) CbGoalLine(goal, row, n);
        else CbBoardLine(board, goal, row, n);
        return true;
    }
    if (!CbGameUp()) return false;
    int srow = -1;
    if (CbStripUp(&srow)) { snprintf(row, n, "Card %d of 5.", srow + 1); return true; }
    int hrow = -1;
    if (CbHandUp(&hrow)) { CbRowText(kCbPlayItems, 5, hrow, row, n); return true; }
    int orow = -1;
    if (CbOptionsUp(&orow)) { CbRowText(kCbOptItems, 5, orow, row, n); return true; }
    int mrow = -1;
    if (CbTurnMenuUp(&mrow)) { CbRowText(kCbTurnItems, 4, mrow, row, n); return true; }
    return false;
}

// ---- the shops (VERIFIED 2026-10-24, bench shop1-shop3, Medal Shop) --
//
// Both shops run SHOP.ELF, a module of its own: the camp/event menu
// system is DOWN while a shop is open (the menu-active word reads 0
// and the screen registry holds garbage), so the gate is shop-native
// and fail-closed on three legs:
//   1. the stock table is in RAM: an "SHP" file image (magic "SHP\0",
//      u16 version 1, u16 type at +0x06 — 2 = the Medal Shop — then a
//      u32 record count and 8-byte records {u16 id, u8 kind
//      (1 = command, 0 = item), u8 sub, u16 v4, u16 price},
//      zero-terminated). No pointer to it exists; it is found by
//      signature scan with per-record sanity, unique (or disam-
//      biguated by the widget's row count). Display order = table
//      order.
//   2. the SHOP.ELF phase words (fixed BSS addresses while the
//      module is loaded, garbage otherwise): u16 @ 0x09918E8C is 2
//      at the Command Shop's top level, 4 in the buy list, 5 in the
//      sell list and 6/7 in the buy/sell quantity+confirmation
//      steps; u16 @ 0x09918F10 is 5 at the top level, 40 on a
//      confirmation step, 30 while a confirmed transaction
//      processes, 10 elsewhere. Camp, field and title dumps all
//      read garbage at these words.
//   3. the shop widget (a heap block that FLOATS per lineage, so it
//      is signature-scanned in the heap window, never hardcoded):
//      cursor u16 @ +0x34, current-list count u16 @ +0x36 with a
//      mirror @ +0x3E, tab u16 @ +0x04 (lists only), and a
//      back-pointer @ +0x2C to the shop state block 0x09918AC0 —
//      and the count must equal the row count this adapter computes
//      for that tab. The Command Shop has THREE widgets (top, buy,
//      sell); the Medal Shop has one. Widgets LINGER after the shop
//      closes, so the phase word is what drops the gate on exit.
// The Medal Shop (Mirage Arena Hub Moogle) is BUY-ONLY and opens
// directly on the Buy list — no Buy/Sell top level (Triangle,
// Square and Select do nothing there; START leaves into camp, and
// Circle exits). Tabs (L/R): 0 All, 1 Attacks, 2 Magic, 3 Items,
// 4 Friendship, 5 Movement, 6 Defense, 7 Reprisals, 8 Shotlocks.
// Rows are NOT materialized per tab anywhere in RAM; they are rebuilt
// by filtering the stock table through the command def table's
// family/sub bytes (the same scheme MeldListBuild uses): family 2
// sub 1 = Attacks, sub 2 = Magic, sub 3 = Items (battle item
// commands — the Moogle's stock settled this), subs 12/13 =
// Friendship; family 3 sub 4 = Movement, sub 5 = Defense, sub 6 =
// Reprisals; anything else (family 5 shotlocks) lands in Shotlocks.
// The Medal Shop's plain items (tickets/crystals/recipes, ids >=
// 0x0100) appear only in All.
// Names: commands from the def table (DefName); ITEM names ride the
// resident message tables the dialogue work decoded — category
// 0x2800, message id 0x28000000 | item_id. All 16 Medal Shop items
// resolve and match their screenshots.
// The buy flow is three steps: list -> quantity (u16 @ 0x09918F14;
// up/down +-1, left/right +-10, no 99 cap observed — a slam reached
// 121) -> confirmation, which DEFAULTS TO NO (proven; the cursor is
// one-hot floats in a heap block that floats, so — like the meld
// confirm — the choice is tracked from the player's own presses,
// starting at No). A completed purchase is proven by the currency
// word dropping (medals u32 @ player block +0x287E0, munny u32 @
// +0x287DC). "In stock" for a command is its instance count in the
// command pool (verified live: a purchase moved it 2 -> 3).

// The save/load gate (defined below) is a stand-down witness here.
// The dialogue census (defined below) resolves the item names.
bool SaveLoadUp(void);
bool ShopUp(void);
int ShopTab(void);
int ShopCursor(void);
const char* ShopCurWord(void);
extern int g_dlgIdCount;
extern uint32_t g_dlgIds[];
extern uint32_t g_dlgStrAddr[];
extern bool g_dlgTablesValid;
void DlgRebuildTables(void);
bool DlgDecode(uint32_t addr, char* out, size_t n);

constexpr uint32_t kShopPhaseAddr = 0x09918E8C;   // 2 top, 4 buy, 5 sell, 6/7 qty+confirm
constexpr uint32_t kShopSubAddr   = 0x09918F10;   // 5 top, 40 confirm, 30 processing, else 10
constexpr uint32_t kShopQtyAddr   = 0x09918F14;   // quantity word
constexpr uint32_t kShopHeapLo    = 0x09910000;   // widget scan window
constexpr uint32_t kShopHeapHi    = 0x09960000;
const char* const kShopTabNames[9] = {
    "All", "Attacks", "Magic", "Items", "Friendship",
    "Movement", "Defense", "Reprisals", "Shotlocks",
};

uint32_t g_shopTable = 0;      // located SHP table base; 0 = none
int g_shopCount = 0;           // its record count
uint16_t g_shopType = 0;       // its type word (2 = Medal Shop)
int g_shopFmt = 0;             // record format: 8 = Medal records, 4 = id list
int g_shopListKind = -1;       // widget's list: 0 top, 1 buy, 2 sell
bool g_sellCountsValid = false; // cached sell-list tab counts are current
int g_sellTabCounts[9] = {};   // sell rows per tab (pool rebuild)
char g_shopSellName[128] = ""; // row on the table at sell-confirm entry
bool g_shopJustTransacted = false; // a Bought/Sold line just fired; the
                               // landing commit it belongs to stays silent
int g_shopSellQty = 0;         // quantity at sell-confirm entry
uint32_t g_shopWidget = 0;     // located widget base (sticky)
bool g_shopUp = false;
int g_shopDownFrames = 1 << 29;
int g_shopTab = -1;            // last announced tab
int g_shopPhase = 0;           // last seen step: 0 none, 1 list, 2 qty, 3 confirm
int g_shopPhaseCand = 0;       // candidate step awaiting stability
int g_shopPhaseCandFrames = 0; // consecutive frames the candidate held
bool g_shopConfYes = false;    // confirm choice, tracked from presses
char g_shopBuyName[128] = "";  // row on the table at confirm entry
int g_shopBuyQty = 0;          // quantity at confirm entry
uint32_t g_shopBalance = 0;    // last seen balance of the shop currency
bool g_shopBalancePrimed = false;

bool ShopRecPlausible(uint32_t rec)
{
    uint8_t kind = u8(rec + 2);
    if (kind > 1) return false;
    uint16_t price = u16(rec + 6);
    if (price % 10 != 0 || price > 100000) return false;
    uint16_t id = u16(rec);
    if (kind == 1) return id < (uint16_t) kDefCount;
    return (id >> 8) <= 0x32;
}

// ---- the munny Command Shop (VERIFIED 2026-10-24, bench shop14-16) --
//
// The Moogle shops that take munny are a different program shape from
// the Medal Shop: a TOP LEVEL first (Buy / Sell / Exit; phase word 2,
// sub 5), a Buy list (phase 4) and a Sell list (phase 5), each with
// its own quantity step (buy 6, sell 7) and Yes/No confirm (sub 40,
// default No; sub 30 while a confirmed transaction processes). The
// stock table is the same "SHP" magic but a different file format
// (cmdXXShop.bin): a bare list of def ids (u32 each, ascending), NO
// prices — prices are per-def game constants that sit in no flat
// table anywhere in RAM (exhaustively searched), so the 86 prices
// below were harvested from the live shop screens in the bench and
// are shipped as data, the same precedent as the room-name tables.
// Four stock ids are never displayed (meld-only/special commands:
// 94 Meteor Crash, 99 Chaos Blade, 110 Ars Solum, 130 Break Time —
// Break Time's price was never needed and is not in the table; a
// sell row for it speaks without a price). Sell price is computed:
// floor(buy * (level + 3) / 8) + (ability ? floor(buy / 4) : 0),
// fitted exactly on a dozen live sell rows. Sell rows are the
// command pool's family-2 and family-3 instances sorted by def id,
// pool order breaking ties (matched the live list row for row;
// the widget's 165 rows = 152 family-2 + 13 family-3 on the
// fixture — finishers and shotlocks are never sellable).
struct ShopPrice { uint16_t id; uint16_t price; };
const ShopPrice kShopPrices[] = {
    {91,100},{92,1200},{94,2500},{95,100},{96,150},{97,1100},{98,2000},
    {99,2500},{100,1700},{101,100},{102,200},{106,650},{108,700},
    {109,1500},{110,4000},{113,150},{115,200},{116,150},{117,700},
    {118,450},{119,450},{120,400},{122,1800},{123,2300},{125,1500},
    {126,1500},{127,1300},{128,2100},{129,1500},{131,100},{132,500},
    {133,1200},{134,2000},{135,1500},{137,1800},{138,100},{139,500},
    {140,1200},{142,150},{143,600},{144,1300},{146,150},{147,600},
    {148,1300},{149,600},{150,1000},{151,1000},{153,150},{154,600},
    {155,1300},{156,120},{157,550},{158,1250},{162,150},{163,600},
    {164,1300},{165,1500},{167,2200},{173,2500},{177,1500},{178,600},
    {179,600},{180,100},{181,150},{182,100},{183,150},{184,100},
    {185,500},{186,1200},{187,100},{188,30},{189,80},{190,150},
    {191,50},{192,160},{193,50},{194,250},{195,350},{196,120},
    {243,200},{245,500},{250,800},{260,200},{263,600},{271,1200},
    {276,1400},
};

int ShopPriceForDef(uint16_t id)
{
    for (size_t i = 0; i < sizeof(kShopPrices) / sizeof(kShopPrices[0]); i++)
        if (kShopPrices[i].id == id) return kShopPrices[i].price;
    return 0;
}

// Defs the Command Shop stocks but never displays.
bool ShopDefHidden(uint16_t id)
{
    return id == 94 || id == 99 || id == 110 || id == 130;
}

// Signature-scan RAM for the stock table; with a widget count hint
// (>0) a duplicate image is disambiguated by it. Fail-closed. Two
// formats share the header: the Medal Shop's 8-byte records and the
// Command Shop's bare u32 def-id list (type 1, strictly ascending,
// zero-terminated).
bool ShopFindTable(int countHint)
{
    uint32_t found = 0; int foundCount = 0; uint16_t foundType = 0;
    int foundFmt = 0;
    int hits = 0;
    for (uint32_t a = kUserRamBase; a + 12 <= kUserRamEnd; a += 4) {
        if (u32(a) != 0x00504853u) continue;            // "SHP\0"
        if (u16(a + 4) != 1) continue;
        int count = (int) u32(a + 8);
        if (count < 1 || count > 512) continue;
        // The type word selects the format, strictly: a bare id
        // list ALSO parses as plausible 8-byte records (every def
        // id is < 65536, so the misread kind byte is 0 and the
        // misread price is 0), so format must never be guessed
        // from record sanity alone.
        int fmt = 0;
        bool ok = true;
        if (u16(a + 6) == 1) {
            uint32_t prev = 0;
            for (int i = 0; i < count; i++) {
                uint32_t id = u32(a + 12 + (uint32_t) i * 4);
                if (id >= (uint32_t) kDefCount || id <= prev) { ok = false; break; }
                prev = id;
            }
            if (ok && u32(a + 12 + (uint32_t) count * 4) != 0) ok = false;
            if (ok) fmt = 4;
        } else {
            for (int i = 0; i < count; i++)
                if (!ShopRecPlausible(a + 12 + (uint32_t) i * 8)) { ok = false; break; }
            if (ok) {
                uint32_t tail = a + 12 + (uint32_t) count * 8;
                if (u32(tail) == 0 && u32(tail + 4) == 0) fmt = 8;
                else ok = false;
            }
        }
        if (!ok) continue;
        hits++;
        if (countHint > 0 && count != countHint) continue;
        found = a; foundCount = count; foundType = u16(a + 6); foundFmt = fmt;
    }
    if (countHint > 0) {
        if (!found) return false;
    } else if (hits != 1) return false;
    g_shopTable = found; g_shopCount = foundCount; g_shopType = foundType;
    g_shopFmt = foundFmt;
    return true;
}

// Stock entry `i`: its id, kind (1 = command def, 0 = plain item)
// and price (0 = unknown — only the Command Shop's table can hit
// that, for a def the bench never priced).
uint16_t ShopStockId(int i, uint8_t* kindOut, int* priceOut)
{
    uint16_t id = 0; uint8_t kind = 1; int price = 0;
    if (g_shopFmt == 4) {
        id = (uint16_t) u32(g_shopTable + 12 + (uint32_t) i * 4);
        price = ShopPriceForDef(id);
    } else {
        uint32_t rec = g_shopTable + 12 + (uint32_t) i * 8;
        id = u16(rec); kind = u8(rec + 2); price = u16(rec + 6);
    }
    if (kindOut) *kindOut = kind;
    if (priceOut) *priceOut = price;
    return id;
}

// Is stock entry `i` displayed at all? (The Command Shop hides four
// meld-only/special commands its table still lists.)
bool ShopStockVisible(int i)
{
    if (g_shopFmt != 4) return true;
    return !ShopDefHidden((uint16_t) u32(g_shopTable + 12 + (uint32_t) i * 4));
}

// The tab a stock entry belongs to, from its def family/sub (the
// classification the live tab counts proved in both shops; plain
// items are the Medal Shop's consumables rule).
int ShopEntryTab(uint16_t id, uint8_t kind)
{
    if (kind == 0) return id < 0x0100 ? 3 : -1;   // Medal items: All only
    uint32_t def = kDefTableAddr + (uint32_t) id * 16;
    uint8_t fam = u8(def), sub = u8(def + 1);
    if (fam == 2 && sub == 1) return 1;
    if (fam == 2 && sub == 2) return 2;
    if (fam == 2 && sub == 3) return 3;           // item commands
    if (fam == 2 && (sub == 12 || sub == 13)) return 4;
    if (fam == 3 && sub == 4) return 5;
    if (fam == 3 && sub == 5) return 6;
    if (fam == 3 && sub == 6) return 7;
    return 8;                                      // shotlocks & rest
}

bool ShopInTab(int i, int tab)
{
    if (!ShopStockVisible(i)) return false;
    if (tab == 0) return true;
    uint8_t kind;
    uint16_t id = ShopStockId(i, &kind, nullptr);
    return ShopEntryTab(id, kind) == tab;
}

int ShopTabCount(int tab)
{
    if (!g_shopTable) return -1;
    int n = 0;
    for (int i = 0; i < g_shopCount; i++)
        if (ShopInTab(i, tab)) n++;
    return n;
}

// ---- the sell list (Command Shop) -----------------------------------
// Rows are the command pool's family-2 and family-3 instances
// sorted by def id (165 = 152 + 13 on the fixture; family 1 and 5
// instances — finishers and shotlocks — are never sellable)
// (pool order breaks ties). Rebuilding per render is a full pool
// scan, so the per-tab counts are computed once per sell visit and
// cached; a completed sale invalidates them.

// One pass over the pool counting sell rows per tab.
void ShopSellCountsCompute(void)
{
    for (int t = 0; t < 9; t++) g_sellTabCounts[t] = 0;
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd) {
        g_sellCountsValid = true;
        return;
    }
    for (int i = 0; i < kPoolScan; i++) {
        uint32_t rec = pool + (uint32_t) i * 10;
        uint16_t id = u16(rec);
        if (!id || u8(rec + 3) != 0) continue;
        uint32_t def = kDefTableAddr + (uint32_t) id * 16;
        uint8_t fam = u8(def);
        if (fam != 2 && fam != 3) continue;   // command families only
        g_sellTabCounts[0]++;
        int t = ShopEntryTab(id, 1);
        if (t >= 1 && t <= 8) g_sellTabCounts[t]++;
    }
    g_sellCountsValid = true;
}

int ShopSellTabCount(int tab)
{
    if (!g_sellCountsValid) ShopSellCountsCompute();
    return g_sellTabCounts[tab];
}

// The unit sell price of one pool instance (def, level byte, ability
// id): floor(buy * (level + 3) / 8) + (ability ? floor(buy / 4) : 0).
// 0 when the def's buy price is unknown (Break Time) or the instance
// is an item stack (sub 3), whose per-unit price is unproven.
int ShopSellPrice(uint16_t id, int level, uint16_t ability)
{
    uint32_t def = kDefTableAddr + (uint32_t) id * 16;
    if (u8(def + 1) == 3) return 0;
    int buy = ShopPriceForDef(id);
    if (!buy) return 0;
    int p = buy * (level + 3) / 8;
    if (ability) p += buy / 4;
    return p;
}

// Fetch the sell row at `visual` in `tab`: def id, level/count byte
// and ability id of the pool instance it names.
bool ShopSellInstance(int tab, int visual, uint16_t* idOut, int* lvOut,
                      uint16_t* abOut)
{
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd) return false;
    int seen = -1;
    for (uint16_t want = 1; want < (uint16_t) kDefCount; want++) {
        if (tab != 0 && ShopEntryTab(want, 1) != tab) continue;
        uint32_t def = kDefTableAddr + (uint32_t) want * 16;
        if (u8(def) != 2 && u8(def) != 3) continue;
        for (int i = 0; i < kPoolScan; i++) {
            uint32_t rec = pool + (uint32_t) i * 10;
            if (u16(rec) != want || u8(rec + 3) != 0) continue;
            if (++seen == visual) {
                *idOut = want;
                *lvOut = u8(rec + 2);
                *abOut = u16(rec + 6);
                return true;
            }
        }
    }
    return false;
}

// "<name>, level <L>[, <ability>], <price> munny, i of n." — item
// stacks speak their held count instead of a level, without a price.
bool ShopSellRowText(int visual, char* buf, size_t n)
{
    if (!ShopUp()) return false;
    int tab = ShopTab();
    int count = ShopSellTabCount(tab);
    int v = visual >= 0 ? visual : ShopCursor();
    if (count <= 0 || v >= count) {
        snprintf(buf, n, "No items.");
        return true;
    }
    uint16_t id = 0, ability = 0; int level = 0;
    if (!ShopSellInstance(tab, v, &id, &level, &ability)) return false;
    char name[128] = "";
    bool named = DefName(id, name, sizeof(name));
    uint32_t def = kDefTableAddr + (uint32_t) id * 16;
    if (u8(def + 1) == 3) {                        // item stack
        if (named)
            snprintf(buf, n, "%s, %d in stock, %d of %d.",
                     name, level, v + 1, count);
        else
            snprintf(buf, n, "%d in stock, %d of %d.", level, v + 1, count);
        return true;
    }
    int price = ShopSellPrice(id, level, ability);
    char ab[128] = "";
    bool abNamed = ability && DefName(ability, ab, sizeof(ab));
    if (named && abNamed && price > 0)
        snprintf(buf, n, "%s, level %d, %s, %d %s, %d of %d.",
                 name, level, ab, price, ShopCurWord(), v + 1, count);
    else if (named && abNamed)
        snprintf(buf, n, "%s, level %d, %s, %d of %d.",
                 name, level, ab, v + 1, count);
    else if (named && price > 0)
        snprintf(buf, n, "%s, level %d, %d %s, %d of %d.",
                 name, level, price, ShopCurWord(), v + 1, count);
    else if (named)
        snprintf(buf, n, "%s, level %d, %d of %d.", name, level, v + 1, count);
    else
        snprintf(buf, n, "Level %d, %d of %d.", level, v + 1, count);
    return true;
}

// The widget at `w` corroborates the list it drives, per list kind:
// top (count is the fixed 3 rows), buy (count equals the computed
// stock rows for the widget's tab), sell (count equals the computed
// pool rows for the tab, from the cached rebuild). Every shop list
// widget also carries a back-pointer at +0x2C to the shop module's
// fixed state block (0x09918AC0) — the discriminator that separates
// the top-level widget from the dozens of static count-3 structs
// in the heap (live-caught: without it the top level never opens).
bool ShopWidgetValid(uint32_t w, bool fresh, int kind)
{
    if (u32(w + 0x2C) != 0x09918AC0) return false;
    uint16_t count = u16(w + 0x36);
    if (u16(w + 0x3E) != count) return false;
    if (fresh && count < 1) return false;      // zeros are everywhere
    uint16_t cursor = u16(w + 0x34);
    if (count == 0) { if (cursor != 0) return false; }
    else if (cursor >= count) return false;
    if (kind == 0) return count == 3;
    uint16_t tab = u16(w + 0x04);
    if (tab > 8) return false;
    if (kind == 1) return (int) count == ShopTabCount((int) tab);
    return (int) count == ShopSellTabCount((int) tab);
}

bool ShopLocateWidget(int kind)
{
    if (g_shopWidget && ShopWidgetValid(g_shopWidget, false, kind)) return true;
    g_shopWidget = 0;
    uint32_t found = 0; int hits = 0;
    for (uint32_t a = kShopHeapLo; a + 0x40 <= kShopHeapHi; a += 2) {
        if (!ShopWidgetValid(a, true, kind)) continue;
        hits++; found = a;
    }
    if (hits != 1) return false;
    g_shopWidget = found;
    return true;
}

bool ShopMedal(void) { return g_shopType == 2; }
const char* ShopName(void) { return ShopMedal() ? "Medal Shop" : "Moogle Shop"; }
const char* ShopCurWord(void) { return ShopMedal() ? "medals" : "munny"; }

uint32_t ShopBalance(void)
{
    if (!g_blockBase) return 0;
    return u32(g_blockBase + (ShopMedal() ? 0x287E0 : 0x287DC));
}

// The current step: 0 = not shop-shaped, 1 = buy list, 2 = buy
// quantity, 3 = buy confirmation, 4 = top level (Command Shop only),
// 5 = sell list, 6 = sell quantity, 7 = sell confirmation.
int ShopPhase(void)
{
    uint16_t main = u16(kShopPhaseAddr);
    if (main == 2) return 4;
    if (main == 4) return 1;
    if (main == 5) return 5;
    if (main == 6 || main == 7) {
        // Sub 40 is the Yes/No confirm; sub 30 is the transaction
        // being processed after Yes (frame-traced in bench shop19) —
        // both belong to the confirm step. Sub 10 (and a brief 0
        // while the dialog typesets) is the quantity step.
        uint16_t sub = u16(kShopSubAddr);
        bool conf = (sub == 40 || sub == 30);
        if (main == 6) return conf ? 3 : 2;
        return conf ? 7 : 6;
    }
    return 0;
}

// Which list the current step drives: 0 top, 1 buy, 2 sell.
int ShopListKindForPhase(int phase)
{
    if (phase == 4) return 0;
    if (phase == 5 || phase == 6 || phase == 7) return 2;
    return 1;
}

bool ShopUp(void)
{
    if (CampActive() || SaveLoadUp()) return false;
    if (u8(kWorldAddrA) == 0xFF && u8(kWorldAddrB) == 0xFF) return false;  // no world
    int phase = ShopPhase();
    if (phase == 0) { g_shopTable = 0; g_shopCount = 0; g_shopFmt = 0; return false; }
    if (!g_shopTable) {
        // First sighting: the widget's All count disambiguates a
        // duplicate table image; without it the scan must be unique.
        if (!ShopFindTable(0)) return false;
    }
    int kind = ShopListKindForPhase(phase);
    if (kind != g_shopListKind) { g_shopWidget = 0; g_shopListKind = kind; }
    if (!ShopLocateWidget(kind)) return false;
    return true;
}

int ShopTab(void)
{
    if (g_shopListKind == 0) return 0;         // the top list has no tab
    return (int) u16(g_shopWidget + 0x04);
}
int ShopCursor(void) { return (int) u16(g_shopWidget + 0x34); }

// Instances of command `id` in the pool = its "in stock" count.
int ShopCmdStock(uint16_t id)
{
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd) return -1;
    int n = 0;
    for (int i = 0; i < kPoolScan; i++) {
        uint32_t rec = pool + (uint32_t) i * 10;
        if (u16(rec) == id && u8(rec + 3) == 0) n++;
    }
    return n;
}

// Stock of an item COMMAND (family 2, sub 3 — Potion and friends):
// its pool instance's +2 byte is the held count, not a level
// (proven: the Potion instance reads 60 = the shop's "in stock").
int ShopItemCmdStock(uint16_t id)
{
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd) return -1;
    int n = 0; bool any = false;
    for (int i = 0; i < kPoolScan; i++) {
        uint32_t rec = pool + (uint32_t) i * 10;
        if (u16(rec) == id && u8(rec + 3) == 0) { n += u8(rec + 2); any = true; }
    }
    return any ? n : -1;
}

// Stock of a plain item (the Medal Shop's tickets and crystals).
// Owned items live in the player block's inventory section as
// category tables of {u16 id, u16 count} pairs, ids ascending
// within a category (proven by a purchase diff: the Shimmering
// Crystal's pair at block+0x26184 went 50 -> 51 on buying one).
// The pair must sit inside an ascending run of at least 3 pairs
// and be unique in the section; anything less stays unspoken.
int ShopItemStock(uint16_t id)
{
    if (!g_blockBase) return -1;
    uint32_t lo = g_blockBase + 0x20000, hi = g_blockBase + 0x29000;
    int found = -1; int hits = 0;
    for (uint32_t a = lo; a + 4 <= hi; a += 2) {
        if (u16(a) != id) continue;
        int count = (int) u16(a + 2);
        if (count > 999) continue;
        // Ascending-run corroboration: the pairs around this one.
        uint16_t prevId = u16(a - 4), nextId = u16(a + 4);
        if (prevId >= id || nextId <= id) continue;
        if (u16(a - 2) > 999 || u16(a + 6) > 999) continue;
        hits++; found = count;
    }
    return hits == 1 ? found : -1;
}

// An item's name through the resident message tables (category
// 0x2800). Plain read of the dialogue string pool via its decoder.
bool ShopItemName(uint16_t id, char* buf, size_t n)
{
    if (!g_dlgTablesValid) DlgRebuildTables();
    uint32_t want = 0x28000000u | id;
    uint32_t strAddr = 0;
    for (int j = 0; j < g_dlgIdCount; j++)
        if (g_dlgIds[j] == want) { strAddr = g_dlgStrAddr[j]; break; }
    if (!strAddr) return false;
    return DlgDecode(strAddr, buf, n);
}

// The row under the cursor (or at `visual` < 0 for the cursor's row),
// spoken as "<name>, <price> <cur>[, N in stock], i of n" — clauses
// drop out individually when a value is unproven (an unpriced def, an
// unlocatable item count).
bool ShopRowText(int visual, char* buf, size_t n)
{
    if (!ShopUp()) return false;
    int tab = ShopTab();
    int count = ShopTabCount(tab);
    int v = visual >= 0 ? visual : ShopCursor();
    if (count <= 0 || v >= count) {
        snprintf(buf, n, "No items.");
        return true;
    }
    int entry = -1; int seen = -1;
    for (int i = 0; i < g_shopCount; i++) {
        if (!ShopInTab(i, tab)) continue;
        if (++seen == v) { entry = i; break; }
    }
    if (entry < 0) return false;
    uint8_t kind = 1; int price = 0;
    uint16_t id = ShopStockId(entry, &kind, &price);
    char name[128] = "";
    bool named = kind == 1 ? DefName(id, name, sizeof(name))
                           : ShopItemName(id, name, sizeof(name));
    int stock = -1;
    if (kind == 1) {
        uint32_t def = kDefTableAddr + (uint32_t) id * 16;
        stock = u8(def + 1) == 3 ? ShopItemCmdStock(id) : ShopCmdStock(id);
    } else {
        stock = ShopItemStock(id);
    }
    char pricePart[64] = "";
    if (price > 0) snprintf(pricePart, sizeof(pricePart), ", %d %s", price, ShopCurWord());
    char stockPart[48] = "";
    if (stock >= 0) snprintf(stockPart, sizeof(stockPart), ", %d in stock", stock);
    if (named)
        snprintf(buf, n, "%s%s%s, %d of %d.", name, pricePart, stockPart, v + 1, count);
    else if (price > 0)
        snprintf(buf, n, "%d %s%s, %d of %d.", price, ShopCurWord(), stockPart, v + 1, count);
    else
        snprintf(buf, n, "%d of %d.", v + 1, count);
    return true;
}

// The Command Shop's top-level row: "Buy, 1 of 3." and friends.
const char* const kShopTopRows[3] = { "Buy", "Sell", "Exit" };
bool ShopTopRowText(int visual, char* buf, size_t n)
{
    int v = visual >= 0 ? visual : ShopCursor();
    if (v < 0 || v > 2) return false;
    snprintf(buf, n, "%s, %d of 3.", kShopTopRows[v], v + 1);
    return true;
}

// The full spoken line for the current shop state (WhereAmI and the
// watch's announcements share it).
bool ShopLine(char* buf, size_t n)
{
    if (!ShopUp()) return false;
    int phase = ShopPhase();
    uint32_t bal = ShopBalance();
    if (phase == 4) {
        char row[64];
        if (!ShopTopRowText(-1, row, sizeof(row))) return false;
        snprintf(buf, n, "%s. %s You have %u %s.",
                 ShopName(), row, bal, ShopCurWord());
        return true;
    }
    if (phase == 1) {
        char row[256];
        if (!ShopRowText(-1, row, sizeof(row))) return false;
        snprintf(buf, n, "%s. %s You have %u %s.",
                 ShopName(), row, bal, ShopCurWord());
        return true;
    }
    if (phase == 5) {
        char row[256];
        if (!ShopSellRowText(-1, row, sizeof(row))) return false;
        snprintf(buf, n, "%s. %s You have %u %s.",
                 ShopName(), row, bal, ShopCurWord());
        return true;
    }
    if (phase == 2 || phase == 3) {
        // The row being bought is the list cursor's row; the watch
        // froze its name at quantity entry.
        int qty = (int) u16(kShopQtyAddr);
        int price = 0;
        {
            int tab = ShopTab(); int v = ShopCursor(); int seen = -1;
            for (int i = 0; i < g_shopCount; i++) {
                if (!ShopInTab(i, tab)) continue;
                if (++seen == v) { ShopStockId(i, nullptr, &price); break; }
            }
        }
        if (phase == 2)
            snprintf(buf, n, "%s. %s Quantity %d, %d %s. You have %u %s.",
                     ShopName(), g_shopBuyName, qty, qty * price,
                     ShopCurWord(), bal, ShopCurWord());
        else if (ShopMedal())
            snprintf(buf, n, "%s. Trade in these %s? %s.",
                     ShopName(), ShopCurWord(), g_shopConfYes ? "Yes" : "No");
        else
            snprintf(buf, n, "%s. Complete this purchase? %s.",
                     ShopName(), g_shopConfYes ? "Yes" : "No");
        return true;
    }
    if (phase == 6 || phase == 7) {
        int qty = (int) u16(kShopQtyAddr);
        uint16_t id = 0, ability = 0; int level = 0, price = 0;
        if (ShopSellInstance(ShopTab(), ShopCursor(), &id, &level, &ability))
            price = ShopSellPrice(id, level, ability);
        if (phase == 6)
            snprintf(buf, n, "%s. %s Quantity %d, %d %s. You have %u %s.",
                     ShopName(), g_shopSellName, qty, qty * price,
                     ShopCurWord(), bal, ShopCurWord());
        else
            snprintf(buf, n, "%s. Complete this sale? %s.",
                     ShopName(), g_shopConfYes ? "Yes" : "No");
        return true;
    }
    return false;
}

void WatchShop(void)
{
    if (ShopUp()) {
        int phase = ShopPhase();
        int tab = ShopTab();
        uint32_t bal = ShopBalance();
        if (!g_shopUp) {
            g_shopUp = true;
            g_shopTab = tab; g_shopPhase = phase;
            g_shopBalance = bal; g_shopBalancePrimed = true;
            char line[512];
            if (ShopLine(line, sizeof(line))) Say(line, "menu", oga::Priority::High);
        } else {
            // A completed purchase: the currency word dropped.
            if (g_shopBalancePrimed && bal < g_shopBalance &&
                g_shopBuyQty > 0 && g_shopBuyName[0]) {
                char line[256];
                if (g_shopBuyQty > 1)
                    snprintf(line, sizeof(line), "Bought %s, quantity %d. You have %u %s.",
                             g_shopBuyName, g_shopBuyQty, bal, ShopCurWord());
                else
                    snprintf(line, sizeof(line), "Bought %s. You have %u %s.",
                             g_shopBuyName, bal, ShopCurWord());
                Say(line, "menu", oga::Priority::High);
                g_shopBuyQty = 0;
                g_shopJustTransacted = true;
            }
            // A completed sale: the currency word rose.
            if (g_shopBalancePrimed && bal > g_shopBalance &&
                g_shopSellQty > 0 && g_shopSellName[0]) {
                char line[256];
                if (g_shopSellQty > 1)
                    snprintf(line, sizeof(line), "Sold %s, quantity %d. You have %u %s.",
                             g_shopSellName, g_shopSellQty, bal, ShopCurWord());
                else
                    snprintf(line, sizeof(line), "Sold %s. You have %u %s.",
                             g_shopSellName, bal, ShopCurWord());
                Say(line, "menu", oga::Priority::High);
                g_shopSellQty = 0;
                g_sellCountsValid = false;   // the pool just changed
                g_shopJustTransacted = true;
            }
            g_shopBalance = bal;
            // Phase changes must be stable for 4 frames before they
            // are announced: during the quantity screen's opening
            // the sub word sits on the confirm value (40) for exactly
            // 2 frames before settling (frame-traced in bench
            // shop19), and a shorter latch spoke the confirm line
            // before the quantity line (live-caught in bench shop13).
            // The purchase check above stays frame-immediate; only
            // the announcements latch.
            if (phase != g_shopPhase) {
                if (phase != g_shopPhaseCand) { g_shopPhaseCand = phase; g_shopPhaseCandFrames = 1; }
                else if (++g_shopPhaseCandFrames < 4) { /* hold */ }
                else g_shopPhaseCandFrames = 0;  // stable: commit below
            }
            if (phase != g_shopPhase && g_shopPhaseCandFrames == 0) {
                if (phase == 2 || phase == 6) {
                    // Freeze the name of the row being traded (the
                    // list cursor's row, up to its first comma).
                    char row[256];
                    bool ok = phase == 2 ? ShopRowText(-1, row, sizeof(row))
                                         : ShopSellRowText(-1, row, sizeof(row));
                    if (ok) {
                        char* dst = phase == 2 ? g_shopBuyName : g_shopSellName;
                        snprintf(dst, sizeof(g_shopBuyName), "%s", row);
                        char* c = strstr(dst, ", ");
                        if (c) *c = 0;
                        if (phase == 2) g_shopBuyQty = (int) u16(kShopQtyAddr);
                        else g_shopSellQty = (int) u16(kShopQtyAddr);
                        char line[384];
                        snprintf(line, sizeof(line), "How many? %s", row);
                        Say(line, "menu", oga::Priority::High);
                    }
                } else if (phase == 3 || phase == 7) {
                    g_shopConfYes = false;      // the game defaults to No
                    if (phase == 3) g_shopBuyQty = (int) u16(kShopQtyAddr);
                    else g_shopSellQty = (int) u16(kShopQtyAddr);
                    char line[256];
                    if (phase == 7)
                        snprintf(line, sizeof(line), "Complete this sale? No.");
                    else if (ShopMedal())
                        snprintf(line, sizeof(line), "Trade in these %s? No.",
                                 ShopCurWord());
                    else
                        snprintf(line, sizeof(line), "Complete this purchase? No.");
                    Say(line, "menu", oga::Priority::High);
                } else if (phase == 1 || phase == 5 || phase == 4) {
                    // Landing in a list (or back at the top level):
                    // speak the row the cursor sits on — unless a
                    // Bought/Sold line just announced this landing.
                    if (!g_shopJustTransacted) {
                        char row[256];
                        bool ok = phase == 1 ? ShopRowText(-1, row, sizeof(row))
                                : phase == 5 ? ShopSellRowText(-1, row, sizeof(row))
                                             : ShopTopRowText(-1, row, sizeof(row));
                        if (ok) Say(row, "menu", oga::Priority::High);
                    }
                    g_shopJustTransacted = false;
                    g_shopTab = tab;
                }
                g_shopPhase = phase;
            } else if ((phase == 1 || phase == 5) && tab != g_shopTab) {
                g_shopTab = tab;
                char row[256];
                bool ok = phase == 1 ? ShopRowText(-1, row, sizeof(row))
                                     : ShopSellRowText(-1, row, sizeof(row));
                if (ok) {
                    char line[384];
                    snprintf(line, sizeof(line), "%s. %s", kShopTabNames[tab], row);
                    Say(line, "menu", oga::Priority::High);
                }
            }
            if (phase == 2) g_shopBuyQty = (int) u16(kShopQtyAddr);
            if (phase == 6) g_shopSellQty = (int) u16(kShopQtyAddr);
        }
        g_shopDownFrames = 0;
        return;
    }
    if (g_shopUp) {
        g_shopUp = false; g_shopPhase = 0; g_shopTab = -1;
        g_shopPhaseCand = 0; g_shopPhaseCandFrames = 0;
        g_shopBuyQty = 0; g_shopSellQty = 0; g_shopSellName[0] = 0;
        g_shopJustTransacted = false;
        g_shopListKind = -1; g_sellCountsValid = false;
        g_shopBalancePrimed = false;
    }
    if (g_shopDownFrames < (1 << 30)) g_shopDownFrames++;
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
    char line[256];
    if (SaveLoadUp()) {
        // The savedata dialog owns the screen while it is up —
        // ahead of camp, whose witnesses are also set under it
        // when it opens from the save point (see the section
        // header's camp trap).
        SaveLoadLine(line, sizeof(line));
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // The Command Board module replaces the world; its screens
    // own the query while any of its gates verify.
    if (CbWhereAmI(line, sizeof(line))) {
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
    if (ModeMenuUp()) {
        char row[96]; ModeMenuRow(row, sizeof(row));
        snprintf(line, sizeof(line), "Mirage Arena. New Event. %s.", row);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (SetupUp()) {
        char fl[192]; SetupFocusLine(fl, sizeof(fl));
        snprintf(line, sizeof(line), "Mirage Arena. Arena Mode setup. %s", fl);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    if (RumbleUp()) {
        char fl[128]; RumbleFocusLine(fl, sizeof(fl));
        snprintf(line, sizeof(line), "Mirage Arena. Rumble Racing setup. %s", fl);
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // A shop likewise owns the screen over the field it opened from.
    if (ShopUp()) {
        char shopLine[512];
        if (ShopLine(shopLine, sizeof(shopLine))) {
            Say(shopLine, "menu", oga::Priority::High);
            return;
        }
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
            char full[448];
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
    if (CbMenuNav(row, sizeof(row))) { Say(row, "menu", oga::Priority::High); return; }
    CampTrack();
    if (CampActive() || ReportsUp()) { CampRowFor(g_campScreen, row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    // The game-over and movie-pause menus own the D-pad while
    // their gates verify (same placement rule as WhereAmI: both
    // precede the field branch).
    if (GameOverUp())  { GameOverRow(row, sizeof(row));  Say(row, "menu", oga::Priority::High); return; }
    if (MoviePauseUp()) { MoviePauseRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (EventMenuUp()) { EventMenuRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (ModeMenuUp()) { ModeMenuRow(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (SetupUp()) { SetupFocusLine(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (RumbleUp()) { RumbleFocusLine(row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
    if (ShopUp()) {
        // List: the landed row. Quantity: the landed quantity line.
        // Confirmation: the tracked choice (its cursor is command-
        // tracked like the meld dialog's; left/right flip it in the
        // command hook below).
        int phase = ShopPhase();
        if (phase == 3 || phase == 7) { Say(g_shopConfYes ? "Yes." : "No.", "menu", oga::Priority::High); return; }
        char sline[512];
        if (ShopLine(sline, sizeof(sline))) {
            // In a list step ShopLine carries the shop name and
            // balance; the nav echo is the bare row.
            char srow[256];
            bool bare = phase == 1 ? ShopRowText(-1, srow, sizeof(srow))
                      : phase == 5 ? ShopSellRowText(-1, srow, sizeof(srow))
                      : phase == 4 ? ShopTopRowText(-1, srow, sizeof(srow))
                                   : false;
            if (bare) Say(srow, "menu", oga::Priority::High);
            else Say(sline, "menu", oga::Priority::High);
        }
        return;
    }
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
        ArchReportsUp() || GameOverUp() || MoviePauseUp() || EventMenuUp() || ModeMenuUp() || SetupUp() || RumbleUp() || ShopUp() ||
        OnWorldMap() || OnTitleMenu() || OnDifficulty() ||
        CbGameUp() || CbTitleUp(nullptr) || CbSetupUp(nullptr, nullptr, nullptr))
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
    g_gaugesPrimed = false; g_gaugesFlags = 0; g_aimFired = false;  // battle gauges watch
    g_ownTrack = HealthTrack{}; g_foeTrack = HealthTrack{};  // sound-first health cues
    g_pendFoeBeep = g_pendOwnBeep = g_pendFocusCue = g_pendDLinkCue = false;
    g_lastActorBase = 0; g_goStruct = 0; g_lastGoScanMs = 0; g_goWasUp = false; g_goDownFrames = 1 << 30; g_mpUp = false;  // game-over / movie-pause gates
    g_evUp = false; g_evDownFrames = 1 << 30; g_evWidget = 0; g_modeSpoken = false; g_modeArmed = true; g_modeRow = -1; g_modeStable = 0; g_modeDownFrames = 1 << 30;  // Mirage Arena event menus gate
    g_setupWidget = 0; g_setupSpoken = false; g_setupArmed = true; g_setupPairA = -1; g_setupPairB = -1; g_setupStable = 0; g_setupDownFrames = 1 << 30; g_setupBattleLatch = -1; g_arenaInfoCat = 0;  // arena setup + event state
    g_rumbleWidget = 0; g_rumbleSpoken = false; g_rumbleArmed = true; g_rumblePairA = -1; g_rumblePairB = -1; g_rumbleStable = 0; g_rumbleDownFrames = 1 << 30; g_eventKindLatch = 0; g_raceCourseLatch = -1; g_evEndArmed = false; g_evEndArmFrames = 0;  // rumble setup + event end
    g_shopTable = 0; g_shopCount = 0; g_shopFmt = 0; g_shopListKind = -1; g_sellCountsValid = false; g_shopSellName[0] = 0; g_shopSellQty = 0; g_shopWidget = 0; g_shopUp = false; g_shopDownFrames = 1 << 29; g_shopTab = -1; g_shopPhase = 0; g_shopPhaseCand = 0; g_shopPhaseCandFrames = 0; g_shopConfYes = false; g_shopBuyQty = 0; g_shopBalancePrimed = false; g_blockBase = 0;  // shops
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

// The battle-gauges watch: Focus / shotlock and D-Link events
// (signals in the constants block; mechanics proven in bench
// khbbs-foc1..foc7). Runs every frame the field actor is live,
// beside WatchLowHp, and follows its discipline: the latches
// track in every context, but a line speaks only with the field
// in front — never under the camp menu or Reports. Every trigger
// is an edge of a flag bit the GAME sets and clears, so nothing
// repeats without the game itself changing state first: Focus
// refilling to full, a fresh aim, a new link. The first live
// sight primes silently — loading a save with full gauges is
// reported by the field line, not as a false event.
//
// SOUND-FIRST SPLIT (2026-10-24, the owner's rule — the bench
// audio audit behind it is in the RE doc): the events divide by
// what the game itself sounds and by what speech alone can say.
// A FIRED shotlock is fully sounded by the game (the fire
// transition measured at full scale), so it raises nothing at
// all now. GAUGE-FULL moments (Focus full, D-Link ready) have no
// game cue of their own, but they are simple "ready" facts — in
// the default SOUND mode they raise earcon pulses through the
// cue snapshot instead of speaking; the KhbbsHudGaugeCues seam
// (top of file) restores the spoken lines for players who prefer
// speech. The lines that STAY spoken are the ones carrying what
// no sound can: the aim starting ("Shotlock." — the aim's entry
// measured BELOW the music's own peaks, and the line is the only
// proof a hold engaged rather than re-locking), a cancel
// ("Shotlock cancelled." — the only sound at a cancel is the
// generic aim-exit whoosh every aim exit shares; it cannot say
// the Focus was refunded), and the D-Link state changes
// ("D-Link: <name>." — the game sounds a jingle, but a jingle
// cannot name the partner; "D-Link ended." — measured at the
// music's level, effectively uncued).
void WatchBattleGauges(void)
{
    uint32_t obj = GaugesObj();
    if (!obj) return;
    uint32_t flags = u32(obj + kGaugesOffFlags);
    uint32_t state = u32(obj + kGaugesOffState);
    bool speak = !CampActive() && !ReportsUp();
    if (!g_gaugesPrimed) {
        g_gaugesPrimed = true;
        g_gaugesFlags = flags;
        g_aimFired = (state == 2);
        return;
    }
    uint32_t prev = g_gaugesFlags;
    // A fired shotlock passes through the firing state — catch
    // it for the frame the aim flag falls (the two change
    // together when Cross is pressed).
    if (state == 2) g_aimFired = true;
    if (!(prev & kGaugesFlagFocusFull) && (flags & kGaugesFlagFocusFull)) {
        if (speak) {
            if (g_hudGaugeCuesSound) g_pendFocusCue = true;
            else Say("Focus full.", "focus", oga::Priority::Normal);
        }
    }
    if (!(prev & kGaugesFlagDLinkPrimed) && (flags & kGaugesFlagDLinkPrimed)) {
        if (speak) {
            if (g_hudGaugeCuesSound) g_pendDLinkCue = true;
            else Say("D-Link ready.", "dlink", oga::Priority::Normal);
        }
    }
    if (!(prev & kGaugesFlagLinked) && (flags & kGaugesFlagLinked)) {
        if (speak) {
            char name[40];
            uint16_t partner = u16(obj + kGaugesOffPartner);
            char line[96];
            if (partner && DefName(partner, name, sizeof(name)))
                snprintf(line, sizeof(line), "D-Link: %s.", name);
            else
                snprintf(line, sizeof(line), "D-Link active.");
            Say(line, "dlink", oga::Priority::Normal);
        }
    }
    if ((prev & kGaugesFlagLinked) && !(flags & kGaugesFlagLinked)) {
        if (speak) Say("D-Link ended.", "dlink", oga::Priority::Normal);
    }
    if (!(prev & kGaugesFlagAiming) && (flags & kGaugesFlagAiming)) {
        g_aimFired = (state == 2);
        if (speak) Say("Shotlock.", "shotlock", oga::Priority::Normal);
    } else if ((prev & kGaugesFlagAiming) && !(flags & kGaugesFlagAiming)) {
        // A fired shotlock says nothing: the game's own fire sound
        // (measured full-scale on the bench) already announced it.
        // A cancel keeps its line — see the header: the refund is
        // the fact, and no game sound carries it.
        if (speak && !g_aimFired)
            Say("Shotlock cancelled.", "shotlock", oga::Priority::Normal);
        g_aimFired = false;
    }
    g_gaugesFlags = flags;
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
// there (a potion used in camp re-arms silently), but the spoken
// line only fires with the field in front — a warning first
// noticed under a menu would be menu noise. HP does not drop
// during this game's cutscenes, and a player who enters one
// already low was warned during play, so the field gate is the
// cutscene gate too. The line is High priority: the latch and the
// re-arm gap already make chatter impossible, and High keeps a
// re-armed warning from being swallowed by the consecutive-line
// dedup that status echoes go through.

// ---- Sound-first battle health cues (2026-10-24, the owner's rule) ----------
// Never speak what the game already cues with a sound. The bench audio
// audit (two captured fights, ~/khbbs-scan/snd1 + snd2) measured what
// the game sounds: EVERY hit lands with a full-scale impact (median
// bucket peak 0.99 of 1.0 for foe hits, 1.00 for hits on the player)
// — but an impact says nothing about HOW MUCH health is left. So a
// hit now raises a one-shot beep through the cue snapshot whose PITCH
// carries the new health fraction, and speech is kept only for the
// milestone crossings, which the game does not mark at all:
//
//   foe health: beep on every hit that drops the tracked foe; spoken
//     percent only at 75 / 50 / 25, and "Foe nearly gone." at 10.
//   own health: the mirror — beeps on hits, "Your health N percent."
//     at the same crossings, "Your health nearly gone." at 10, and
//     the older "Low health." warning still fires at the game's own
//     <26% boundary (WatchLowHp, just below).
//   Exact numbers stay where they always were: the foe query.
//
// PITCH IS LOGARITHMIC (perceived pitch is roughly log frequency, so
// equal health steps sound like equal pitch steps), foe 1200→300 Hz
// (bright tick), own 800→200 Hz (duller thud register), panned to the
// owner's sides: FOE RIGHT (+0.8), PLAYER LEFT (−0.8). The registers
// differ as well as the pans, so the sides stay separable in mono.
// The design follows the precedents the research found: Mortal
// Kombat 1's official accessibility "Health Meter" cue sounds at
// exactly 75/50/25/critical (the same milestone set), Street Fighter
// 6's designed hit/health indicator sounds, and the Dissidia
// proposal's one-family-per-kind discipline. Durations and waveforms
// are the synth's side of the contract (adapter.h, cue-synth.md):
// foe ~35 ms bright tick, own ~55 ms thud, both at the health
// family's own level under the app's battle-cues switch.
//
// THE TRACKED FOE is the lock-on target while locked. While
// unlocked it is the last locked foe, followed through its entity
// block for as long as the block still validates (same max HP, sane
// current HP, live scene word) — foe HP only ever drops because the
// player hit it, so "the foe the player just hit" and "the last
// locked foe" are the same foe in this game's fights. A foe that
// was never locked is NOT followed: finding it would take a
// per-frame actor census, and the one census the adapter has (the
// lock-record sweep) costs ~2M reads and is throttled to once a
// second for exactly that reason. Documented limit, RE doc.
//
// Milestones latch per subject (actor / foe block + max HP): a
// plunge through several crossings speaks only the lowest; healing
// back to a milestone + 5 points re-arms it (the low-HP watch's
// hysteresis shape). A drop to 0 raises NOTHING — a death has the
// game's own sound, the lock loss, and the game-over flow; a beep
// and a "0 percent" would be noise on top. Events stash only with
// the field in front (the same discipline as speech: a hit cannot
// land under the camp menu anyway, and a stale beep after a menu
// closes would be a lie about when it happened).
// (The HealthTrack struct, the pending-event fields and the pitch
// constants are declared with the battle-gauges state, above.)

// Pitch from a health fraction, log-mapped between the family's
// endpoints: fraction 1 = hi, 0 = lo, and the midpoint fraction
// lands on the geometric mean (600 Hz foe / 400 Hz own).
float HealthPitch(float lo, float hi, unsigned cur, unsigned max)
{
    float frac = max ? (float) cur / (float) max : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return lo * powf(hi / lo, frac);
}

// The foe entity block behind the lock record, with its HP — the
// same record/target validation ReadFoe does, minus the distance,
// and minus the record sweep: locating is the cue snapshot's job
// (it owns the throttle); this watch only rides the cached record.
// (ReadFoe keeps its own copy: it is on proven query paths and this
// watch must not be able to disturb them.)
bool ReadFoeBlock(uint32_t* blkOut, unsigned* curOut, unsigned* mxOut)
{
    if (!g_lockRec) return false;
    if (u32(g_lockRec + kLockRecOffActor) != g_actorBase) return false;
    uint32_t tgt = u32(g_lockRec + kLockRecOffTgt);
    if (!tgt) return false;
    if (u32(tgt) != u32(g_actorBase + kActorOffEngage)) return false;
    uint32_t blk = tgt - kFoeBlockBack;
    unsigned cur = (unsigned) u16(blk + kActorOffCurHp);
    unsigned mx = (unsigned) u16(blk + kActorOffMaxHp);
    if (mx < 10 || mx > 20000 || cur > mx) return false;
    *blkOut = blk; *curOut = cur; *mxOut = mx;
    return true;
}

// One frame of milestone/beep tracking for one side. foeSide picks
// the family (pitch range, pan, wording, speech priority).
void TrackHealthSide(HealthTrack& t, uint32_t who, unsigned cur, unsigned max,
                     bool foeSide)
{
    if (!max || !who) return;
    if (t.who != who || t.max != (uint16_t) max) {
        // A new subject (or a new max under the same block): prime
        // silently — milestones at or below the current percent are
        // already passed and must not speak for a fight joined late.
        t.who = who; t.max = (uint16_t) max; t.prev = (int) cur;
        int pct = (int) ((cur * 100) / max);
        t.m75 = pct <= 75; t.m50 = pct <= 50;
        t.m25 = pct <= 25; t.m10 = pct <= 10;
        return;
    }
    int pct = (int) ((cur * 100) / max);
    if ((int) cur < t.prev) {
        if (cur > 0) {   // a hit landed (death is the game's own moment)
            bool front = !CampActive() && !ReportsUp();
            if (front) {
                if (foeSide) {
                    g_pendFoeBeep = true;
                    g_pendFoeHz = HealthPitch(kFoeBeepLoHz, kFoeBeepHiHz, cur, max);
                } else {
                    g_pendOwnBeep = true;
                    g_pendOwnHz = HealthPitch(kOwnBeepLoHz, kOwnBeepHiHz, cur, max);
                }
            }
            int crossed = 0;   // latch every crossing; speak only the lowest
            if (pct <= 75) { if (!t.m75) crossed = 75; t.m75 = true; }
            if (pct <= 50) { if (!t.m50) crossed = 50; t.m50 = true; }
            if (pct <= 25) { if (!t.m25) crossed = 25; t.m25 = true; }
            if (pct <= 10) { if (!t.m10) crossed = 10; t.m10 = true; }
            if (crossed && front) {
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
    } else if ((int) cur > t.prev) {
        // Healing re-arms each milestone once the percent is back
        // above it by the margin (milestone + 5).
        if (pct >= 75 + kHealthMilestoneRearm) t.m75 = false;
        if (pct >= 50 + kHealthMilestoneRearm) t.m50 = false;
        if (pct >= 25 + kHealthMilestoneRearm) t.m25 = false;
        if (pct >= 10 + kHealthMilestoneRearm) t.m10 = false;
    }
    t.prev = (int) cur;
}

void WatchHealthCues(void)
{
    // Own side: the located actor's HP words (the same words
    // WatchLowHp reads). No plausible actor — the field was left,
    // or the player died and the game-over flow owns the screen —
    // forget the track; a return primes fresh.
    if (g_actorBase) {
        unsigned cur = (unsigned) u16(g_actorBase + kActorOffCurHp);
        unsigned mx = (unsigned) u16(g_actorBase + kActorOffMaxHp);
        if (mx >= 10 && mx <= 20000 && cur <= mx && cur > 0)
            TrackHealthSide(g_ownTrack, g_actorBase, cur, mx, false);
        else if (cur == 0)
            g_ownTrack = HealthTrack{};
    } else {
        g_ownTrack = HealthTrack{};
    }

    // Foe side: the lock record's foe while it validates…
    uint32_t blk; unsigned fc, fm;
    if (ReadFoeBlock(&blk, &fc, &fm)) {
        TrackHealthSide(g_foeTrack, blk, fc, fm, true);
        if (fc == 0) g_foeTrack = HealthTrack{};   // it died under the lock
    } else if (g_foeTrack.who) {
        // …else the last locked foe, while its block still validates:
        // same max HP, sane current HP, and a live scene word at the
        // scene object the block belongs to.
        uint32_t b = g_foeTrack.who;
        unsigned mx = (unsigned) u16(b + kActorOffMaxHp);
        unsigned cur = (unsigned) u16(b + kActorOffCurHp);
        if (mx == (unsigned) g_foeTrack.max && mx && cur <= mx && cur > 0 &&
            b != g_actorBase && u32(b + kFoeBlockBack) != 0) {
            TrackHealthSide(g_foeTrack, b, cur, mx, true);
        } else if (mx == (unsigned) g_foeTrack.max && cur == 0) {
            g_foeTrack = HealthTrack{};   // the remembered foe died
        }
    }
}

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
    // The New Event mode list's arrival watch: the event
    // menu's child, on the same widget family.
    WatchModeMenu();
    // The Arena Mode setup screen's arrival watch, and the
    // event-state watch for the pad/Ready tail of the flow
    // (its own gates scope it to the Hub field).
    WatchSetupMenu();
    WatchRumbleMenu();
    WatchArenaEvent();
    WatchEventEnd();
    // The shops' watch (arrival, tab landings, buy-flow steps and
    // the purchase line). Same rising-edge pattern.
    WatchShop();
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
    // The Command Board's arrival watches (title, setup,
    // dialogs, turns, landings, sub-screens). The board module
    // replaces the world, so this too precedes the field gate.
    WatchBoard();
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
    WatchBattleGauges();
    WatchHealthCues();
    // The mark also reads 4 during a shotlock aim — the lock is
    // held throughout (the reticle tracks the target), so 4
    // counts as locked here: without it every shotlock would
    // read as a lost lock and re-announce "Locked on." at the
    // aim's end. The foe / lock QUERIES still key on 2 exactly,
    // as proven; they are not issued mid-aim in practice.
    int lock = (u16(g_actorBase + kActorOffMark) == 2 ||
                u16(g_actorBase + kActorOffMark) == 4) ? 1 : 0;
    if (g_lastLock == 0 && lock == 1) {
        // Name what was locked onto when its type is proven —
        // the name alone, no chatter (the lock tick itself
        // measured at music level in the sound-first audit, so
        // this line is a query-grade answer, not a cue).
        char name[40];
        if (ReadFoeName(name, sizeof(name))) {
            char line[64];
            snprintf(line, sizeof(line), "Locked on: %s.", name);
            Say(line, "lock", oga::Priority::Normal);
        } else {
            Say("Locked on.", "lock", oga::Priority::Normal);
        }
    }
    g_lastLock = lock;
}

static void khbbs_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();  break;
        case Command::NextAlly:        if (InField() && !CampActive() && !ReportsUp()) CmdFoe();       else CmdNotMapped(); break;
        case Command::NextEnemy:       if (InField() && !CampActive() && !ReportsUp()) CmdLockQuery(); else CmdNotMapped(); break;
        case Command::Enemies:         if (InField() && !CampActive() && !ReportsUp()) CmdRoster();    else CmdNotMapped(); break;
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
            if (ShopUp() && (ShopPhase() == 3 || ShopPhase() == 7)) {
                // The shop confirmation's choice lives in one-hot
                // heap floats that float per lineage; like the meld
                // dialog, it is tracked from the player's presses.
                // It starts on No (proven); a press flips it.
                g_shopConfYes = !g_shopConfYes;
                Say(g_shopConfYes ? "Yes." : "No.", "menu", oga::Priority::High);
                break;
            }
            CmdMenuNav();
            break;
        case Command::Guide:           CmdHubGuide(); CmdRoomGuide(); break;
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
           GameOverUp() || MoviePauseUp() || EventMenuUp() || ModeMenuUp() || SetupUp() || RumbleUp() || OnWorldMap() || SaveLoadUp() ||
           ArchivesUp() || TheaterUp() || ArchReportsUp() || DialogueUp() || ShopUp() ||
           CbGameUp() || CbTitleUp(nullptr) || CbSetupUp(nullptr, nullptr, nullptr);
}

static void khbbs_detach(void)
{
    g_host = nullptr; g_lastDeck = -1;
    g_actorBase = 0; g_lastScanMs = 0; g_charId = -1; g_lastLock = -1; g_lockRec = 0;
    g_lowHpWarned = false; g_lowHpActor = 0; g_lowHpMax = 0;
    g_gaugesPrimed = false; g_gaugesFlags = 0; g_aimFired = false;  // battle gauges watch
    g_ownTrack = HealthTrack{}; g_foeTrack = HealthTrack{};  // sound-first health cues
    g_pendFoeBeep = g_pendOwnBeep = g_pendFocusCue = g_pendDLinkCue = false;
    g_lastActorBase = 0; g_goStruct = 0; g_lastGoScanMs = 0; g_goWasUp = false; g_goDownFrames = 1 << 30; g_mpUp = false;  // game-over / movie-pause gates
    g_evUp = false; g_evDownFrames = 1 << 30; g_evWidget = 0; g_modeSpoken = false; g_modeArmed = true; g_modeRow = -1; g_modeStable = 0; g_modeDownFrames = 1 << 30;  // Mirage Arena event menus gate
    g_setupWidget = 0; g_setupSpoken = false; g_setupArmed = true; g_setupPairA = -1; g_setupPairB = -1; g_setupStable = 0; g_setupDownFrames = 1 << 30; g_setupBattleLatch = -1; g_arenaInfoCat = 0;  // arena setup + event state
    g_rumbleWidget = 0; g_rumbleSpoken = false; g_rumbleArmed = true; g_rumblePairA = -1; g_rumblePairB = -1; g_rumbleStable = 0; g_rumbleDownFrames = 1 << 30; g_eventKindLatch = 0; g_raceCourseLatch = -1; g_evEndArmed = false; g_evEndArmFrames = 0;  // rumble setup + event end
    g_shopTable = 0; g_shopCount = 0; g_shopFmt = 0; g_shopListKind = -1; g_sellCountsValid = false; g_shopSellName[0] = 0; g_shopSellQty = 0; g_shopWidget = 0; g_shopUp = false; g_shopDownFrames = 1 << 29; g_shopTab = -1; g_shopPhase = 0; g_shopPhaseCand = 0; g_shopPhaseCandFrames = 0; g_shopConfYes = false; g_shopBuyQty = 0; g_shopBalancePrimed = false; g_blockBase = 0;  // shops
    g_visitObj = 0; g_lastVisitScanMs = 0; g_wmUp = false; g_wmSel = -1; g_wmSelCand = -1; g_wmSelFrames = 0; g_wmDlgUp = false; g_wmDlgDownFrames = 1 << 30; g_wmPauseUp = false;  // world map
    g_archTopUp = false; g_archTopStable = 0; g_theaterUp = false; g_theaterStable = 0; g_reportsUp = false; g_reportsStable = 0;  // trinity archives
    SlReset();  // save/load dialog
    DlgReset();  // field dialogue
    g_lastLockScanMs = 0;
    g_nodeBase = 0; g_nodeActor = 0; g_lastNodeScanMs = 0;
    g_foeCount = -1; g_foeCensusActor = 0; g_foeCensusMs = 0;  // foe census cache
    FaceReset(); g_faceNode = 0;
    CbReset(); g_cbFrame = 0; g_cbLastRecScan = -100;  // command board
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
    //
    // The sound-first one-shot events (foe/own hit beeps, gauge-full
    // earcons — see adapter.h) ride this same snapshot: the watches
    // stash them during on_frame, and the first poll that passes the
    // field gates copies them out and clears them, so each event
    // sounds exactly once. A poll refused by the gates (camp open,
    // field left) does NOT consume — but hits cannot land in those
    // states anyway, so nothing real can be stranded by the wait.
    if (!out) return false;
    out->battle = false;
    out->locked = false;
    out->is_core = false;
    out->dist = 0.0f;
    out->core_gain = false;
    out->foe_beep = false; out->foe_beep_hz = 0.0f; out->foe_beep_pan = 0.0f;
    out->own_beep = false; out->own_beep_hz = 0.0f; out->own_beep_pan = 0.0f;
    out->focus_cue = false; out->dlink_cue = false;
    if (!g_host || !InField() || CampActive() || ReportsUp()) return false;
    out->battle = true;
    out->foe_beep = g_pendFoeBeep;
    out->foe_beep_hz = g_pendFoeHz;
    out->foe_beep_pan = g_pendFoeBeep ? kFoeBeepPan : 0.0f;
    out->own_beep = g_pendOwnBeep;
    out->own_beep_hz = g_pendOwnHz;
    out->own_beep_pan = g_pendOwnBeep ? kOwnBeepPan : 0.0f;
    out->focus_cue = g_pendFocusCue;
    out->dlink_cue = g_pendDLinkCue;
    g_pendFoeBeep = g_pendOwnBeep = g_pendFocusCue = g_pendDLinkCue = false;
    g_pendFoeHz = g_pendOwnHz = 0.0f;
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
    "Title and difficulty menu speech; Trinity Archives speech; camp menu speech; field health, command and gauge speech; battle health cues (hit beeps and milestone callouts); enemy names in the lock-on readouts; enemy count and roster queries; low-health warning; game-over menu speech; movie pause menu speech; world map speech; Mirage Arena event menu speech; Command Board speech (entry flow, turn menus, landings, turn summaries); save and load screen speech; lock-on beacon cue; landmark guide for the Mirage Arena Hub and the surveyed Olympus Coliseum and Radiant Garden rooms",
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
    "Lock-on: acquiring a lock (L and R together) is announced "
    "with the foe's name when its type is known (\"Locked on: "
    "Bruiser.\"). While locked on, Where Am I also reports the "
    "target's name, health and how far away it is, and two "
    "queries answer on demand: Next Ally speaks the foe (its "
    "name, health, distance, and whether it is above or below "
    "you), and Next Enemy speaks the lock state and the name. "
    "Names are the game's own enemy names, proven type by type; "
    "a foe whose type is not proven yet — the bosses among "
    "them — is still read by health and distance, with no name.\n"
    "Enemy awareness: the Enemies control answers with how many "
    "foes are in the fight and a roster of them, nearest first — "
    "each foe's name when proven, how far away it is, and, once "
    "you have moved and the reader knows which way you face, a "
    "clock direction; the foe you are locked onto is marked "
    "\"locked on\". Where Am I and the foe query carry the count "
    "too. A foe whose position cannot be verified is counted and "
    "named, but no distance is claimed for it. Nothing about the "
    "other foes is announced unasked: in a fight, speech stays "
    "with the crossings and the lock, and the roster is there "
    "when you ask for it.\n"
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
    "the other floor's landmarks are never named. The Guide "
    "works the same way in the surveyed rooms of Olympus "
    "Coliseum — the Vestibule, the Coliseum Gates and the Town "
    "Near Thebes — and Radiant Garden — the Entryway, Central "
    "Square, Castle Town, Merlin's House, the Fountain Court "
    "balcony and the Outer Gardens: it names each room's save "
    "point, shops, chests and people, and its exits by the room "
    "they lead to. Rooms that have not been surveyed yet stay "
    "silent. The Log In "
    "terminal's own menu is spoken too — \"Mirage Arena,\" "
    "then New Event, Active Events, or D-Links. Choosing New "
    "Event opens the list of event types (Versus Mode, Arena "
    "Mode, Command Board, Rumble Racing), and the list is "
    "spoken. Choosing Arena Mode opens the event setup: the "
    "battle you are on is spoken with its risk (of five) and "
    "how many rounds it runs — all thirteen battles are read "
    "from the game's own tables — along with the players row "
    "and OK. Confirming with OK sets the event: the reader "
    "says so, names the battle, and tells you to head to the "
    "middle of the hub; standing on the pad and pressing "
    "Cross raises a spoken \"Ready. Press START to begin.\" "
    "When an arena battle ends, the reader says \"Event "
    "over.\" while the game shows its results summary, and "
    "then you are back in the hub. Choosing Rumble Racing "
    "opens its own setup, and that is spoken too: the "
    "course (Country Chase, Disney Drive, Grand Spree, or "
    "Castle Circuit), the number of laps, and the number "
    "of racers — and setting the event names the race, "
    "just like a battle. The race itself is real-time "
    "driving and is not narrated. Versus Mode needs other "
    "players and does nothing on your own. The Command "
    "Board itself is spoken now, from start to finish. "
    "Its title menu (New Game, Continue, How to Play), "
    "the board choice — the reader names each board, its "
    "GP goal and its printed description — the goal "
    "choice, and the \"Play on this board?\" question all "
    "speak, as do the playing order and the \"Shuffle the "
    "playing order?\" question. In the game, your turn "
    "menu (Roll, Hand, Stats, Options) speaks, the Hand "
    "screen names its five special plays (Stun, Two Dice, "
    "Navigator, Three Dice, Double Toll), and the Options "
    "rows speak by name. Pressing Cross rolls and walks "
    "for you: you cannot steer at junctions by ear yet, "
    "but the reader names the panel you land on, and when "
    "it is a command panel it asks whether to place a "
    "card and speaks the card picker as \"Card N of 5\" — "
    "the cards' own names are not spoken there. Asking "
    "where you are names your panel and every player's GP "
    "in turn order. The other players' turns are kept to "
    "outcomes: whose turn it is, and at the end of it what "
    "they spent, gained, or paid to another player. Not "
    "spoken on the board: dice values, panel prices and "
    "the GP totals on the Stats screen, and the two speed "
    "values on the Options screen.\n"
    "Battle cue: with the app's battle cues turned on in "
    "Settings, locking on also starts the beacon, a low beep "
    "that pulses faster as your target gets closer and stops "
    "when the lock ends. No lock, no beep.\n"
    "Battle health cues: hits in a fight raise short beeps of "
    "their own — a hit on the foe beeps on the right, a hit on "
    "you on the left, and the pitch falls as that health "
    "falls, so you can hear roughly how much is left without "
    "stopping the fight. Speech keeps only the big crossings: "
    "the foe's health and yours at three-quarters, half, and "
    "a quarter, and \"nearly gone\" near the end; exact "
    "numbers stay on the foe query. When the Focus or D-Link "
    "gauge fills, a bright ready chime sounds instead of a "
    "spoken line. Linking to a partner is still announced by "
    "name, and a link ending is still spoken. A fired shotlock "
    "is not announced at all — the game's own shot sound "
    "already says it. These cues ride the same battle-cues "
    "setting as the beacon.\n"
    "Not mapped yet: the other camp sub-screens (Items, Abilities, "
    "and the rest), and battle state beyond the field readouts "
    "above. The tutorial is still being mapped.\n"
    "Supported version: Kingdom Hearts: Birth by Sleep, USA retail "
    "v1.00 (ULUS10505). Other pressings are not verified.",
};

} // namespace oga
