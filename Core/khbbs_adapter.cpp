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
    // the title's (3), the title step has reached idle (4) and the
    // fade word has settled (0) does the real menu take input —
    // bench-verified across six cold boots (header note).
    int count = u16(kTitleCountAddr);
    return u32(kSubmenuPtrAddr) == 0 &&
           (count == 3 || count == 4) &&
           u16(kTitleCursorAddr) < count &&
           u32(kTitlePhaseAddr) == 3 &&
           u8(kTitleStepAddr) == 4 &&
           u32(kTitleFadeAddr) == 0;
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
// object, slot 6 the decks screen object, each zeroed while its
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

void CampRow(char* buf, size_t n)
{
    uint32_t obj = CampMainObj();
    int c = obj ? (int) u16(obj + 0x66) : 0;
    snprintf(buf, n, "%s, %d of %d", kCampItems[c], c + 1, kCampCount);
}

// Which camp screen is on top, tracked across commands (see the map
// comment: no page word exists; the count + the one cursor that moved
// name the screen). Reset whenever the camp menu is not active.
enum class CampScreen { None, Main, DecksSub, Editor, SlotList, Inventory };
CampScreen g_campScreen = CampScreen::None;
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
    if (g_campScreen == CampScreen::Main || g_campScreen == CampScreen::None) {
        g_campScreen = CampScreen::DecksSub;  // a dive happened
        g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
        g_lastSlotRaw = -1;
    }
    uint32_t decksObj = CampDecksObj();
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

// Describe inventory row `row` (0-based over the command rows, the
// cursor word's own counting): "Meteor Crash, level 5, with Combo
// Plus, 2 of 150". Items (sub-type 3) carry a count, not a level:
// "Potion, 60 in stock, ...". False when the pool is unavailable or
// the row is out of range — the caller falls back to the row number.
bool InvRowText(int row, char* buf, size_t n)
{
    if (row < 0) return false;
    uint32_t pool = u32(kWorkPtrAddr);
    if (pool < kUserRamBase || pool + kPoolScan * 10 > kUserRamEnd)
        return false;
    // The list on screen follows the filter tab (live13): All shows
    // every deck command; the other tabs show one sub-type each
    // (Friendship = the group and special commands). An unknown tab
    // value fails closed to the row-number fallback.
    uint32_t slotW = CampSlotWidget();
    int tab = slotW ? (int) u16(slotW + 0x134) : -1;
    if (tab < 0 || tab > 4) return false;
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
    if (row >= cnt) return false;
    const InvEntry& r = g_invEntries[row];
    char name[40];
    if (!DefName(r.id, name, sizeof(name))) return false;
    char head[96];
    if (r.sub == 3)
        snprintf(head, sizeof(head), "%s, %d in stock", name, r.level);
    else
        snprintf(head, sizeof(head), "%s, level %d", name, r.level);
    char ability[40];
    if (r.ability && DefName(r.ability, ability, sizeof(ability)))
        snprintf(buf, n, "%s, with %s, %d of %d", head, ability,
                 row + 1, cnt);
    else
        snprintf(buf, n, "%s, %d of %d", head, row + 1, cnt);
    return true;
}

void CampRowFor(CampScreen s, char* buf, size_t n)
{
    switch (s) {
        case CampScreen::Main: CampRow(buf, n); return;
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
        default:
            snprintf(buf, n, "Camp menu."); return;
    }
}

uint32_t g_actorBase = 0;   // located player actor; 0 = not located
uint64_t g_lastScanMs = 0;  // host time of the last locate sweep
int g_charId = -1;          // character id from the player block; -1 = unknown
int g_lastLock = -1;        // last polled lock state: -1 unknown, 0 off, 1 on
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
    if (g_actorBase && ActorPlausible(g_actorBase)) {
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
    if (g_actorBase) ResolveCharacter();
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
// return ring), joined only by the transporter. The positions are
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
    if (CampActive()) return;
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

void CmdWhereAmI(void)
{
    char line[192];
    CampTrack();
    if (CampActive()) {
        // The camp menu opens ON TOP of the field; it wins over the
        // field readout whenever its witnesses say it is up.
        CampWhere(line, sizeof(line));
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
    CampTrack();
    if (CampActive()) { CampRowFor(g_campScreen, row, sizeof(row)); Say(row, "menu", oga::Priority::High); return; }
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

} // namespace

static bool khbbs_attach(const Host* host)
{
    g_host = host;
    g_actorBase = 0; g_lastScanMs = 0; g_charId = -1; g_lastLock = -1; g_lockRec = 0; g_nodeBase = 0; g_nodeActor = 0; g_lastNodeScanMs = 0;  // actor + character located per boot
    g_lastLockScanMs = 0;
    g_lastDeck = -1;
    g_campScreen = CampScreen::None;
    g_lastMain = g_lastSub = g_lastGroup = g_lastSlot = g_lastInv = -1;
    g_lastSlotRaw = -1;
    FaceReset(); g_faceNode = 0;
    return true;   // the game code check already happened in the registry
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
    if (CampActive()) CampTrack();
    // Lock-on edges (VERIFIED 2026-10-18, battle30/31): the player
    // actor's spawn-mark word reads 2 exactly while a lock-on is
    // held and 1 otherwise, across lock, unlock, and relock tests.
    // Announce acquiring a lock; losing one stays silent (it is
    // usually deliberate, and the field line reports current state).
    if (!InField()) { g_lastLock = -1; g_lockRec = 0; g_lastLockScanMs = 0; g_nodeBase = 0; g_nodeActor = 0; FaceReset(); g_faceNode = 0; return; }
    FaceTrack();
    int lock = (u16(g_actorBase + kActorOffMark) == 2) ? 1 : 0;
    if (g_lastLock == 0 && lock == 1)
        Say("Locked on.", "lock", oga::Priority::Normal);
    g_lastLock = lock;
}

static void khbbs_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();  break;
        case Command::NextAlly:        if (InField() && !CampActive()) CmdFoe();       else CmdNotMapped(); break;
        case Command::NextEnemy:       if (InField() && !CampActive()) CmdLockQuery(); else CmdNotMapped(); break;
        case Command::PrevAlly:
        case Command::PrevEnemy:
        case Command::NextUnactedAlly: if (InField() && !CampActive()) CmdSelfLine();  else CmdNotMapped(); break;
        case Command::DumpState:       CmdDump();      break;
        case Command::MenuState:       CmdWhereAmI();  break;
        case Command::MenuNext:        CmdMenuNav();   break;
        case Command::MenuPrev:        CmdMenuNav();   break;
        case Command::MenuLeft:        CmdMenuNav();   break;
        case Command::MenuRight:       CmdMenuNav();   break;
        case Command::Guide:           CmdHubGuide();  break;
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
    return OnTitleMenu() || OnDifficulty() || InField() || CampActive();
}

static void khbbs_detach(void)
{
    g_host = nullptr; g_lastDeck = -1;
    g_actorBase = 0; g_lastScanMs = 0; g_charId = -1; g_lastLock = -1; g_lockRec = 0;
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
    if (!g_host || !InField() || CampActive()) return false;
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
    "Title and difficulty menu speech; camp menu speech; field health, command and gauge speech; lock-on beacon cue; Mirage Arena Hub landmark guide",
    "Spoken: the title menu (New Game, Continue, Install Game — plus "
    "Trinity Archives once clear data exists) and the New Game "
    "difficulty select. In the field, Where Am I speaks your health, "
    "the currently selected deck command, and the Command Gauge. It "
    "also names the world and the room you are in (for example, "
    "Radiant Garden, Central Square), and speaks your position "
    "as two numbers, your coordinates within the room. The "
    "player's field data is located fresh each boot; on boots where "
    "the deck layout is not the verified one, Where Am I speaks "
    "health and gauge only. The camp menu (opened with Start in the field) is spoken: "
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
    "the lobby, the arena terminal, the Moogle Medal Shop and "
    "the transporter back. Each comes with its distance, and "
    "once you have walked a step and the reader knows which "
    "way you are facing, a clock direction too (straight "
    "ahead, 3 o'clock, and so on). A bearing is not a route: "
    "the two floors are joined only by the transporter, so "
    "the other floor's landmarks are never named.\n"
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
