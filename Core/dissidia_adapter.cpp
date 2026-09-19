/*
 * dissidia_adapter.cpp — Dissidia Final Fantasy (PSP, ULUS10437) adapter skeleton.
 *
 * STATUS: LIVE. The pause-menu cursor is found, named, and tracked (s101).
 *
 * VERIFIED MAP (all confirmed against live RAM + decompile):
 *   TEXT_POOL   0x09D16A68  UTF-16LE menu/UI strings, one flag byte per entry
 *                             (0x00 normal; 0xFF conditional lines). 179 strings,
 *                             contiguous 0x09D16A68..0x09D1943C.
 *   MGR_SLOT    0x08B9B770  holds heap pointer -> manager object (e.g. 0x08C08EB0).
 *   Manager +0x18           render block COUNT (derived row count; responds to
 *                             delivered up/down but is NOT the index).
 *   PAUSE WIDGET  W = [[0x08B98940]] + 0x234  (battle_ui_root holder + 0x234;
 *                             Codex ANSWER3; TRACKED LIVE incl. wrap both ways).
 *   Index at W+0x3C, signed, -1 = none/invalid (pause closed reads -1 / 0).
 *   Count at W+0x240, 1..7 (pause menu: 4).
 *   Element = W+0x64+idx*0x44 (stride 0x44, hard cap 7); tag at element+0x18.
 *     Pause tags observed live: 0x0C Return to Game, 0x2E Quicksave,
 *     0x30 Quit Level Progression, 0x39 Help Manual.
 *   Reader: FUN_00250538(obj) validates 0 <= idx < count else returns -1.
 *
 * DP LOGIC (s104, write-watchpoint proven):
 *   Spend/adjust: FUN_001b6084(obj, delta) does s16[[obj+0x38]+6] += delta with
 *     clamps (floor -10, cap 0x14) then calls FUN_001ca124 to refresh the cache.
 *     Called from the battle-UI dispatcher FUN_001b97c8 (3 sites) + FUN_001bffa8.
 *   Cache refresh: FUN_001d5438 (HUD draw tick) rewrites [U+0xD78] on every input
 *     tick -- presentation only, never game logic.
 *
 * INPUT CHAIN (code-verified end to end, sections 79-88):
 *   sceCtrl -> pad object +0x00 (live button word) -> FUN_000f7138 reads raw
 *   -> FUN_000f70c4 maps raw bits to LOGICAL action bits
 *   -> FUN_000f6694 writes EVENT STATE into pad+0xC0
 *        (+0x00 current, +0x08 just-pressed one-frame, +0x10 just-released,
 *         +0x30 repeat timer)
 *   -> FUN_000f67c0 bit-test predicate -> FUN_000f71c8 guarded query wrapper
 *        (zero callers; published in the API vtable at RAM 0x08BA46BC..)
 *   -> menu consumer FUN_00267cb8 calls the API through a function pointer
 *        loaded as *(code **)(*(int *)(obj + 0x34) + 0x14), then asks
 *        FUN_00250538(&DAT...+param_1) "what is selected".
 */

#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

namespace oga {
namespace dissidia {

// ---- validated addresses ----------------------------------------------------
constexpr uint32_t TEXT_POOL  = 0x09D16A68u;
constexpr uint32_t MGR_SLOT   = 0x08B9B770u;
constexpr uint32_t MGR_OFF_RENDER = 0x18u;   // derived row count (NOT the index)
constexpr uint32_t IDX_OFF    = 0x3Cu;       // index at W+0x3C (= P+0x1300)
constexpr uint32_t CNT_OFF    = 0x240u;      // count at W+0x240 (= P+0x1504)
constexpr uint32_t ELT_BASE   = 0x60u;       // elements at W+0x60+4+idx*0x44
constexpr uint32_t ELT_STRIDE = 0x44u;
constexpr uint32_t ELT_CAP    = 7u;
constexpr uint32_t ELT_VAL    = 0x18u;       // selected value at element+0x18
constexpr int32_t  NO_SELECTION = -1;
constexpr uint32_t RAM_LO = 0x08800000u;
constexpr uint32_t RAM_HI = 0x0A000000u;

// ---- Codex ANSWER3 objects (doc s101; VALIDATED LIVE on the board pause menu) ----
// battle_ui_root holder (vaddr 0x00394940): pause widget W = [holder] + 0x234.
// index [W+0x3C], count [W+0x240]; pause closed reads idx -1 / count 0.
constexpr uint32_t BATTLE_ROOT_HOLDER = 0x08B98940u;
constexpr uint32_t PAUSE_WIDGET_OFF   = 0x234u;

// ---- Board chains (ANSWER5, s105; VALIDATED LIVE: cursor tracks, DP spends/refunds) ----
// M = [0x08B98940]; P = [M+0x118]; B = [P+0x04]; T = [B+0x0C]; D = [B+0x10].
// highlight (x,y) = u8[D+0x194], u8[D+0x195]; origin = u8[[D+0x38]+0x02], +0x03.
// Progress: G = [0x08B99338]; chapter = u8[M+0x120]; C = G+0x1AE80+chapter*0xF74;
//   slot = u8[C+2]; R = C+slot*0x314+8; DP = s16[R+6].
constexpr uint32_t PROGRESS_HOLDER = 0x08B99338u;
constexpr uint32_t OFF_P = 0x118u, OFF_B = 0x04u, OFF_T = 0x0Cu, OFF_D = 0x10u;
constexpr uint32_t OFF_HX = 0x194u, OFF_HY = 0x195u, OFF_DORG = 0x38u;
constexpr uint32_t OFF_CH = 0x120u, CH_BASE = 0x1AE80u, CH_STRIDE = 0xF74u;
constexpr uint32_t SLOT_STRIDE = 0x314u, OFF_DP = 0x06u;

// ---- Dense grid (ANSWER7, s108; VALIDATED LIVE: 8x5 map, west byte 0x00) ----
// G = [B+8]; A = [G+0]; cells = [G+4]; w = u8[A], h = u8[A+1];
// cell(x,y) = u8[cells + y*w + x]; legal = (f&2) || (f && !(f&0x2C)).
// Marker array: N = [T+4], 32 slots stride 0x10, key u8[+0], x/y s8[+2/+3].
constexpr uint32_t OFF_G = 0x08u;
constexpr uint32_t MARK_SLOTS = 32u, MARK_STRIDE = 0x10u;

static const Host* g_host = nullptr;
// W = the CURRENT menu's list widget (index at W+0x3C, count at W+0x240).
// 0 = not tracked yet. Heap addresses shift per boot, so this is re-discovered
// from static holders on every use -- never a constant, never cached across menus.
static uint32_t g_widgetRoot = 0;
// True when the harness explicitly pinned the widget (it owns the validity claim);
// false when the adapter discovered it (may go stale when the menu closes).
static bool g_widgetPinned = false;

static uint8_t u8(uint32_t a) { return g_host ? g_host->read8(g_host->ctx, a) : 0; }
static uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

static void Say(const char* s, bool interrupt = true)
{
    if (g_host && g_host->speak) g_host->speak(g_host->ctx, s, interrupt);
}

static bool InRam(uint32_t a) { return a >= RAM_LO && a < RAM_HI; }

/// The harness may pin a known-live widget directly. Prefer discovery: heap
/// addresses shift per boot, so a pinned widget is only valid for its own menu.
void SetWidgetRoot(uint32_t w) { g_widgetRoot = w; g_widgetPinned = (w != 0); }
uint32_t WidgetRoot(void) { return g_widgetRoot; }

/// Discover the pause-menu widget from its static holder (Codex ANSWER3, s101).
/// Returns 0 unless the widget looks live (count 1..7, index in range): a closed
/// pause menu reads count 0, and we refuse that rather than track a dead object.
uint32_t DiscoverPauseWidget(void)
{
    if (!g_host) return 0;
    uint32_t root = u32(BATTLE_ROOT_HOLDER);
    if (!InRam(root) || (root & 3)) return 0;
    uint32_t w = root + PAUSE_WIDGET_OFF;
    uint32_t cnt = u32(w + CNT_OFF);
    if (cnt == 0 || cnt > ELT_CAP) return 0;
    int32_t idx = (int32_t) u32(w + IDX_OFF);
    if (idx < 0 || (uint32_t) idx >= cnt) return 0;
    return w;
}

static bool ManagerOk(void)
{
    uint32_t m = u32(MGR_SLOT);
    return InRam(m) && (m & 3) == 0;
}

/// Read the selection index with the same validation FUN_00250538 applies:
/// 0 <= idx < count else -1. Returns -2 when no widget root is tracked.
static int SelectionIndex(uint32_t* countOut)
{
    if (countOut) *countOut = 0;
    if (!g_widgetRoot || !InRam(g_widgetRoot)) return -2;
    uint32_t w = g_widgetRoot;   // g_widgetRoot IS the widget (index W+0x3C)
    int32_t idx = (int32_t) u32(w + IDX_OFF);
    uint32_t cnt = u32(w + CNT_OFF);
    if (countOut) *countOut = cnt;
    if (cnt == 0 || cnt > ELT_CAP) return NO_SELECTION;
    if (idx < 0 || (uint32_t) idx >= cnt) return NO_SELECTION;
    return (int) idx;
}

/// Walk the board chain M -> P -> B -> D. Returns D (dispatcher) or 0.
/// Every hop is validated (in RAM, aligned); 0 means "board not available".
static uint32_t BoardDispatcher(void)
{
    if (!g_host) return 0;
    uint32_t m = u32(BATTLE_ROOT_HOLDER);
    if (!InRam(m) || (m & 3)) return 0;
    uint32_t p = u32(m + OFF_P);
    if (!InRam(p) || (p & 3)) return 0;
    uint32_t b = u32(p + OFF_B);
    if (!InRam(b) || (b & 3)) return 0;
    uint32_t d = u32(b + OFF_D);
    if (!InRam(d) || (d & 3)) return 0;
    return d;
}

/// Walk M -> P -> B, returning B (board bundle) or 0. Shared by grid + markers.
static uint32_t BoardBundle(void)
{
    if (!g_host) return 0;
    uint32_t m = u32(BATTLE_ROOT_HOLDER);
    if (!InRam(m) || (m & 3)) return 0;
    uint32_t p = u32(m + OFF_P);
    if (!InRam(p) || (p & 3)) return 0;
    uint32_t b = u32(p + OFF_B);
    if (!InRam(b) || (b & 3)) return 0;
    return b;
}

struct Grid {
    uint32_t cells;
    uint32_t w, h;
    bool ok;
};

/// Read the dense grid header. Validates dimensions (boards are small).
static Grid BoardGrid(uint32_t b)
{
    Grid g = {0, 0, 0, false};
    if (!b) return g;
    uint32_t gg = u32(b + OFF_G);
    if (!InRam(gg) || (gg & 3)) return g;
    uint32_t a = u32(gg + 0);
    uint32_t cells = u32(gg + 4);
    if (!InRam(a) || !InRam(cells)) return g;
    uint32_t w = u8(a), h = u8(a + 1);
    if (w == 0 || w > 60 || h == 0 || h > 60) return g;
    if (!InRam(cells + w * h - 1)) return g;
    g.cells = cells; g.w = w; g.h = h; g.ok = true;
    return g;
}

/// The game's own rule (FUN_001b72e4): in-bounds plus flag test.
static bool GridLegal(const Grid& g, int x, int y)
{
    if (!g.ok) return false;
    if (x < 0 || y < 0 || (uint32_t) x >= g.w || (uint32_t) y >= g.h) return false;
    uint32_t f = u8(g.cells + (uint32_t) y * g.w + (uint32_t) x);
    return (f & 0x02) != 0 || (f != 0 && (f & 0x2Cu) == 0);
}

static void CmdDirections(void)
{
    if (!ManagerOk()) { Say("Game not ready yet."); return; }
    uint32_t d = BoardDispatcher();
    if (!d) { Say("Board not available."); return; }
    Grid g = BoardGrid(BoardBundle());
    if (!g.ok) { Say("Board map unreadable."); return; }
    int hx = u8(d + OFF_HX), hy = u8(d + OFF_HY);
    if (hx > 60 || hy > 60) { Say("Cursor unreadable."); return; }
    static const char* names[4] = {"west", "east", "north", "south"};
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dy[4] = {0, 0, -1, 1};
    char open[64] = {0}, shut[64] = {0};
    for (int i = 0; i < 4; i++) {
        bool ok = GridLegal(g, hx + dx[i], hy + dy[i]);
        snprintf(open + strlen(open), sizeof(open) - strlen(open), "%s%s",
                 (ok && open[0]) ? ", " : "", ok ? names[i] : "");
        snprintf(shut + strlen(shut), sizeof(shut) - strlen(shut), "%s%s",
                 (!ok && shut[0]) ? ", " : "", !ok ? names[i] : "");
    }
    char line[160];
    if (!open[0]) {
        snprintf(line, sizeof(line), "No open direction. Blocked: %s.", shut);
    } else if (!shut[0]) {
        snprintf(line, sizeof(line), "Open: %s.", open);
    } else {
        snprintf(line, sizeof(line), "Open: %s. Blocked: %s.", open, shut);
    }
    Say(line);
    // Caveat (s108): the grid recipe approves cells the marker/story stage may
    // still gate (observed once at (6,2)). Grid-legal is necessary, not sufficient.
}

static void CmdMarkers(void)
{
    if (!ManagerOk()) { Say("Game not ready yet."); return; }
    uint32_t b = BoardBundle();
    if (!b) { Say("Board not available."); return; }
    uint32_t t = u32(b + OFF_T);
    if (!InRam(t) || (t & 3)) { Say("Board not available."); return; }
    uint32_t n = u32(t + 4);
    if (!InRam(n) || (n & 3)) { Say("Board not available."); return; }
    uint32_t d = BoardDispatcher();
    int hx = -1, hy = -1;
    if (d) { hx = u8(d + OFF_HX); hy = u8(d + OFF_HY); }
    char line[192];
    int found = 0;
    for (uint32_t i = 0; i < MARK_SLOTS; i++) {
        uint32_t e = n + i * MARK_STRIDE;
        if (!InRam(e + MARK_STRIDE - 1)) break;
        int mx = (int) (int8_t) u8(e + 2), my = (int) (int8_t) u8(e + 3);
        uint32_t fl = u8(e + 0x0C), key = u8(e + 0);
        bool active = !(mx == 0 && my == 0 && fl == 0 && key == 0);
        if (!active) continue;
        found++;
        if (found == 1) {
            if (hx >= 0 && mx == hx && my == hy) {
                snprintf(line, sizeof(line),
                         "Special tile here at %d, %d, type %u.", mx, my, key);
            } else if (hx >= 0) {
                int dx = mx - hx, dy = my - hy;
                char dir[32];
                snprintf(dir, sizeof(dir), "%s%s",
                         dy < 0 ? "north" : (dy > 0 ? "south" : ""),
                         dx < 0 ? "west" : (dx > 0 ? "east" : ""));
                int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                snprintf(line, sizeof(line),
                         "Special tile %s %d away at %d, %d, type %u.",
                         dir[0] ? dir : "here", dist, mx, my, key);
            } else {
                snprintf(line, sizeof(line),
                         "Special tile at %d, %d, type %u.", mx, my, key);
            }
            Say(line, false);
        }
    }
    if (!found) {
        Say("No special tiles recorded.");
        return;
    }
    if (found > 1) {
        snprintf(line, sizeof(line), "%d special tiles. First announced.", found);
        Say(line, false);
    }
    // NOTE: "type" here is the marker KEY (catalog key u8[+0]); the catalog type
    // s16[O+4] needs the C-chain (ANSWER7) and is not yet spoken. Never a guess.
}

/// Authoritative DP (s16 progress record). Returns -1000 when unreadable.
static int BoardDP(void)
{
    if (!g_host) return -1000;
    uint32_t m = u32(BATTLE_ROOT_HOLDER);
    uint32_t g = u32(PROGRESS_HOLDER);
    if (!InRam(m) || !InRam(g)) return -1000;
    uint32_t ch = u8(m + OFF_CH);
    if (ch > 40) return -1000;
    uint32_t c = g + CH_BASE + ch * CH_STRIDE;
    if (!InRam(c)) return -1000;
    uint32_t slot = u8(c + 2);
    if (slot > 4) return -1000;
    uint32_t r = c + slot * SLOT_STRIDE + 8;
    if (!InRam(r + OFF_DP + 1)) return -1000;
    return (int) (int16_t) ((u8(r + OFF_DP + 1) << 8) | u8(r + OFF_DP));
}

static void CmdWhereAmI(void)
{
    if (!ManagerOk()) { Say("Game not ready yet."); return; }
    // Board first when no menu widget is live: pause widget validates itself
    // (count 1..7), so a live pause menu still wins via the pinned root below.
    if (!g_widgetRoot) {
        g_widgetRoot = DiscoverPauseWidget();
        g_widgetPinned = false;
    }
    if (g_widgetRoot) {
        uint32_t cnt = 0;
        int idx = SelectionIndex(&cnt);
        if (idx >= 0) {
            char line[96];
            snprintf(line, sizeof(line), "Row %d of %u.", idx + 1, cnt);
            Say(line);
            return;
        }
        // Explicitly pinned widgets speak for their menu even when empty.
        if (g_widgetPinned) { Say("No selection."); return; }
        // Auto-discovered widgets go stale when the menu closes: clear and
        // fall through to the board instead of announcing a dead menu.
        g_widgetRoot = 0;
    }
    uint32_t d = BoardDispatcher();
    if (!d) { Say("Menu not tracked yet."); return; }
    int dp = BoardDP();
    uint32_t hx = u8(d + OFF_HX), hy = u8(d + OFF_HY);
    uint32_t org = u32(d + OFF_DORG);
    uint32_t ox = 999, oy = 999;
    if (InRam(org)) { ox = u8(org + 2); oy = u8(org + 3); }
    char line[128];
    if (hx > 60 || hy > 60 || ox > 60 || oy > 60) {
        Say("Board state unreadable.");
        return;
    }
    if (dp == -1000) {
        snprintf(line, sizeof(line), "Cursor %u, %u. Origin %u, %u.", hx, hy, ox, oy);
    } else if (hx == ox && hy == oy) {
        snprintf(line, sizeof(line), "DP %d. Cursor home at %u, %u.", dp, hx, hy);
    } else {
        snprintf(line, sizeof(line), "DP %d. Cursor %u, %u. Origin %u, %u.",
                 dp, hx, hy, ox, oy);
    }
    Say(line);
    // TODO: item names (tag->string mapping open), available directions (tile-table
    // owner open), nearby objects. Position only until proven -- never a guess.
}

static void CmdDump(void)
{
    char line[160];
    uint32_t m = u32(MGR_SLOT);
    snprintf(line, sizeof(line), "manager 0x%08X render %u widget 0x%08X.",
             m, InRam(m) ? u32(m + MGR_OFF_RENDER) : 0u, g_widgetRoot);
    // Log, don't speak: the dump is developer data.
    if (g_host && g_host->log) g_host->log(g_host->ctx, line);
    if (g_widgetRoot) {
        uint32_t cnt = 0;
        int idx = SelectionIndex(&cnt);
        snprintf(line, sizeof(line), "index %d count %u.", idx, cnt);
        if (g_host && g_host->log) g_host->log(g_host->ctx, line);
    }
    uint32_t d = BoardDispatcher();
    if (d) {
        int dp = BoardDP();
        snprintf(line, sizeof(line), "board D=0x%08X cursor %u,%u dp %d.", d,
                 u8(d + OFF_HX), u8(d + OFF_HY), dp);
        if (g_host && g_host->log) g_host->log(g_host->ctx, line);
    }
}

static bool Ready(void) { return ManagerOk(); }

static void OnFrame(void) { /* nothing per-frame: this adapter polls on demand */ }

static void Command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI: CmdWhereAmI(); break;
        // Board mapping of the shared commands (documented, game-specific):
        //   NextAlly      -> available movement directions from the cursor.
        //   NextEnemy     -> nearby special tiles/markers.
        // Other unit-cycling commands fall back to position.
        case Command::NextAlly: CmdDirections(); break;
        case Command::NextEnemy: CmdMarkers(); break;
        case Command::PrevAlly:
        case Command::PrevEnemy:
        case Command::NextUnactedAlly: CmdWhereAmI(); break;
        case Command::DumpState: CmdDump(); break;
    }
}

static bool Attach(const Host* host)
{
    g_host = host;
    g_widgetRoot = 0;   // never inherit a root across attach
    g_widgetPinned = false;
    return true;        // the game code check already happened in the registry
}

static void Detach(void) { g_host = nullptr; g_widgetRoot = 0; g_widgetPinned = false; }

} // namespace dissidia

// NOTE: the instance lives at oga scope (like kDragonBallZSaiyans), NOT inside
// namespace dissidia -- otherwise the registry's `oga::kDissidiaFinalFantasy`
// declaration will not link (dbz_adapter_test.cpp documents the same trap).
extern const Adapter kDissidiaFinalFantasy;
const Adapter kDissidiaFinalFantasy = {
    "dissidia",
    "Dissidia Final Fantasy",
    "ULUS10437",          // PSP game code for Dissidia Final Fantasy (USA)
    dissidia::Attach,
    dissidia::OnFrame,
    dissidia::Command,
    dissidia::Ready,
    dissidia::Detach,
};

} // namespace oga
