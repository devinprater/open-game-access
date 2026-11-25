/*
 * mkt_adapter.cpp — Mortal Kombat Trilogy (PlayStation, USA, SLUS-00330).
 *
 * THE FIRST PLAYSTATION GAME READER. The disc's boot serial is the registry key (pokecore's
 * LoadPs1Rom looks it up before falling back to the console row in ps1_adapter.cpp), so
 * loading this disc selects this reader. Every address below was measured on the owner's own
 * copy of the disc (docs/reverse-engineering/mortal-kombat-trilogy.md says how, and what is
 * still unconfirmed). What it speaks:
 *
 *   - the menus: the main menu's four boxes, the options hub's five icons, the rows of Game
 *     Konfigure (and the difficulty's value), and the character-select cursor by fighter name;
 *   - the pause: "Paused." and "Resumed.";
 *   - the fight, with the language Mortal Kombat: Unchained (Core/mku_adapter.cpp) already has:
 *       * health: a pitch-mapped beep on every hit (foe on the right, you on the left, the
 *         pitch falling with the health), with the percent spoken only at 75, 50, 25 and
 *         nearly gone;
 *       * closeness: the beacon the battle-cue synth already plays, its pace carrying how far
 *         apart the two fighters are, silent while paused;
 *       * side: one short line when the foe ends up on your other side (a jump over), because
 *         in a 2D fighter the side decides which way every move goes;
 *       * the round: who is fighting, round starts, who won each round, and who won the match.
 *
 * A feature whose map field is 0 is off, so a field that turns out wrong on a pressing of the
 * disc is switched off by clearing it, not by editing logic.
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

// The screen id pair each screen shows (bytes at screen_a_addr and screen_b_addr). Measured on
// 60+ saved RAM images; see the doc.
const MktScreenId kMktScreenIds[kMktScreenCount] = {
    {0xFF, 0xFF},   // Other: never matched
    {2, 2},         // main menu
    {1, 4},         // options hub
    {11, 11},       // Game Konfigure (also shown on the "choose your destiny" ladder)
    {33, 5},        // character select
    {29, 29},       // a fight
    {0, 0},         // continue screen (also all-zero at boot: only trusted straight after a fight)
};

namespace {

const Host* g_host = nullptr;

// THE SHIPPED MAP. Each address was measured; the comments say how many states confirmed it.
const MktMap kShippedMap = {
    /* screen_a_addr        */ 0x800283BC,   // 29+ states, 6 screens
    /* screen_b_addr        */ 0x80028408,
    /* main_cursor_addr     */ 0x80032662,   // 7 states (0..3)
    /* options_cursor_addr  */ 0x80033638,   // 7 states (0,1,3,4; the "?" icon is skipped)
    /* konfigure_cursor_addr*/ 0x80033676,   // 7 states (0..6), unique match
    /* difficulty_addr      */ 0x800324A4,   // easy 1, medium 2, hard 3
    /* select_cursor_addr   */ 0x80033ACE,   // 30 states, 24 slots
    /* select_table_addr    */ 0x800FA8D6,   // slot -> fighter id, 14 slots checked against the HUD
    /* fight_addr           */ 0x80032478,   // 1 in every fight state, 0 on every menu state
    /* fight_value          */ 1,
    /* p1_id_addr           */ 0x80032198,   // 17 fighters read off the HUD
    /* p2_id_addr           */ 0x80032310,
    /* p1_health_addr       */ 0x8003219E,   // hits, KOs and refills, 2 fights
    /* p2_health_addr       */ 0x80032316,
    /* health_full          */ 166,
    /* p1_wins_addr         */ 0x800321A4,   // P2's confirmed over a full match; P1's by symmetry
    /* p2_wins_addr         */ 0x8003231C,
    /* p1_obj_ptr_addr      */ 0x8003218C,   // objects swap slots between fights: read via these
    /* p2_obj_ptr_addr      */ 0x80032304,
    /* x_offset             */ 0x12,
    /* gap_near             */ 20,           // 113 fight images: smallest gap 13, median 130
    /* gap_far              */ 250,          //                    largest gap 247
    /* pause_addr           */ 0x800327A4,   // 12 states
    /* pause_value          */ 1,
};
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

// ---- the game's words --------------------------------------------------------------------

const char* const kMainItems[] = { "Mortal Kombat", "2 on 2 Kombat", "8 Player Kombat", "Options" };
const char* const kOptionsItems[] = { "Game Konfigure", "Kontrol Konfigure", "Secret Konfigure",
                                      "Sound and Music", "Exit Kombat" };
const char* const kKonfigureRows[] = {
    "Difficulty", "Blood", "Timer", "Aggressor", "Combos player 1", "Combos player 2",
    "Shang Tsung's morphs", "Versus screen", "Buy in screen", "Select screen", "Match recycle", "Exit",
};
const char* const kDifficulty[] = { "Very easy", "Easy", "Medium", "Hard", "Very hard" };

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))

/// Fighter id -> name. Ids 0-20, 21, 23, 26, 28, 29 and 31 were each read off the on-screen
/// health bar during a fight. 24, 25, 27, 30 and 32 follow the game's own "<NAME> WINS" string
/// table (which runs two entries behind the ids from 23 up), and 22 is not known: it is spoken
/// as a number rather than guessed.
const char* FighterName(int id, char* buf, size_t cap)
{
    static const char* const kNames[33] = {
        "Kano", "Sonya", "Jax", "Nightwolf", "Sub-Zero", "Stryker", "Sindel", "Sektor", "Cyrax",
        "Kung Lao", "Kabal", "Sheeva", "Shang Tsung", "Liu Kang", "Smoke", "Kitana", "Jade",
        "Mileena", "Scorpion", "Reptile", "Ermac", "Sub-Zero", nullptr, "Noob Saibot", "Motaro",
        "Shao Kahn", "Rain", "Chameleon", "Rayden", "Baraka", "Kintaro", "Johnny Cage", "Goro",
    };
    if (id >= 0 && id < 33 && kNames[id]) return kNames[id];
    snprintf(buf, cap, "Fighter %d", id);
    return buf;
}

// ---- what the map has ------------------------------------------------------------------

bool HasScreens(void) { return g_map->screen_a_addr && g_map->screen_b_addr; }
bool HasFight(void)  { return g_map->fight_addr != 0; }
bool HasHealth(void)
{
    return HasFight() && g_map->p1_health_addr && g_map->p2_health_addr && g_map->health_full;
}
bool HasPos(void)
{
    return HasFight() && g_map->p1_obj_ptr_addr && g_map->p2_obj_ptr_addr && g_map->gap_far > g_map->gap_near;
}
bool HasWins(void)   { return HasFight() && g_map->p1_wins_addr && g_map->p2_wins_addr; }
bool HasIds(void)    { return HasFight() && g_map->p1_id_addr && g_map->p2_id_addr; }
bool HasPause(void)  { return g_map->pause_addr != 0; }
bool AnythingMapped(void) { return HasFight() || HasPause() || HasScreens(); }

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

/// A fighter's x position, read through the pointer to its game object. False when the pointer
/// is not into main RAM (between fights the slot is empty).
bool ReadX(uint32_t ptr_addr, int32_t* out)
{
    uint32_t p = u32(ptr_addr);
    if (p < kRamBase || p + g_map->x_offset + 2 > kRamEnd) return false;
    *out = (int32_t) (int16_t) u16(p + g_map->x_offset);
    return true;
}

/// The gap between the fighters along the arena, and which side the foe is on (+1 right of
/// you, -1 left, 0 on top of you). Only the x axis: a jump does not read as distance.
bool FightGap(int32_t* gap, int* side)
{
    if (!HasPos()) return false;
    int32_t x1, x2;
    if (!ReadX(g_map->p1_obj_ptr_addr, &x1) || !ReadX(g_map->p2_obj_ptr_addr, &x2)) return false;
    int64_t d = (int64_t) x2 - (int64_t) x1;
    int64_t a = d < 0 ? -d : d;
    // Wider than four arenas is not a position pair; refuse it rather than pulse at it.
    if (a > (int64_t) g_map->gap_far * 4) return false;
    *gap = (int32_t) a;
    *side = d > 0 ? 1 : (d < 0 ? -1 : 0);
    return true;
}

// ---- which screen ----------------------------------------------------------------------

int ScreenNow(void)
{
    if (!HasScreens()) return kMktScreenOther;
    uint8_t a = u8(g_map->screen_a_addr), b = u8(g_map->screen_b_addr);
    for (int s = kMktScreenMain; s < kMktScreenCount; s++)
        if (a == kMktScreenIds[s].a && b == kMktScreenIds[s].b) return s;
    return kMktScreenOther;
}

/// A screen must hold this many frames before it counts, so the flicker of a transition (and
/// the all-zero frames of a load) never speaks.
constexpr int kScreenHoldFrames = 15;
int g_screen = kMktScreenOther;        // the settled screen
int g_prevScreen = kMktScreenOther;    // the settled screen before it
int g_candidate = kMktScreenOther;
int g_candidateFrames = 0;
int g_menuCursor = -1;                 // the cursor last said on the settled screen
int g_difficulty = -1;
bool g_konfigureReal = false;          // Game Konfigure was entered from the options hub

/// What the cursor says on screen `s`, or nullptr when it cannot say.
const char* MenuWords(int s, char* buf, size_t cap, int* cursor_out)
{
    int c = -1;
    const char* w = nullptr;
    switch (s) {
        case kMktScreenMain:
            if (g_map->main_cursor_addr) { c = u8(g_map->main_cursor_addr); if (c < COUNT(kMainItems)) w = kMainItems[c]; }
            break;
        case kMktScreenOptions:
            if (g_map->options_cursor_addr) { c = u8(g_map->options_cursor_addr); if (c < COUNT(kOptionsItems)) w = kOptionsItems[c]; }
            break;
        case kMktScreenKonfigure:
            if (g_map->konfigure_cursor_addr && g_konfigureReal) {
                c = u8(g_map->konfigure_cursor_addr);
                if (c < COUNT(kKonfigureRows)) {
                    w = kKonfigureRows[c];
                    if (c == 0 && g_map->difficulty_addr) {
                        int d = u8(g_map->difficulty_addr);
                        if (d < COUNT(kDifficulty)) { snprintf(buf, cap, "%s, %s", w, kDifficulty[d]); w = buf; }
                    }
                }
            }
            break;
        case kMktScreenSelect:
            if (g_map->select_cursor_addr && g_map->select_table_addr) {
                c = u8(g_map->select_cursor_addr);
                if (c < 32) {
                    char nb[24];
                    snprintf(buf, cap, "%s", FighterName(u16(g_map->select_table_addr + 2u * (uint32_t) c), nb, sizeof(nb)));
                    w = buf;
                }
            }
            break;
        default: break;
    }
    *cursor_out = c;
    return w;
}

const char* ScreenTitle(int s)
{
    switch (s) {
        case kMktScreenMain:      return "Main menu";
        case kMktScreenOptions:   return "Options";
        case kMktScreenKonfigure: return "Game Konfigure";
        case kMktScreenSelect:    return "Choose your fighter";
        default: return nullptr;
    }
}

void TrackScreen(void)
{
    int now = ScreenNow();
    if (now != g_candidate) { g_candidate = now; g_candidateFrames = 0; }
    else if (g_candidateFrames < kScreenHoldFrames + 1) g_candidateFrames++;

    if (g_candidate != g_screen && g_candidateFrames >= kScreenHoldFrames) {
        g_prevScreen = g_screen;
        g_screen = g_candidate;
        g_menuCursor = -1;
        g_difficulty = -1;
        // The "choose your destiny" ladder shows the Konfigure id too; only a trip through the
        // options hub is the real settings list.
        g_konfigureReal = (g_screen == kMktScreenKonfigure && g_prevScreen == kMktScreenOptions);
        if (g_screen == kMktScreenContinue && g_prevScreen == kMktScreenFight)
            Say("Continue? Press start.", "screen", Priority::Normal);
        if (const char* title = (g_screen == kMktScreenKonfigure && !g_konfigureReal) ? nullptr : ScreenTitle(g_screen)) {
            char buf[64]; int c;
            const char* w = MenuWords(g_screen, buf, sizeof(buf), &c);
            char line[128];
            if (w) snprintf(line, sizeof(line), "%s. %s.", title, w);
            else   snprintf(line, sizeof(line), "%s.", title);
            AdapterNoteSpoken("");
            Say(line, "menu", Priority::Normal);
            g_menuCursor = c;
            if (g_screen == kMktScreenKonfigure && g_map->difficulty_addr) g_difficulty = u8(g_map->difficulty_addr);
        }
        return;
    }
    if (g_candidate != g_screen) return;

    char buf[64]; int c;
    const char* w = MenuWords(g_screen, buf, sizeof(buf), &c);
    if (!w) return;
    int diff = (g_screen == kMktScreenKonfigure && g_map->difficulty_addr) ? (int) u8(g_map->difficulty_addr) : -1;
    if (c != g_menuCursor || (c == 0 && diff != g_difficulty)) {
        char line[96];
        snprintf(line, sizeof(line), "%s.", w);
        Say(line, "menu", Priority::Normal);
    }
    g_menuCursor = c;
    g_difficulty = diff;
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

// ---- the round: who, which round, who won ------------------------------------------------

constexpr int kRoundsToWin = 2;            // best of three
constexpr int kFightHoldFrames = 20;       // the fight flag must hold this long before the line
int g_fightFrames = 0;
bool g_fightAnnounced = false;
bool g_winsPrimed = false;
int  g_p1Wins = 0, g_p2Wins = 0;
bool g_koSeen = false;                     // a bar hit empty: the next refill is a new round

int Wins(uint32_t addr)
{
    uint32_t v = u32(addr);
    return v > 9 ? 9 : (int) v;            // a garbage read must not become "round 1000"
}

void NamesLine(char* out, size_t cap)
{
    char a[24], b[24];
    snprintf(out, cap, "%s versus %s", FighterName(u8(g_map->p1_id_addr), a, sizeof(a)),
             FighterName(u8(g_map->p2_id_addr), b, sizeof(b)));
}

void AnnounceRound(void)
{
    char names[96] = "", line[160];
    if (HasIds()) NamesLine(names, sizeof(names));
    int round = HasWins() ? g_p1Wins + g_p2Wins + 1 : 0;
    if (round > 0 && names[0]) snprintf(line, sizeof(line), "Round %d. %s.", round, names);
    else if (round > 0)        snprintf(line, sizeof(line), "Round %d.", round);
    else if (names[0])         snprintf(line, sizeof(line), "%s.", names);
    else return;
    AdapterNoteSpoken("");
    Say(line, "round", Priority::Normal);
}

void TrackRound(void)
{
    if (g_fightFrames < kFightHoldFrames + 1) g_fightFrames++;
    if (g_fightFrames < kFightHoldFrames) return;

    if (HasWins()) {
        int w1 = Wins(g_map->p1_wins_addr), w2 = Wins(g_map->p2_wins_addr);
        if (!g_winsPrimed) { g_winsPrimed = true; g_p1Wins = w1; g_p2Wins = w2; }
        else {
            char line[96];
            if (w1 > g_p1Wins) {
                if (w1 >= kRoundsToWin) snprintf(line, sizeof(line), "You win the match.");
                else snprintf(line, sizeof(line), "You win the round. %d to %d.", w1, w2);
                Say(line, "round", Priority::High);
            }
            if (w2 > g_p2Wins) {
                char nb[24];
                if (w2 >= kRoundsToWin && HasIds())
                    snprintf(line, sizeof(line), "%s wins the match.", FighterName(u8(g_map->p2_id_addr), nb, sizeof(nb)));
                else if (w2 >= kRoundsToWin) snprintf(line, sizeof(line), "Foe wins the match.");
                else snprintf(line, sizeof(line), "Foe wins the round. %d to %d.", w1, w2);
                Say(line, "round", Priority::High);
            }
            g_p1Wins = w1; g_p2Wins = w2;
        }
    }

    if (!g_fightAnnounced) {
        g_fightAnnounced = true;
        AnnounceRound();
    }
}

// ---- the pause --------------------------------------------------------------------------

bool g_wasPaused = false;

void TrackPause(void)
{
    bool paused = Paused() && InFight();
    if (paused && !g_wasPaused) {
        AdapterNoteSpoken("");              // pausing twice is two pauses, not a repeat
        Say("Paused.", "pause", Priority::Normal);
    } else if (!paused && g_wasPaused && InFight()) {
        AdapterNoteSpoken("");
        Say("Resumed.", "pause", Priority::Normal);
    }
    g_wasPaused = paused;
}

void ResetFight(void)
{
    g_own = HealthTrack{};
    g_foe = HealthTrack{};
    g_side = 0; g_sideCandidate = 0; g_sideFrames = 0;
    g_fightFrames = 0; g_fightAnnounced = false;
    g_winsPrimed = false; g_p1Wins = g_p2Wins = 0; g_koSeen = false;
}

void ResetAll(void)
{
    ResetFight();
    g_pendFoeBeep = g_pendOwnBeep = false;
    g_pendFoeHz = g_pendOwnHz = 0.0f;
    g_wasPaused = false;
    g_screen = g_prevScreen = g_candidate = kMktScreenOther;
    g_candidateFrames = 0; g_menuCursor = -1; g_difficulty = -1; g_konfigureReal = false;
}

void OnFrame(void)
{
    if (!g_host) return;
    TrackScreen();
    TrackPause();
    if (!InFight()) { ResetFight(); return; }
    if (Paused()) return;                  // nothing moves under a pause; keep the latches
    TrackRound();
    if (HasHealth()) {
        float own, foe;
        bool haveOwn = HealthFrac(g_map->p1_health_addr, &own);
        bool haveFoe = HealthFrac(g_map->p2_health_addr, &foe);
        // A refill after a KO is a new round: say which one (the wins have already moved).
        if (haveOwn && haveFoe && g_fightAnnounced) {
            if (own <= 0.0f || foe <= 0.0f) g_koSeen = true;
            else if (g_koSeen && own >= 0.99f && foe >= 0.99f) {
                g_koSeen = false;
                bool matchOver = g_p1Wins >= kRoundsToWin || g_p2Wins >= kRoundsToWin;
                if (!matchOver) AnnounceRound();
            }
        }
        if (haveOwn) TrackSide(g_own, own, false);
        if (haveFoe) TrackSide(g_foe, foe, true);
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
    char hp[96] = "", pos[64] = "", rd[24] = "";
    float own, foe;
    if (HasWins()) snprintf(rd, sizeof(rd), "Round %d. ", Wins(g_map->p1_wins_addr) + Wins(g_map->p2_wins_addr) + 1);
    if (HasHealth() && HealthFrac(g_map->p1_health_addr, &own) &&
        HealthFrac(g_map->p2_health_addr, &foe))
        snprintf(hp, sizeof(hp), "Your health %d percent, foe %d percent.", Pct(own), Pct(foe));
    int32_t gap; int side;
    if (FightGap(&gap, &side))
        snprintf(pos, sizeof(pos), "%sFoe %s%s.", hp[0] ? " " : "",
                 side > 0 ? "on your right, " : (side < 0 ? "on your left, " : ""), GapWords(gap));
    if (!hp[0] && !pos[0]) return false;
    snprintf(out, cap, "%s%s%s", rd, hp, pos);
    return true;
}

/// Where the player is, as a sentence, when not in a fight.
bool MenuLine(char* out, size_t cap)
{
    int s = ScreenNow();
    const char* title = ScreenTitle(s);
    if (!title) return false;
    // The settled screen decides Konfigure: its rows are only trusted after the options hub.
    if (s == kMktScreenKonfigure && g_screen != kMktScreenKonfigure) return false;
    char buf[64]; int c;
    const char* w = MenuWords(s, buf, sizeof(buf), &c);
    if (w) snprintf(out, cap, "%s. %s.", title, w);
    else   snprintf(out, cap, "%s.", title);
    return true;
}

void CmdWhereAmI(void)
{
    char line[224];
    if (Paused() && InFight()) { Say("Paused.", "whereami", Priority::High); return; }
    if (FightLine(line, sizeof(line))) { Say(line, "whereami", Priority::High); return; }
    if (MenuLine(line, sizeof(line))) { Say(line, "whereami", Priority::High); return; }
    if (!AnythingMapped()) { CmdNotMapped(); return; }
    Say(InFight() ? "In a fight." : "Not in a fight or a menu I know.", "whereami", Priority::High);
}

void CmdStatus(void)
{
    char line[224];
    if (FightLine(line, sizeof(line))) { Say(line, "status", Priority::High); return; }
    if (MenuLine(line, sizeof(line))) { Say(line, "status", Priority::High); return; }
    if (!HasHealth()) { CmdNotMapped(); return; }
    Say("Not in a fight.", "status", Priority::High);
}

void CmdMenuState(void)
{
    char line[160];
    if (InFight()) { Say(Paused() ? "Paused." : "Not paused.", "pause", Priority::High); return; }
    if (MenuLine(line, sizeof(line))) { Say(line, "menu", Priority::High); return; }
    if (!AnythingMapped()) { CmdNotMapped(); return; }
    Say("No menu I know is open.", "menu", Priority::High);
}

// The anchor words, in the game's own spelling as the fighter roster and menus print them.
// Matched case-blind. A hit is a CANDIDATE text buffer for further mapping, never a proven menu.
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

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[mkt] Mortal Kombat Trilogy (SLUS00330) dump, main RAM 0x%08X-0x%08X", kRamBase, kRamEnd);
    Log(line);
    snprintf(line, sizeof(line), "[mkt] map: screens %s, fight %s, health %s, position %s, wins %s, pause %s",
             HasScreens() ? "on" : "unmapped", HasFight() ? "on" : "unmapped",
             HasHealth() ? "on" : "unmapped", HasPos() ? "on" : "unmapped",
             HasWins() ? "on" : "unmapped", HasPause() ? "on" : "unmapped");
    Log(line);
    if (HasScreens()) {
        snprintf(line, sizeof(line), "[mkt] screen id %u/%u (settled screen %d), fight flag %u, pause flag %u",
                 u8(g_map->screen_a_addr), u8(g_map->screen_b_addr), g_screen,
                 HasFight() ? u8(g_map->fight_addr) : 0u, HasPause() ? u8(g_map->pause_addr) : 0u);
        Log(line);
    }
    if (HasHealth()) {
        snprintf(line, sizeof(line), "[mkt] health: you %u, foe %u (full %u)",
                 u16(g_map->p1_health_addr), u16(g_map->p2_health_addr), g_map->health_full);
        Log(line);
    }
    if (HasPos()) {
        int32_t x1 = 0, x2 = 0;
        bool ok1 = ReadX(g_map->p1_obj_ptr_addr, &x1), ok2 = ReadX(g_map->p2_obj_ptr_addr, &x2);
        snprintf(line, sizeof(line), "[mkt] x: you %s%d, foe %s%d", ok1 ? "" : "(no object) ", x1,
                 ok2 ? "" : "(no object) ", x2);
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
        // Menu echoes and the OSK family are ignored silently (adapter.h): the menus are read
        // from their own cursors as they move, not from the pad.
        default: break;
    }
}

// Ready from the first frame: Where Am I and the dump answer honestly on any screen, and the
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
    "Reads the menus, character select, the pause, and the fight: health beeps, closeness, rounds",
    "Menus: the main menu's four boxes, the options icons, the rows of Game Konfigure with the "
    "difficulty, and the fighter under the cursor on character select, each said as the "
    "highlight moves.\n"
    "Fights: the round and who is fighting; a beep on every hit with its pitch showing the "
    "health left (the foe on the right, you on the left) and the percent spoken at 75, 50, 25 "
    "and nearly gone; a closeness sound whose pace shows how far apart the fighters are; a "
    "short line when the foe crosses to your other side; who won each round and the match; "
    "Paused and Resumed.\n"
    "Not yet read: the round timer, the pause menu's Continue and Quit rows, the other "
    "Konfigure settings' values, and the second player's cursor. Where Am I says the screen "
    "and, in a fight, the round, both health bars and where the foe is. Dump writes a memory "
    "scan to the reading log.\n"
    "Supported version: Mortal Kombat Trilogy, USA (SLUS-00330).",
};

} // namespace oga
