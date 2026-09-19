/*
 * dissidia_adapter.cpp — Dissidia Final Fantasy (PSP, ULUS10437) adapter skeleton.
 *
 * STATUS: SCAFFOLD. The memory map below is VERIFIED against the live game
 * (docs/reverse-engineering/dissidia-final-fantasy.md, sections 40-96); the one
 * thing still missing is the live address P of the current menu's UI root, which
 * the ongoing hunt (scripts/psp-find-uiroot*.py) has not yet named. Until P is
 * set, every command refuses with "menu not tracked yet" rather than guessing.
 *
 * VERIFIED MAP (all confirmed against live RAM + decompile):
 *   TEXT_POOL   0x09D16A68  UTF-16LE menu/UI strings, one flag byte per entry
 *                             (0x00 normal; 0xFF conditional lines). 179 strings,
 *                             contiguous 0x09D16A68..0x09D1943C.
 *   MGR_SLOT    0x08B9B770  holds heap pointer -> manager object (e.g. 0x08C08EB0).
 *   Manager +0x18           render block COUNT (derived row count; responds to
 *                             delivered up/down but is NOT the index).
 *   Widget W = P + 0x12c4  (P = menu UI root; the `&DAT_000012c4 + param_1` in the
 *                             decompile is a field offset -- file bytes at 0x12c4
 *                             are MIPS code, so it cannot be a global).
 *   Index at W+0x3C (= P+0x1300), signed, -1 = none/invalid.
 *   Count at W+0x240 (= P+0x1504), 1..7.
 *   Element = W+0x60+4+idx*0x44 (stride 0x44, hard cap 7 -- matches the 7-row
 *     title menu). Selected value at element+0x18, 0xffffffff when unavailable.
 *   Reader: FUN_00250538(obj) validates 0 <= idx < count else returns -1.
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
constexpr uint32_t WIDGET_FIELD   = 0x12c4u; // W = P + 0x12c4
constexpr uint32_t IDX_OFF    = 0x3Cu;       // index at W+0x3C (= P+0x1300)
constexpr uint32_t CNT_OFF    = 0x240u;      // count at W+0x240 (= P+0x1504)
constexpr uint32_t ELT_BASE   = 0x60u;       // elements at W+0x60+4+idx*0x44
constexpr uint32_t ELT_STRIDE = 0x44u;
constexpr uint32_t ELT_CAP    = 7u;
constexpr uint32_t ELT_VAL    = 0x18u;       // selected value at element+0x18
constexpr int32_t  NO_SELECTION = -1;
constexpr uint32_t RAM_LO = 0x08800000u;
constexpr uint32_t RAM_HI = 0x0A000000u;

static const Host* g_host = nullptr;
// P = menu UI root for the CURRENT menu. 0 = not tracked yet. Set by the
// harness once the live hunt names it (see scripts/psp-find-uiroot*.py).
static uint32_t g_widgetRoot = 0;

static uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

static void Say(const char* s, bool interrupt = true)
{
    if (g_host && g_host->speak) g_host->speak(g_host->ctx, s, interrupt);
}

static bool InRam(uint32_t a) { return a >= RAM_LO && a < RAM_HI; }

/// The harness calls this once the live hunt names P. Separated from attach so
/// a stale P can never survive a menu change: whoever observes a new menu must
/// re-set (or clear) the root.
void SetWidgetRoot(uint32_t p) { g_widgetRoot = p; }
uint32_t WidgetRoot(void) { return g_widgetRoot; }

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
    uint32_t w = g_widgetRoot + WIDGET_FIELD;
    int32_t idx = (int32_t) u32(w + IDX_OFF);
    uint32_t cnt = u32(w + CNT_OFF);
    if (countOut) *countOut = cnt;
    if (cnt == 0 || cnt > ELT_CAP) return NO_SELECTION;
    if (idx < 0 || (uint32_t) idx >= cnt) return NO_SELECTION;
    return (int) idx;
}

static void CmdWhereAmI(void)
{
    if (!ManagerOk()) { Say("Game not ready yet."); return; }
    if (!g_widgetRoot) { Say("Menu not tracked yet."); return; }
    uint32_t cnt = 0;
    int idx = SelectionIndex(&cnt);
    if (idx < 0) { Say("No selection."); return; }
    char line[96];
    snprintf(line, sizeof(line), "Row %d of %u.", idx + 1, cnt);
    Say(line);
    // TODO: resolve the selected item's text. FUN_00251de4(element+0x18) names the
    // value, but the value->string mapping is not yet established. Speak position
    // only until it is -- never a guess.
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
}

static bool Ready(void) { return ManagerOk(); }

static void OnFrame(void) { /* nothing per-frame: this adapter polls on demand */ }

static void Command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI: CmdWhereAmI(); break;
        // Cursor-following commands need the live index first; until the hunt
        // names P they refuse inside CmdWhereAmI rather than walking blind.
        case Command::NextAlly:
        case Command::PrevAlly:
        case Command::NextEnemy:
        case Command::PrevEnemy:
        case Command::NextUnactedAlly: CmdWhereAmI(); break;
        case Command::DumpState: CmdDump(); break;
    }
}

static bool Attach(const Host* host)
{
    g_host = host;
    g_widgetRoot = 0;   // never inherit a root across attach
    return true;        // the game code check already happened in the registry
}

static void Detach(void) { g_host = nullptr; g_widgetRoot = 0; }

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
