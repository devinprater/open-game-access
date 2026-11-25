/*
 * mkt_adapter.cpp — Mortal Kombat Trilogy (PlayStation, USA, SLUS-00330).
 *
 * THE FIRST PLAYSTATION GAME READER, AND A SCAFFOLD ON PURPOSE. The disc's boot serial is the
 * registry key (pokecore's LoadPs1Rom looks it up before falling back to the console row in
 * ps1_adapter.cpp), so loading this disc selects this reader. What it does today:
 *
 *   - says plainly what is not mapped yet, rather than guessing;
 *   - DumpState: scans all 2 MiB of PlayStation main RAM for the game's own words (fighter
 *     names, menu words) and logs every hit with its address, and logs what the published
 *     GameShark health addresses hold right now, labelled as unconfirmed candidates;
 *   - carries the fighting-game cues the owner asked for, the ones Mortal Kombat: Unchained
 *     already has (Core/mku_adapter.cpp), each switched on by its own measured map fields:
 *       * health: a pitch-mapped beep on every hit (foe on the right, you on the left, the
 *         pitch falling with the health), with the percent spoken only at 75, 50, 25 and
 *         nearly gone — the MKU and Birth by Sleep health language, unchanged;
 *       * closeness: the beacon the battle-cue synth already plays, its pace carrying how far
 *         apart the two fighters are (MKU's fight-proximity cue), silent while paused;
 *       * side: one short line when the foe ends up on your other side (a jump over, a
 *         teleport), because in a 2D fighter the side decides which way every move goes;
 *       * the pause menu: "Paused." and the highlighted row, then each row as the highlight
 *         moves.
 *
 * ⛔ NO ADDRESS HERE IS MEASURED YET, so every one of those cues is OFF in the shipped map
 * (Core/mkt_adapter.h). The host test proves each one against fake RAM; a phone run with the
 * debug channel's `search` fills the real map (docs/reverse-engineering/mortal-kombat-trilogy.md).
 *
 * READS. Host reads reach PlayStation main RAM at 0x80000000 (2 MiB, every mirror) and the
 * scratchpad at 0x1F800000; anything else reads 0 (the Host contract).
 */
#include "adapter.h"
#include "mkt_adapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace oga {

namespace {

const Host* g_host = nullptr;

// ⛔ THE SHIPPED MAP: EMPTY UNTIL MEASURED. Value-initialized, so every address is 0 and every
// feature that needs one stays off. Fill a field only with an address confirmed live.
const MktMap kShippedMap = {};
const MktMap* g_map = &kShippedMap;

constexpr uint32_t kRamBase = 0x80000000;
constexpr uint32_t kRamEnd  = 0x80200000;   // 2 MiB of main RAM

uint8_t  u8 (uint32_t a) { return g_host && g_host->read8  ? g_host->read8 (g_host->ctx, a) : 0; }
uint16_t u16(uint32_t a) { return g_host && g_host->read16 ? g_host->read16(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host && g_host->read32 ? g_host->read32(g_host->ctx, a) : 0; }

void Say(const char* s, const char* group, Priority pri)
{
    // High = the player asked, so it always speaks; it is still noted, so an automatic repeat
    // of the same line right after stays quiet (the MKU Say).
    if (!AdapterNoteSpoken(s) && pri != Priority::High) return;
    if (g_host && g_host->announce_q)
        announce(g_host->announce_q, Announcement{s, pri, group, nullptr, 0, 0, -1}, g_host->now_ms);
    else if (g_host && g_host->speak)
        g_host->speak(g_host->ctx, s, pri == Priority::High);
}

void Log(const char* s)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, s);
}

// ---- what the map has ------------------------------------------------------------------

bool HasFight(void)  { return g_map->fight_addr != 0; }
bool HasHealth(void)
{
    return HasFight() && g_map->p1_health_addr && g_map->p2_health_addr && g_map->health_full;
}
bool HasPos(void)
{
    return HasFight() && g_map->p1_x_addr && g_map->p2_x_addr &&
           (g_map->x_width == 2 || g_map->x_width == 4) && g_map->gap_far > g_map->gap_near;
}
bool HasPause(void)  { return g_map->pause_addr != 0; }
bool HasCursor(void)
{
    return HasPause() && g_map->pause_cursor_addr && g_map->pause_count > 0 &&
           g_map->pause_count <= kMktMaxPauseItems;
}
bool AnythingMapped(void) { return HasFight() || HasPause(); }

bool InFight(void) { return HasFight() && u8(g_map->fight_addr) == g_map->fight_value; }
bool Paused(void)  { return HasPause() && u8(g_map->pause_addr) == g_map->pause_value; }

/// A health fraction, or false when the halfword is not a health value (above full): a
/// garbage read must never beep or speak as if it were a hit.
bool HealthFrac(uint32_t addr, float* out)
{
    uint16_t v = u16(addr);
    if (v > g_map->health_full) return false;
    *out = (float) v / (float) g_map->health_full;
    return true;
}

int Pct(float frac)
{
    int p = (int) (frac * 100.0f + 0.5f);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

int32_t ReadX(uint32_t addr)
{
    if (g_map->x_width == 4) return (int32_t) u32(addr);
    uint16_t v = u16(addr);
    return g_map->x_signed ? (int32_t) (int16_t) v : (int32_t) v;
}

/// The gap between the fighters along the arena, and which side the foe is on (+1 right of
/// you, -1 left, 0 on top of you). Only the x axis: a jump does not read as distance.
bool FightGap(int32_t* gap, int* side)
{
    if (!HasPos()) return false;
    int32_t x1 = ReadX(g_map->p1_x_addr), x2 = ReadX(g_map->p2_x_addr);
    int64_t d = (int64_t) x2 - (int64_t) x1;
    int64_t a = d < 0 ? -d : d;
    // Wider than four arenas is not a position pair; refuse it rather than pulse at it.
    if (a > (int64_t) g_map->gap_far * 4) return false;
    *gap = (int32_t) a;
    *side = d > 0 ? 1 : (d < 0 ? -1 : 0);
    return true;
}

const char* PauseItem(void)
{
    if (!HasCursor()) return nullptr;
    int i = u8(g_map->pause_cursor_addr);
    if (i < 0 || i >= g_map->pause_count) return nullptr;
    const char* s = g_map->pause_items[i];
    return (s && *s) ? s : nullptr;
}

// ---- health cues (the MKU / Birth by Sleep health language) ----------------------------

constexpr float kFoeHiHz = 1200.0f, kFoeLoHz = 300.0f;
constexpr float kOwnHiHz = 800.0f,  kOwnLoHz = 200.0f;
constexpr float kFoePan = 0.8f, kOwnPan = -0.8f;    // foe right, you left (owner's rule)
constexpr int   kMilestoneRearm = 5;

bool  g_pendFoeBeep = false, g_pendOwnBeep = false;
float g_pendFoeHz = 0.0f, g_pendOwnHz = 0.0f;

struct HealthTrack {
    bool primed = false;
    float prev = 0.0f;
    bool m75 = false, m50 = false, m25 = false, m10 = false;
};
HealthTrack g_own, g_foe;

float Pitch(float lo, float hi, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return lo * powf(hi / lo, frac);
}

void TrackSide(HealthTrack& t, float cur, bool foe)
{
    int pct = Pct(cur);
    if (!t.primed) {
        // The first sight in a round primes silently: a milestone already passed must not
        // speak for a round joined late (or a state loaded mid-round).
        t.primed = true; t.prev = cur;
        t.m75 = pct <= 75; t.m50 = pct <= 50; t.m25 = pct <= 25; t.m10 = pct <= 10;
        return;
    }
    if (cur < t.prev && cur > 0.0f) {          // a hit; the KO is the game's own moment
        if (foe) { g_pendFoeBeep = true; g_pendFoeHz = Pitch(kFoeLoHz, kFoeHiHz, cur); }
        else     { g_pendOwnBeep = true; g_pendOwnHz = Pitch(kOwnLoHz, kOwnHiHz, cur); }
        int crossed = 0;                        // latch every crossing, speak only the lowest
        if (pct <= 75) { if (!t.m75) crossed = 75; t.m75 = true; }
        if (pct <= 50) { if (!t.m50) crossed = 50; t.m50 = true; }
        if (pct <= 25) { if (!t.m25) crossed = 25; t.m25 = true; }
        if (pct <= 10) { if (!t.m10) crossed = 10; t.m10 = true; }
        if (crossed) {
            char line[64];
            if (crossed == 10)
                snprintf(line, sizeof(line), "%s", foe ? "Foe nearly gone." : "Your health nearly gone.");
            else
                snprintf(line, sizeof(line), foe ? "Foe health %d percent." : "Your health %d percent.", crossed);
            Say(line, "health", foe ? Priority::Low : Priority::Normal);
        }
    } else if (cur > t.prev) {
        // A new round refills the bars: each milestone re-arms once back above it by 5.
        if (pct >= 75 + kMilestoneRearm) t.m75 = false;
        if (pct >= 50 + kMilestoneRearm) t.m50 = false;
        if (pct >= 25 + kMilestoneRearm) t.m25 = false;
        if (pct >= 10 + kMilestoneRearm) t.m10 = false;
    }
    t.prev = cur;
}

// ---- side switches ---------------------------------------------------------------------

/// A new side must hold this many frames before it is said, so two fighters standing on
/// top of each other (the sign flickering through 0) do not chatter.
constexpr int kSideHoldFrames = 6;
int g_side = 0;           // the side last said or primed; 0 = not known yet
int g_sideCandidate = 0;
int g_sideFrames = 0;

void TrackSideSwitch(void)
{
    int32_t gap; int side;
    if (!FightGap(&gap, &side) || side == 0) { g_sideFrames = 0; return; }
    if (g_side == 0) { g_side = side; return; }   // the round's first sight: no line
    if (side == g_side) { g_sideCandidate = 0; g_sideFrames = 0; return; }
    if (side != g_sideCandidate) { g_sideCandidate = side; g_sideFrames = 0; }
    if (++g_sideFrames < kSideHoldFrames) return;
    g_side = side;
    g_sideCandidate = 0; g_sideFrames = 0;
    Say(side > 0 ? "Foe on your right." : "Foe on your left.", "side", Priority::Normal);
}

// ---- the pause menu --------------------------------------------------------------------

bool g_wasPaused = false;
int  g_lastCursor = -1;

void TrackPause(void)
{
    bool paused = Paused();
    int cursor = HasCursor() ? (int) u8(g_map->pause_cursor_addr) : -1;
    if (paused && !g_wasPaused) {
        const char* item = PauseItem();
        char line[96];
        if (item) snprintf(line, sizeof(line), "Paused. %s.", item);
        else snprintf(line, sizeof(line), "Paused.");
        // Not deduped against the last pause: pausing twice is two pauses.
        AdapterNoteSpoken("");
        Say(line, "pause", Priority::Normal);
    } else if (paused && cursor != g_lastCursor) {
        if (const char* item = PauseItem()) {
            char line[96];
            snprintf(line, sizeof(line), "%s.", item);
            Say(line, "pause", Priority::Normal);
        }
    }
    g_wasPaused = paused;
    g_lastCursor = cursor;
}

void ResetFight(void)
{
    g_own = HealthTrack{};
    g_foe = HealthTrack{};
    g_side = 0; g_sideCandidate = 0; g_sideFrames = 0;
}

void ResetAll(void)
{
    ResetFight();
    g_pendFoeBeep = g_pendOwnBeep = false;
    g_pendFoeHz = g_pendOwnHz = 0.0f;
    g_wasPaused = false;
    g_lastCursor = -1;
}

void OnFrame(void)
{
    if (!g_host) return;
    TrackPause();
    if (!InFight()) { ResetFight(); return; }
    if (Paused()) return;                  // nothing moves under a pause; keep the latches
    if (HasHealth()) {
        float own, foe;
        if (HealthFrac(g_map->p1_health_addr, &own)) TrackSide(g_own, own, false);
        if (HealthFrac(g_map->p2_health_addr, &foe)) TrackSide(g_foe, foe, true);
    }
    TrackSideSwitch();
}

// ---- the closeness beacon --------------------------------------------------------------

// The synth's beacon was tuned on Dissidia's scale (fastest at 4 units, slowest at 45). The
// arena gap is mapped linearly from [gap_near, gap_far] onto that band, as MKU's FightGap is.
constexpr float kSynthNear = 4.0f, kSynthFar = 45.0f;

bool CueSnapshotFn(CueSnapshot* out)
{
    if (!out) return false;
    *out = CueSnapshot{};
    if (!g_host || !InFight()) return false;
    out->battle = true;
    int32_t gap; int side;
    if (!Paused() && FightGap(&gap, &side)) {
        float t = (float) (gap - g_map->gap_near) / (float) (g_map->gap_far - g_map->gap_near);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        out->locked = true;
        out->dist = kSynthNear + t * (kSynthFar - kSynthNear);
    }
    out->foe_beep = g_pendFoeBeep;
    out->foe_beep_hz = g_pendFoeHz;
    out->foe_beep_pan = g_pendFoeBeep ? kFoePan : 0.0f;
    out->own_beep = g_pendOwnBeep;
    out->own_beep_hz = g_pendOwnHz;
    out->own_beep_pan = g_pendOwnBeep ? kOwnPan : 0.0f;
    g_pendFoeBeep = g_pendOwnBeep = false;
    g_pendFoeHz = g_pendOwnHz = 0.0f;
    return true;
}

// ---- commands --------------------------------------------------------------------------

void CmdNotMapped(void)
{
    Say("Not mapped yet in this game.", "status", Priority::High);
}

/// "close", "in the middle", "far": the beacon's own band, in words.
const char* GapWords(int32_t gap)
{
    float t = (float) (gap - g_map->gap_near) / (float) (g_map->gap_far - g_map->gap_near);
    if (t < 0.25f) return "close";
    if (t < 0.65f) return "in the middle";
    return "far";
}

bool FightLine(char* out, size_t cap)
{
    if (!InFight()) return false;
    char hp[96] = "", pos[64] = "";
    float own, foe;
    if (HasHealth() && HealthFrac(g_map->p1_health_addr, &own) &&
        HealthFrac(g_map->p2_health_addr, &foe))
        snprintf(hp, sizeof(hp), "Your health %d percent, foe %d percent.", Pct(own), Pct(foe));
    int32_t gap; int side;
    if (FightGap(&gap, &side))
        snprintf(pos, sizeof(pos), "%sFoe %s%s.", hp[0] ? " " : "",
                 side > 0 ? "on your right, " : (side < 0 ? "on your left, " : ""), GapWords(gap));
    if (!hp[0] && !pos[0]) return false;
    snprintf(out, cap, "%s%s", hp, pos);
    return true;
}

void CmdWhereAmI(void)
{
    char line[192];
    if (Paused()) {
        const char* item = PauseItem();
        if (item) snprintf(line, sizeof(line), "Paused. %s.", item);
        else snprintf(line, sizeof(line), "Paused.");
        Say(line, "whereami", Priority::High);
        return;
    }
    if (FightLine(line, sizeof(line))) { Say(line, "whereami", Priority::High); return; }
    if (!AnythingMapped()) { CmdNotMapped(); return; }
    Say(InFight() ? "In a fight." : "Not in a fight.", "whereami", Priority::High);
}

void CmdStatus(void)
{
    char line[192];
    if (FightLine(line, sizeof(line))) { Say(line, "status", Priority::High); return; }
    if (!HasHealth()) { CmdNotMapped(); return; }
    Say("Not in a fight.", "status", Priority::High);
}

void CmdMenuState(void)
{
    if (!HasPause()) { CmdNotMapped(); return; }
    if (!Paused()) { Say("Not paused.", "pause", Priority::High); return; }
    char line[96];
    const char* item = PauseItem();
    if (item) snprintf(line, sizeof(line), "Paused. %s.", item);
    else snprintf(line, sizeof(line), "Paused.");
    Say(line, "pause", Priority::High);
}

// The anchor words, in the game's own spelling as the fighter roster and menus print them.
// Matched case-blind, because a menu may store "Paused" where the screen shows PAUSED. A hit
// is a CANDIDATE text buffer, never a proven menu: it is confirmed against the screen before
// anything speaks from it.
const char* const kAnchors[] = {
    "SCORPION", "SUB-ZERO", "LIU KANG", "SHAO KAHN", "SONYA", "RAIDEN", "KITANA",
    "PAUSE", "OPTIONS", "ARCADE", "VERSUS", "CONTINUE", "QUIT", "KOMBAT", "RESUME",
    "EXIT", "FIGHT", "WINS",
};
constexpr int kAnchorCount = (int) (sizeof(kAnchors) / sizeof(kAnchors[0]));
constexpr int kMaxLoggedPerAnchor = 8;

char Upper(uint8_t c) { return (c >= 'a' && c <= 'z') ? (char) (c - 32) : (char) c; }

bool MatchAt(uint32_t addr, const char* anchor)
{
    for (int i = 0; anchor[i]; i++)
        if (Upper(u8(addr + (uint32_t) i)) != anchor[i]) return false;
    return true;
}

// Published GameShark health codes (NTSC-U). Two pressings of this disc share the serial and
// disagree, so both sets are logged. UNCONFIRMED: a candidate is a place to look, not an
// address this adapter reads for a feature.
struct Candidate { const char* label; uint32_t addr; };
const Candidate kCandidates[] = {
    { "P1 health (v1.0 code)", 0x8003219E },
    { "P2 health (v1.0 code)", 0x80032316 },
    { "P1 health (v1.1 code)", 0x80032146 },
};

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[mkt] Mortal Kombat Trilogy (SLUS00330) dump, main RAM 0x%08X-0x%08X", kRamBase, kRamEnd);
    Log(line);
    snprintf(line, sizeof(line), "[mkt] map: fight %s, health %s, position %s, pause %s, pause rows %s",
             HasFight() ? "on" : "unmapped", HasHealth() ? "on" : "unmapped",
             HasPos() ? "on" : "unmapped", HasPause() ? "on" : "unmapped",
             HasCursor() ? "on" : "unmapped");
    Log(line);
    for (const Candidate& c : kCandidates) {
        uint16_t v = u16(c.addr);
        snprintf(line, sizeof(line), "[mkt] candidate %s @ 0x%08X = 0x%04X (%u), unconfirmed",
                 c.label, c.addr, v, v);
        Log(line);
    }

    int total = 0;
    int counts[kAnchorCount] = {0};
    int logged[kAnchorCount] = {0};
    for (uint32_t addr = kRamBase; addr < kRamEnd; addr++) {
        char c = Upper(u8(addr));
        if (c < 'A' || c > 'Z') continue;          // every anchor starts with a letter
        for (int a = 0; a < kAnchorCount; a++) {
            if (c != kAnchors[a][0] || !MatchAt(addr, kAnchors[a])) continue;
            counts[a]++; total++;
            if (logged[a] < kMaxLoggedPerAnchor) {
                logged[a]++;
                snprintf(line, sizeof(line), "[mkt] \"%s\" @ 0x%08X", kAnchors[a], addr);
                Log(line);
            }
        }
    }
    for (int a = 0; a < kAnchorCount; a++) {
        if (!counts[a]) continue;
        snprintf(line, sizeof(line), "[mkt] \"%s\": %d hits", kAnchors[a], counts[a]);
        Log(line);
    }
    snprintf(line, sizeof(line), "[mkt] scan done: %d hits", total);
    Log(line);
    Say("Memory scan written to the reading log.", "status", Priority::High);
}

} // namespace

void MktSetMap(const MktMap* map)
{
    g_map = map ? map : &kShippedMap;
    ResetAll();
}

const MktMap* MktCurrentMap(void) { return g_map; }

static bool mkt_attach(const Host* host)
{
    g_host = host;
    ResetAll();
    return true;   // the disc serial already matched in the registry
}

static void mkt_on_frame(void) { OnFrame(); }

static void mkt_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();  break;
        case Command::Status:          CmdStatus();    break;
        case Command::MenuState:       CmdMenuState(); break;
        case Command::DumpState:       CmdDump();      break;
        // One foe, one fight: there is nothing to cycle, and the refusal says so honestly.
        case Command::NextAlly:
        case Command::PrevAlly:
        case Command::NextEnemy:
        case Command::PrevEnemy:
        case Command::NextUnactedAlly: CmdNotMapped(); break;
        // Menu echoes and the OSK family are ignored silently (adapter.h): the pause menu is
        // read from its own cursor as it moves, not from the pad.
        default: break;
    }
}

// Ready from the first frame: Where Am I and the dump answer honestly in any screen, and the
// cues gate themselves on their own map fields.
static bool mkt_ready(void) { return g_host != nullptr; }

static void mkt_detach(void) { ResetAll(); g_host = nullptr; }

// At oga scope (not in the anonymous namespace), or the registry's extern will not link.
extern const Adapter kMortalKombatTrilogy;
const Adapter kMortalKombatTrilogy = {
    "mkt",
    "Mortal Kombat Trilogy",
    "SLUS00330",           // the USA disc's boot serial from SYSTEM.CNF, dash dropped
    mkt_attach,
    mkt_on_frame,
    mkt_command,
    mkt_ready,
    mkt_detach,
    CueSnapshotFn,
    "Started: a memory scan for mapping; fight cues and the pause menu arrive as they are measured",
    "This reader is new. Its memory map is being measured, so for now the Where Am I key says "
    "Not mapped yet, and the Dump key writes a memory scan to the reading log for the mapping "
    "work.\n"
    "Coming as each address is confirmed: a beep on every hit with its pitch showing the health "
    "left (the foe on the right, you on the left) and the percent spoken at 75, 50, 25 and "
    "nearly gone; a closeness sound whose pace shows how far apart the fighters are; a short "
    "line when the foe crosses to your other side; and the pause menu read aloud, row by row.\n"
    "Supported version: Mortal Kombat Trilogy, USA (SLUS-00330).",
};

} // namespace oga
