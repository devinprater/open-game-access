/*
 * dbz_adapter.cpp — Dragon Ball Z: Attack of the Saiyans, as an Open Game Access
 * adapter.
 *
 * WHY THIS EXISTS: the map below was established by live measurement over a long
 * investigation (see docs/reverse-engineering/dbz-attack-of-the-saiyans.md), and
 * every address in it was confirmed against the game's OWN screens — the Status
 * page listing the party, and the per-member HP/KI readout. Nothing here is
 * inferred from a plausible-looking value.
 *
 * ⛔ THE VERIFIED MAP (do not adjust without re-confirming on screen):
 *
 *   character record array  (8 entries: Goku, Gohan, Piccolo, Krillin, Tien,
 *                            Yamcha, Bubbles, Gregory)
 *     base   0x020CD660        <- NOT 0x020CD754. An earlier reading used a base
 *     stride 0x24C                one whole stride too high, which made every
 *     name   +0x114               record decode to a plausible NEIGHBOUR.
 *
 *   party pointer array
 *     at     0x020CC774        <- NUL-terminated array of pointers to record BASES
 *     count  0x020CC794        <- low u16 == number of entries
 *
 *   per-record stats (u32, each stored three times: cur / max / display copy,
 *   at +0x00 / +0x04 / +0x08 within the stat block — re-verified 2026-10-25
 *   against the in-game MEMBER screen for Krillin/Tien/Yamcha; an earlier
 *   u16 reading of max was the high half of the u32 current and read 0)
 *     +0x0A0  HP
 *     +0x0B0  KI
 *     +0x240  NEXT (exp to next level)
 *
 * ⛔ WHY THE PUBLISHED ACTION REPLAY CODES READ ZERO: the published base is
 * 0x020CD300, which is 0x360 below the true base. The list is right about the
 * SHAPE of this data (party stats at a 0x24C stride) and wrong about WHERE.
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

// ---- the verified map ------------------------------------------------------
constexpr uint32_t REC_BASE   = 0x020CD660;
constexpr uint32_t REC_STRIDE = 0x24C;
constexpr uint32_t REC_COUNT  = 8;
constexpr uint32_t NAME_OFF   = 0x114;

constexpr uint32_t PARTY_PTR  = 0x020CC774;
constexpr uint32_t PARTY_CNT  = 0x020CC794;
constexpr uint32_t PARTY_MAX  = 4;      // the array is 4 slots + terminator

constexpr uint32_t OFF_HP     = 0x0A0;
constexpr uint32_t OFF_KI     = 0x0B0;
constexpr uint32_t OFF_NEXT   = 0x240;

// ---- the message system (dialogue witness, proven 2026-10-25) -------------
//
// The message context at 0x020D5E00 drives every text box and narration
// page. Measured across 13 dumped states matched to screenshots (opening
// narration, the Launch/Krillin house scene, title card, gameplay, menu):
//
//   +0x08  u16 ACTIVE — 1 while a box/page is open, 0 between boxes and
//          in every non-dialogue state.
//   +0x10  P1 — pointer to the end of the current text in the buffer.
//   +0x40  SPK — pointer to the current message's script record. For a
//          dialogue box it lands on the speaker's display-name string in
//          the script ("Krillin"); for narration it lands on a binary
//          record whose first byte is 0 — no name, correctly.
//   buffer 0x020D5E44 — 0x0C, then the processed text of the CURRENT box
//          (markup stripped, line breaks as '\n'), complete at box open
//          even while the screen is still typing. A multi-page message
//          (the opening narration) appends its pages, each 0x0C-led, so
//          the current page is the LAST 0x0C segment before P1.
//
// ⛔ A box is spoken only when it also AGREES with the loaded script:
// the segment must reconstruct exactly from a <p>…<k> box in the script
// work area (tags skipped, whitespace-normalised, %d a digit wildcard).
// The buffer alone is the game's own text, but the cross-check is what
// keeps a stale or foreign buffer from ever being read aloud.
constexpr uint32_t MSG_FLAG   = 0x020D5E08;
constexpr uint32_t MSG_P1     = 0x020D5E10;
constexpr uint32_t MSG_SPK    = 0x020D5E40;
constexpr uint32_t MSG_BUF    = 0x020D5E44;
constexpr uint32_t MSG_BUFMAX = 0x020D6100;   // P1 never legitimately passes here
constexpr uint32_t SCRIPT_LO  = 0x02200000;
constexpr uint32_t SCRIPT_HI  = 0x02300000;

// ---- the main-menu dial (proven 2026-10-25) --------------------------------
//
// The word at 0x020D2128 is the dial's state. In every menu state it reads
// 0x0v0v0202: the low u16 0x0202 marks the dial screen, and the byte v is
// mirrored at +2/+3 holding (5 - index) mod 7 over the dial's seven entries
// (rotation order): Items 5, Skills 4, Equip 3, Status 2, Options 1,
// Switch 0, Capsules 6. Proven across eight dumped states, every one
// matched to its bottom-screen screenshot — including UP from Items
// wrapping to Capsules (v=6) and close+reopen returning to Items (v=5):
// the word is history-independent selection state. Outside the menu it
// reads entirely different values (0x00000203, 0x01110000, ...), so the
// gate is: low u16 == 0x0202, mirrored bytes equal, v <= 6.
// ⛔ The script-VM frame at 0x020D8E40+ is NOT this state — its counters
// tracked four DOWN rotations [1,2,3,4] by coincidence and were falsified
// by the UP-wrap and reopen states. Do not "simplify" back to it.
constexpr uint32_t MENU_WORD = 0x020D2128;
const char* const kMenuEntries[7] = {
    "Items", "Skills", "Equip", "Status", "Options", "Switch", "Capsules",
};

// ---- battle state (proven 2026-10-26, round 9) ------------------------------
//
//   0x020CC668  u32 FLAG — 1 while a battle (or its scripted tutorial
//               chain) is loaded; 0 on the field, the world map, and in
//               field event scenes. Measured across map/field/scene/
//               battle dumps matched to screenshots.
//   0x020CC66C  u32 PHASE — 0x11 = battle command/action flow;
//               0x09/0x19 = in-battle tutorial pages; 0x02 = the
//               POST-battle chain (results, Krillin's help pages) —
//               the field is walkable again under (1, 0x02), so battle
//               speech keys on flag==1 AND phase != 0x02.
//   Enemy display names: two NUL-terminated strings that exist only
//   while a battle is loaded — 0x020CE9D4 and 0x020CEF88 ("Giant Snake
//   A"/"Giant Snake B" in the first battle, "RR Deserter A"/"B" in
//   the second; both read as zeros on the field and the map).
//   Sanity gate: slot 0's party record (via PARTY_PTR) must carry a
//   plausible HP triple — the record layout is (current, display,
//   MAX) at OFF_HP/+4/+8, proven when damage separated the three
//   words (224, 224, 300). The +4 word is the bar's drain animation,
//   NOT the max; round 9 corrected the party speech below, which had
//   been printing it as the maximum.
constexpr uint32_t BTL_FLAG   = 0x020CC668;
constexpr uint32_t BTL_PHASE  = 0x020CC66C;
constexpr uint32_t ENM_NAME1  = 0x020CE9D4;
constexpr uint32_t ENM_NAME2  = 0x020CEF88;

uint16_t u16(uint32_t a) { return g_host ? g_host->read16(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

// Speech routed through the announcement queue with a per-site group and
// priority. Null-queue hosts fall back to the direct wire, which keeps the
// host tests' synchronous stubs working unchanged.
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

/// Read a NUL-terminated ASCII name at an absolute address. Returns false when the
/// bytes there are not printable text, which is the signal that we are NOT looking
/// at a character record — the same "prove it before you speak it" rule the other
/// adapters follow.
bool ReadName(uint32_t addr, char* out, size_t cap)
{
    size_t n = 0;
    for (; n + 1 < cap; n++) {
        uint8_t c = g_host ? g_host->read8(g_host->ctx, addr + (uint32_t) n) : 0;
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;   // not text
        out[n] = (char) c;
    }
    out[n] = 0;
    return n >= 2;
}

/// Resolve party slot `i` (0-based) to a record index, or -1 when empty/invalid.
///
/// ⛔ VALIDATED, NOT ASSUMED. A slot is only accepted when its pointer is 4-aligned
/// and lands EXACTLY on a record boundary ((ptr - base) % stride == 0) inside the
/// array, AND the name at that record is readable text. That triple check is what
/// would have caught the one-stride base error immediately, so it is built in.
int PartySlot(int i)
{
    if (i < 0 || i >= (int) PARTY_MAX) return -1;
    uint32_t ptr = u32(PARTY_PTR + (uint32_t) i * 4);
    if (ptr == 0) return -1;                       // terminator
    if (ptr & 3) return -1;                        // not a valid pointer
    if (ptr < REC_BASE) return -1;
    uint32_t delta = ptr - REC_BASE;
    if (delta % REC_STRIDE) return -1;             // NOT a record base
    uint32_t rec = delta / REC_STRIDE;
    if (rec >= REC_COUNT) return -1;               // outside the array
    char nm[24];
    if (!ReadName(REC_BASE + rec * REC_STRIDE + NAME_OFF, nm, sizeof(nm))) return -1;
    return (int) rec;
}

// ⛔ START AT -1 ("nothing chosen yet"), NOT 0. CmdNextAlly advances before it
// speaks, so a cursor initialised to 0 makes the very first "next ally" skip the
// party's FIRST member and announce the second — the player never hears slot 0.
// -1 makes the first press land on slot 0, which is what "next" means from a
// standing start. (Found by the host test; it is invisible in a manual play-through
// because the skip is only ever one member, once.)
int g_cursor = -1;   // which party member the speech commands walk

// ---- dialogue watcher ------------------------------------------------------
//
// State for the per-frame box watcher. The last segment is remembered so a
// box speaks once; the memory is cleared only after the message system has
// been idle for several frames (boxes close between story beats — that gap
// is what lets an identical next box speak again — while a 1-frame flag
// flicker must not re-speak the box that is still up).
char g_dlg_spoken[448];
bool g_dlg_have_spoken = false;
char g_dlg_cand[448];
int  g_dlg_cand_frames = 0;
int  g_dlg_idle_frames = 0;

uint8_t u8(uint32_t a) { return g_host ? g_host->read8(g_host->ctx, a) : 0; }

/// Copy `s` into `out` with all whitespace removed (the script raw text and
/// the processed buffer segment differ only in whitespace: the engine wraps
/// lines the script leaves whole, and <n> breaks are display detail).
void DlgNorm(const char* s, char* out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; s[i] && n + 1 < cap; i++) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        out[n++] = c;
    }
    out[n] = 0;
}

/// Match a normalised script box against the normalised segment. 0x01 in
/// `raw` is a %d/%s wildcard and matches a run of one or more digits.
bool DlgGlob(const char* raw, const char* seg)
{
    while (*raw) {
        if (*raw == 0x01) {
            raw++;
            if (*seg < '0' || *seg > '9') return false;
            while (*seg >= '0' && *seg <= '9') seg++;
            // Greedy digit consumption could over-match if the next raw
            // char is also a digit; boxes end on punctuation in practice,
            // and the full-string anchor below catches any overrun.
            continue;
        }
        if (*raw != *seg) return false;
        raw++; seg++;
    }
    return *seg == 0;
}

/// Reconstruct the raw script box starting just after a "<p>" at `pos`,
/// normalised, into `out`. Returns false when the box is malformed
/// (unterminated, NUL before <k>, absurd length).
bool DlgReconstruct(uint32_t pos, char* out, size_t cap)
{
    size_t n = 0;
    uint32_t a = pos;
    for (uint32_t walked = 0; walked < 8192; walked++, a++) {
        uint8_t c = u8(a);
        if (c == 0) return false;
        if (c == '<') {
            // A tag: read to '>'. "<k>" ends the box; anything else
            // (<n>, <w=30>, <c=#…>) is a control code — <n>'s line break
            // normalises away like other whitespace.
            char tag[20]; size_t t = 0; uint32_t j = a + 1;
            for (; j < a + 24; j++) {
                uint8_t d = u8(j);
                if (d == '>') break;
                if (d == 0 || d == '<') return false;
                if (t + 1 < sizeof(tag)) tag[t++] = (char) d;
            }
            if (u8(j) != '>') return false;
            tag[t] = 0;
            if (strcmp(tag, "k") == 0) { out[n] = 0; return n > 0; }
            if (strcmp(tag, "p") == 0) return false;   // a new box: bad start
            a = j;   // the loop's ++ moves past '>'
            continue;
        }
        if (c == '%') {
            uint8_t d = u8(a + 1);
            if (d == 'd' || d == 's') {
                if (n + 1 < cap) out[n++] = 0x01;
                a++;
                continue;
            }
            if (d == '%') { if (n + 1 < cap) out[n++] = '%'; a++; continue; }
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c < 0x20 || c > 0x7E) return false;
        if (n + 1 < cap) out[n++] = (char) c;
    }
    return false;
}

/// Does the loaded script contain exactly this box? The search window is
/// anchored on SPK (the current message's script record — the box lives
/// within a few KB of it), falling back to the whole script work area
/// when SPK is unusable.
bool DlgSegmentInScript(const char* seg)
{
    char segNorm[448];
    DlgNorm(seg, segNorm, sizeof(segNorm));
    if (!segNorm[0]) return false;

    uint32_t spk = u32(MSG_SPK);
    uint32_t lo = SCRIPT_LO, hi = SCRIPT_HI;
    if (spk >= SCRIPT_LO && spk < SCRIPT_HI) {
        lo = spk > SCRIPT_LO + 0x2000 ? spk - 0x2000 : SCRIPT_LO;
        hi = spk + 0x8000 < SCRIPT_HI ? spk + 0x8000 : SCRIPT_HI;
    }
    char rawNorm[1024];
    for (uint32_t a = lo; a + 3 < hi; a++) {
        if (u8(a) == '<' && u8(a + 1) == 'p' && u8(a + 2) == '>') {
            if (DlgReconstruct(a + 3, rawNorm, sizeof(rawNorm)) &&
                DlgGlob(rawNorm, segNorm))
                return true;
        }
    }
    return false;
}

/// The speaker's display name for the current box, read from the script
/// record SPK points at. False when there is no usable name (narration's
/// record is binary; a name must be clean capitalised text).
bool DlgSpeakerName(char* out, size_t cap)
{
    uint32_t spk = u32(MSG_SPK);
    if (spk < SCRIPT_LO || spk >= SCRIPT_HI) return false;
    size_t n = 0;
    for (; n + 1 < cap; n++) {
        uint8_t c = u8(spk + (uint32_t) n);
        if (c == 0) break;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  c == ' ' || c == '.' || c == '\'' || c == '-';
        if (!ok) return false;
        out[n] = (char) c;
    }
    out[n] = 0;
    return n >= 2 && out[0] >= 'A' && out[0] <= 'Z';
}

// ---- menu watcher ------------------------------------------------------------
//
// Speaks the dial's arrival and cursor. The candidate index must be stable
// for two consecutive frames (the word is written by the UI while we poll
// it, and the dial animates). Only changes speak; closing is silent.
int g_menu_idx = -1;        // last spoken selection, -1 = menu closed
int g_menu_cand = -1;
int g_menu_cand_frames = 0;

int DbzMenuIndex(void)
{
    if (u16(MENU_WORD) != 0x0202) return -1;
    uint8_t v = u8(MENU_WORD + 2);
    if (v > 6 || u8(MENU_WORD + 3) != v) return -1;
    return (5 - (int) v + 7) % 7;
}

void DbzMenuFrame(void)
{
    int idx = DbzMenuIndex();
    if (idx != g_menu_cand) { g_menu_cand = idx; g_menu_cand_frames = 1; return; }
    if (++g_menu_cand_frames < 2) return;
    if (idx == g_menu_idx) return;
    bool opening = g_menu_idx < 0 && idx >= 0;
    g_menu_idx = idx;
    if (idx < 0) return;
    if (opening) {
        char line[64];
        snprintf(line, sizeof(line), "Menu. %s.", kMenuEntries[idx]);
        Say(line, "menu", oga::Priority::High);
    } else {
        Say(kMenuEntries[idx], "menu", oga::Priority::Normal);
    }
}

// ---- battle watcher ----------------------------------------------------------
//
// Announces battle start (with the enemies' names when the game has
// them resident) and battle end. Transitions only, debounced over two
// consecutive frames like the menu watcher, and fail-closed: the flag,
// the phase, and a sane slot-0 HP triple must all agree before
// anything is spoken. Only the first two enemy name slots are proven;
// a battle against a single named enemy speaks just that name, and a
// battle whose names are not readable falls back to a plain line.
bool g_btl_in = false;
bool g_btl_cand = false;
int  g_btl_cand_frames = 0;

bool DbzBattleActive(void)
{
    if (u32(BTL_FLAG) != 1) return false;
    if (u32(BTL_PHASE) == 0x02) return false;   // post-battle chain / field
    uint32_t rec = u32(PARTY_PTR);
    if (rec == 0) return false;
    uint32_t cur = u32(rec + OFF_HP);
    uint32_t max = u32(rec + OFF_HP + 8);
    return max >= 1 && max <= 99999 && cur <= max;
}

void DbzBattleFrame(void)
{
    bool now = DbzBattleActive();
    if (now != g_btl_cand) { g_btl_cand = now; g_btl_cand_frames = 1; return; }
    if (++g_btl_cand_frames < 2) return;
    if (now == g_btl_in) return;
    g_btl_in = now;
    if (now) {
        char n1[28], n2[28];
        bool h1 = ReadName(ENM_NAME1, n1, sizeof(n1));
        bool h2 = ReadName(ENM_NAME2, n2, sizeof(n2));
        char line[96];
        if (h1 && h2)     snprintf(line, sizeof(line), "Battle: %s and %s.", n1, n2);
        else if (h1)      snprintf(line, sizeof(line), "Battle: %s.", n1);
        else              snprintf(line, sizeof(line), "Battle started.");
        Say(line, "battle", oga::Priority::Normal);
    } else {
        Say("Battle over.", "battle", oga::Priority::Normal);
    }
}

// ---- Active Guard cue (proven 2026-10-26, rounds 10–11) --------------------
//
// The game's timed defence: an enemy attack is preceded by a guard
// window, and pressing the right button just before impact cuts the
// damage. The window is witnessed by the action-descriptor block at
// 0x020CCBB0, measured per-frame across 19 enemy attacks in three
// families (rounds 10–11):
//
//   +0x03  target byte — 0x80|row while an enemy attack is inbound,
//          0xFF when idle, 0x00 during party actions. ⛔ The low bits
//          are the target's FORMATION ROW, not its party slot: the
//          tutorial battle's ghost read 0x81 twice while the damage
//          landed on slot 0 (round 10's battles read 0x80→slot 0 and
//          0x82→slot 2). Rows are per-battle, so this byte must NEVER
//          be turned into a member's name. It is used only as a
//          sanity witness (must be 0x80..0x82) and never spoken.
//   +0x0D  window flag — 1 while the approach runs (the countdown is
//          falling), 2 from countdown-zero until the action clears
//          (projectile flight / strike resolution), 0 when idle.
//   +0x0E  u16 countdown — the RANGED family: armed at the attack's
//          telegraph (13..29 observed), falls exactly 1 per frame to
//          0 at launch; a burst arms ONCE, before its first tick.
//          The HEAVY melee family counts down +0x10 (19→0) instead
//          with the flag up; LIGHT melee has no countdown at all —
//          the flag's own lead (~28 frames) is its only witness.
//   Party attacks write the block too (target 0, flag 0, no
//   countdown), which the arm condition below excludes.
//
// The cue (the owner's sound-first design; Budokai's rising-tone
// model): a tone whose pitch sweeps low → peak across the window so
// the PEAK is the guard moment, a soft beat when the window opens,
// NO speech on the timing moment. One setup line at the opening
// ("Guard!") and one factual outcome line after impact, naming the
// member whose HP actually fell. Fail-closed everywhere: an
// inconsistent block (target byte outside 0x80..0x82, countdown
// implausible) never arms, and an attack that lands no damage (a
// miss, a full block) ends in silence, not invention.
constexpr uint32_t AG_TGT = 0x020CCBB3;
constexpr uint32_t AG_WIN = 0x020CCBBD;
constexpr uint32_t AG_CNT = 0x020CCBBE;   // u16
constexpr uint32_t AG_HVY = 0x020CCBC0;
constexpr int AG_MAX_COUNT = 240;         // sanity bound on any countdown
constexpr int AG_LIGHT_WINDOW = 28;       // light melee: the flag's measured lead
constexpr int AG_HEAVY_START = 19;        // heavy melee: +0x10's proven start value

bool     g_guard_armed = false;
bool     g_guard_beat = false;            // one-frame, consumed by the snapshot
float    g_guard_frac = 0.0f;
int      g_guard_window = 0;              // frames the current window spans
int      g_guard_left = 0;                // frames left (synthetic-count families)
bool     g_guard_counted = false;         // the window runs off a real countdown
uint8_t  g_guard_prev_hvy = 0;            // last frame's +0x10 (heavy-fall detection)
int      g_guard_grace = 0;               // frames after close still watching for impact
bool     g_guard_spoken_hit = false;      // outcome line already given for this arm
uint32_t g_guard_hp[PARTY_MAX];           // party HP when the window opened
bool     g_guard_hp_ok[PARTY_MAX];        // per-slot: the slot was occupied then

void GuardReset(void)
{
    g_guard_armed = false;
    g_guard_frac = 0.0f;
    g_guard_window = 0;
    g_guard_left = 0;
    g_guard_counted = false;
    g_guard_prev_hvy = 0;
    g_guard_grace = 0;
    g_guard_spoken_hit = false;
    for (int i = 0; i < (int) PARTY_MAX; i++) g_guard_hp_ok[i] = false;
}

// The member whose HP fell since the window opened, if any: slot
// order, first drop wins. Returns the damage or -1 for no drop.
int GuardImpact(int* slot_out)
{
    for (int i = 0; i < (int) PARTY_MAX; i++) {
        if (!g_guard_hp_ok[i]) continue;
        int rec = PartySlot(i);
        if (rec < 0) continue;
        uint32_t now = u32(REC_BASE + (uint32_t) rec * REC_STRIDE + OFF_HP);
        if (now < g_guard_hp[i]) {
            if (slot_out) *slot_out = i;
            return (int) (g_guard_hp[i] - now);
        }
    }
    return -1;
}

void GuardSpeakHit(void)
{
    int slot = -1;
    int dmg = GuardImpact(&slot);
    if (dmg < 0) return;
    g_guard_spoken_hit = true;
    int rec = PartySlot(slot);
    char nm[24];
    char line[96];
    if (rec >= 0 && ReadName(REC_BASE + (uint32_t) rec * REC_STRIDE + NAME_OFF,
                             nm, sizeof(nm)))
        snprintf(line, sizeof(line), "%s took %d damage.", nm, dmg);
    else
        snprintf(line, sizeof(line), "Took %d damage.", dmg);
    Say(line, "battle", oga::Priority::Normal);
}

void DbzGuardFrame(void)
{
    if (!DbzBattleActive()) { GuardReset(); return; }

    uint8_t  tgt  = u8(AG_TGT);
    uint8_t  win  = u8(AG_WIN);
    uint16_t cnt  = u16(AG_CNT);
    uint8_t  hvy  = u8(AG_HVY);
    bool tgt_ok = (tgt >= 0x80 && tgt <= 0x82);
    bool sane = tgt_ok && cnt <= AG_MAX_COUNT && hvy <= AG_MAX_COUNT;
    bool inbound = sane && (win != 0 || cnt != 0);

    if (!g_guard_armed) {
        if (!inbound) {
            if (g_guard_grace > 0) {
                // The window closed but a slow attack may still land
                // (the tutorial ghost's flame hit 47 frames after its
                // countdown ended, inside its flag-2 phase; a block
                // that clears early gets the same courtesy). Watch
                // for the hit, then stop either way. A NEW window
                // opening cuts the grace short — it arms below.
                if (!g_guard_spoken_hit) GuardSpeakHit();
                if (g_guard_spoken_hit || --g_guard_grace <= 0) g_guard_grace = 0;
            }
            return;
        }
        g_guard_grace = 0;
        // Arm. The countdown showing now IS the window's length for
        // the family that has one; a flag-only opening starts from
        // light melee's witnessed lead and adopts a real countdown
        // (the ranged u16, or a heavy's falling +0x10) if one starts.
        g_guard_armed = true;
        g_guard_spoken_hit = false;
        g_guard_counted = (cnt != 0);
        g_guard_window = cnt ? cnt : AG_LIGHT_WINDOW;
        g_guard_left = g_guard_window;
        g_guard_prev_hvy = hvy;
        g_guard_frac = 0.0f;
        g_guard_beat = true;
        for (int i = 0; i < (int) PARTY_MAX; i++) {
            int rec = PartySlot(i);
            g_guard_hp_ok[i] = (rec >= 0);
            g_guard_hp[i] = (rec >= 0)
                ? u32(REC_BASE + (uint32_t) rec * REC_STRIDE + OFF_HP) : 0;
        }
        Say("Guard!", "battle", oga::Priority::Normal);
        return;
    }

    // Armed. Advance the sweep from whichever witness is live.
    if (cnt != 0) {
        if (!g_guard_counted || cnt > g_guard_left + 2) {
            // A countdown armed (or re-armed higher) mid-window: it is
            // the window now — restart the sweep on it, with the beat.
            g_guard_counted = true;
            g_guard_window = cnt;
            g_guard_beat = true;
        }
        g_guard_left = cnt;
        g_guard_frac = 1.0f - (float) cnt / (float) g_guard_window;
    } else if (win == 2) {
        g_guard_frac = 1.0f;   // launched: the moment is now
    } else if (win == 1 && hvy < g_guard_prev_hvy) {
        // Heavy melee closing in: +0x10 is falling toward the strike.
        // Its scale is its own proven start value, not the window's.
        g_guard_frac = 1.0f - (float) hvy / (float) AG_HEAVY_START;
    } else if (win == 1) {
        // Flag-only approach (light melee): run the witnessed lead
        // down a frame at a time.
        if (g_guard_left > 0) g_guard_left--;
        g_guard_frac = 1.0f - (float) g_guard_left / (float) g_guard_window;
    } else {
        // The block cleared. One last impact check this frame, then
        // a short grace for a slow projectile.
        if (!g_guard_spoken_hit) GuardSpeakHit();
        g_guard_armed = false;
        g_guard_frac = 0.0f;
        g_guard_grace = g_guard_spoken_hit ? 0 : 100;
        return;
    }
    if (g_guard_frac < 0.0f) g_guard_frac = 0.0f;
    if (g_guard_frac > 1.0f) g_guard_frac = 1.0f;
    g_guard_prev_hvy = hvy;
    if (!g_guard_spoken_hit) GuardSpeakHit();
}

// The cue snapshot: the guard fields ride the shared per-frame poll.
// The one-frame beat is consumed here, exactly like the health beeps.
static bool dbz_cue_snapshot(oga::CueSnapshot* out)
{
    if (!out) return false;
    *out = oga::CueSnapshot{};
    out->battle = DbzBattleActive();
    out->guard_active = g_guard_armed;
    out->guard_frac = g_guard_armed ? g_guard_frac : 0.0f;
    out->guard_beat = g_guard_beat;
    g_guard_beat = false;
    return true;
}

void DbzDialogueFrame(void)
{
    if (u16(MSG_FLAG) != 1) {
        if (++g_dlg_idle_frames >= 3) { g_dlg_have_spoken = false; }
        g_dlg_cand_frames = 0;
        return;
    }
    g_dlg_idle_frames = 0;

    uint32_t p1 = u32(MSG_P1);
    if (p1 <= MSG_BUF || p1 > MSG_BUFMAX) { g_dlg_cand_frames = 0; return; }
    if (u8(MSG_BUF) != 0x0C) { g_dlg_cand_frames = 0; return; }

    // Read the buffer and take the LAST 0x0C-led segment before P1: for a
    // single box that is the box; for a multi-page message it is the
    // current page (earlier pages were spoken when they were last).
    uint32_t len = p1 - MSG_BUF;
    char seg[448];
    size_t sn = 0;
    bool bad = false;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = u8(MSG_BUF + i);
        if (c == 0x0C) { sn = 0; continue; }
        if (c == '\r' || c == 0) continue;      // terminator noise at box end
        if (c != '\n' && (c < 0x20 || c > 0x7E)) { bad = true; break; }
        if (sn + 1 < sizeof(seg)) seg[sn++] = (char) c;
        else { bad = true; break; }
    }
    seg[sn] = 0;
    // Trailing whitespace/newlines are not part of the line.
    while (sn > 0 && (seg[sn - 1] == ' ' || seg[sn - 1] == '\n')) seg[--sn] = 0;
    if (bad || sn == 0) { g_dlg_cand_frames = 0; return; }

    // Debounce: the segment must be byte-identical on two consecutive
    // frames before it is even validated — the buffer is written by the
    // engine while we poll it.
    if (strcmp(seg, g_dlg_cand) != 0) {
        strncpy(g_dlg_cand, seg, sizeof(g_dlg_cand) - 1);
        g_dlg_cand[sizeof(g_dlg_cand) - 1] = 0;
        g_dlg_cand_frames = 1;
        return;
    }
    if (++g_dlg_cand_frames < 2) return;
    if (g_dlg_have_spoken && strcmp(seg, g_dlg_spoken) == 0) return;

    // Remember the segment either way: a box that fails validation stays
    // silent for its whole display, never retried frame after frame.
    strncpy(g_dlg_spoken, seg, sizeof(g_dlg_spoken) - 1);
    g_dlg_spoken[sizeof(g_dlg_spoken) - 1] = 0;
    g_dlg_have_spoken = true;

    if (!DlgSegmentInScript(seg)) {
        Log("[dbz] dialogue segment not confirmed in script; staying silent");
        return;
    }

    char name[28];
    bool hasName = DlgSpeakerName(name, sizeof(name));
    char line[512];
    char flat[448];
    size_t fn = 0;
    for (size_t i = 0; seg[i] && fn + 1 < sizeof(flat); i++)
        flat[fn++] = seg[i] == '\n' ? ' ' : seg[i];
    flat[fn] = 0;
    if (hasName) snprintf(line, sizeof(line), "%s: %s", name, flat);
    else         snprintf(line, sizeof(line), "%s", flat);
    // High, like the other adapters' story dialogue: sequential story
    // beats must all speak, even two identical boxes in a row — the
    // latch above, not the shared dedup, is the repeat guard.
    Say(line, "dialogue", oga::Priority::High);
}

// ---- commands --------------------------------------------------------------

void SpeakMember(int rec)
{
    uint32_t base = REC_BASE + (uint32_t) rec * REC_STRIDE;
    char nm[24];
    if (!ReadName(base + NAME_OFF, nm, sizeof(nm))) { Say("Unknown member.", "party", oga::Priority::High); return; }

    uint32_t hp  = u32(base + OFF_HP);
    uint32_t hpM = u32(base + OFF_HP + 8);
    uint32_t ki  = u32(base + OFF_KI);
    uint32_t kiM = u32(base + OFF_KI + 8);
    uint32_t nxt = u32(base + OFF_NEXT);

    char line[160];
    // HP/Ki: each is a (current, display, MAX) u32 triple — the max is
    // the THIRD word (see the battle-state note above; the second word
    // only animates toward the first and must never be spoken as a
    // maximum). Print both when they differ, because "current" alone
    // is ambiguous mid-battle.
    if (hpM && hpM != hp)
        snprintf(line, sizeof(line), "%s. HP %u of %u. Ki %u of %u.",
                 nm, hp, hpM, ki, kiM ? kiM : ki);
    else
        snprintf(line, sizeof(line), "%s. HP %u. Ki %u.", nm, hp, ki);
    Say(line, "party", oga::Priority::High);

    if (nxt) {
        snprintf(line, sizeof(line), "%u experience to the next level.", nxt);
        Say(line, "party", oga::Priority::Normal);   // queue behind the stats rather than interrupting
    }
}

void CmdWhereAmI(void)
{
    // ⛔ HONEST SCOPE. This adapter reads the PARTY, not the map. There is no
    // verified map/position structure for this game yet, so rather than narrate a
    // guess it says what it actually knows: the whole party, in slot order.
    //
    // This used to name only the member under the speech cursor, which made the
    // "where am I" key read like a location answer while delivering one member.
    // A status query should answer with the party's status — all of it, in one
    // press — because that is the question the player is actually asking on the
    // field: "how is everyone?" Next/Prev still walk member by member.
    int n = 0;
    for (int i = 0; i < (int) PARTY_MAX; i++)
        if (PartySlot(i) >= 0) n++;
    if (n == 0) { Say("Party not available yet.", "party", oga::Priority::High); return; }

    char line[64];
    snprintf(line, sizeof(line), n == 1 ? "Party of 1." : "Party of %d.", n);
    Say(line, "party", oga::Priority::High);
    for (int i = 0; i < (int) PARTY_MAX; i++) {
        int rec = PartySlot(i);
        if (rec >= 0) SpeakMember(rec);
    }
}

void CmdNextAlly(int dir)
{
    // From a standing start (-1, nothing spoken yet) "next" is the FIRST member and
    // "previous" is the LAST one — not a wrap through the middle of the party.
    int start = (g_cursor < 0) ? (dir > 0 ? -1 : (int) PARTY_MAX) : g_cursor;
    for (int step = 0; step < (int) PARTY_MAX; step++) {
        int i = start + dir * (step + 1);
        i = ((i % (int) PARTY_MAX) + (int) PARTY_MAX) % (int) PARTY_MAX;
        int rec = PartySlot(i);
        if (rec >= 0) {
            g_cursor = i;
            SpeakMember(rec);
            return;
        }
    }
    Say("No party members found.", "party", oga::Priority::High);
}

void CmdNextEnemy(int /*dir*/)
{
    // ⛔ NOT APPLICABLE, AND SAYING SO IS THE CORRECT BEHAVIOUR. This game's enemy
    // data was not located (only the enemy NAME table in the ROM was). Inventing an
    // answer here would be worse than refusing — the same rule the GBA adapter
    // follows for its own unsupported commands.
    Say("Not applicable in this game.", "status", oga::Priority::High);
}

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[dbz] base=0x%08X stride=0x%X party=0x%08X count=0x%08X",
             REC_BASE, REC_STRIDE, PARTY_PTR, u32(PARTY_CNT));
    Log(line);

    for (int i = 0; i < (int) PARTY_MAX; i++) {
        uint32_t ptr = u32(PARTY_PTR + (uint32_t) i * 4);
        int rec = PartySlot(i);
        if (rec < 0) {
            snprintf(line, sizeof(line), "[dbz] slot %d ptr=0x%08X (none)", i, ptr);
            Log(line);
            continue;
        }
        uint32_t base = REC_BASE + (uint32_t) rec * REC_STRIDE;
        char nm[24];
        ReadName(base + NAME_OFF, nm, sizeof(nm));
        snprintf(line, sizeof(line),
                 "[dbz] slot %d ptr=0x%08X rec=%d name=%s hp=%u/%u ki=%u/%u next=%u",
                 i, ptr, rec, nm,
                 u32(base + OFF_HP), u32(base + OFF_HP + 4),
                 u32(base + OFF_KI), u32(base + OFF_KI + 4),
                 u32(base + OFF_NEXT));
        Log(line);
    }
}

bool DbzReady(void)
{
    // ⛔ READY ONLY WHEN THE DATA IS ACTUALLY THERE. Before the game allocates its
    // character array, these reads return zero/garbage; speaking then would read
    // nonsense to the player. Require a valid record name behind at least one party
    // slot — the same gate the other adapters use, for the same reason.
    return PartySlot(0) >= 0;
}

} // namespace

static bool dbz_attach(const Host* host)
{
    g_host = host;
    g_cursor = -1;
    g_dlg_have_spoken = false;
    g_dlg_cand_frames = 0;
    g_dlg_idle_frames = 0;
    g_dlg_cand[0] = 0;
    g_menu_idx = -1;
    g_menu_cand = -1;
    g_menu_cand_frames = 0;
    g_btl_in = false;
    g_btl_cand = false;
    g_btl_cand_frames = 0;
    GuardReset();
    g_guard_beat = false;
    return true;   // the game code check already happened in the registry
}

static void dbz_on_frame(void) { DbzDialogueFrame(); DbzMenuFrame(); DbzBattleFrame(); DbzGuardFrame(); }

static void dbz_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:          CmdWhereAmI();            break;
        case Command::NextAlly:          CmdNextAlly(+1);          break;
        case Command::PrevAlly:          CmdNextAlly(-1);          break;
        case Command::NextEnemy:         CmdNextEnemy(+1);         break;
        case Command::PrevEnemy:         CmdNextEnemy(-1);         break;
        case Command::NextUnactedAlly:   CmdNextAlly(+1);          break;
        case Command::DumpState:         CmdDump();                break;
    }
}

static bool dbz_ready(void) { return DbzReady(); }

static void dbz_detach(void)
{
    g_host = nullptr;
    g_cursor = -1;
    g_dlg_have_spoken = false;
    g_dlg_cand_frames = 0;
    g_dlg_cand[0] = 0;
    g_menu_idx = -1;
    g_menu_cand = -1;
    g_menu_cand_frames = 0;
    g_btl_in = false;
    g_btl_cand = false;
    g_btl_cand_frames = 0;
    GuardReset();
    g_guard_beat = false;
}

extern const Adapter kDragonBallZSaiyans;
const Adapter kDragonBallZSaiyans = {
    "dbz-saiyans",
    "Dragon Ball Z: Attack of the Saiyans",
    "BRPE",                 // ROM header game code (0x0C..0x0F)
    dbz_attach,
    dbz_on_frame,
    dbz_command,
    dbz_ready,
    dbz_detach,
    dbz_cue_snapshot,
    "Party status, story dialogue, menu, battle transitions, and "
    "Active Guard timing cues",
    "Story dialogue is read aloud as it appears, with the speaker's "
    "name — the game's own text, confirmed against its script before "
    "anything is spoken. The main menu announces itself and speaks "
    "each entry as you move around its dial. Speech also covers your "
    "party — who is in it, in slot order — read from the game's own "
    "party data, the same data its Status screen shows. Battle start "
    "is announced with the enemies' names, and battle end is "
    "announced. When an enemy attack is incoming, a rising tone "
    "sweeps up to the moment to attempt an Active Guard, and the "
    "damage an attack deals is spoken after it lands. Field "
    "navigation is not covered yet.\n"
    "Supported version: Dragon Ball Z: Attack of the Saiyans (game "
    "code BRPE).",
};

} // namespace oga
