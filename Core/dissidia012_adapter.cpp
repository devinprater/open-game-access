/*
 * dissidia012_adapter.cpp — Dissidia 012 Final Fantasy (PSP, ULUS10566) adapter, first cut.
 *
 * STATUS: SKELETON. It attaches only on a fingerprinted build and speaks nothing yet.
 * Every address below is STATIC (decrypted EBOOT, Ghidra project oga-ghidra-dissidia012)
 * and is NOT runtime-confirmed. Do not carry an address from dissidia_adapter.cpp
 * (ULUS10437) into this file: 012 loads at 0x08804040, has three overlays linked at
 * 0x089dd4c0, and its board and battle code was rewritten. See
 * docs/research/dissidia-012-start.md for the carry-over survey.
 *
 * Plumbing that survived byte-for-byte (masked-opcode match), so the original's
 * menu-manager READ PATH should port once the live addresses are re-found:
 *   menu selection validator   0x088d75f0   (orig FUN_00250538)
 *   menu event dispatcher      0x088cf550   (orig FUN_0024932c)
 *   menu node lookup           0x088da828   (orig FUN_0025468c)
 *   menu input consumer        0x088ea070   (orig FUN_00267cb8)
 *   pad read / input sink      0x08871e24 / 0x08871bd0
 *   event poster / SFX poster  0x088a9de8 / 0x0882fa10
 * In OVL_BATTLE_APP (only valid while that overlay is resident):
 *   talk-event manager init    0x08abe9f0   EX pickup poster 0x08a3abc4
 */
#include "adapter.h"
#include <stdint.h>

namespace oga {
namespace dissidia012 {

static const Host* g_host = nullptr;

static uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

// Instruction words 1 and 2 of three resident EBOOT functions, read from the
// decrypted 012 EBOOT. All three must match: a different build, or a title that
// is not 012, fails closed and the registry falls through.
struct Fp { uint32_t addr, w1, w2; };
// Words 1 and 2, NOT word 0: PPSSPP overwrites a compiled function's first word with its
// own 0x68-prefixed marker, so word 0 changes at runtime (measured live on the pad read).
static const Fp kFingerprint[3] = {
    { 0x08871e24u, 1007028381u, 3301742380u },   // pad read
    { 0x088a9de8u, 2946826240u, 16797733u },   // event poster
    { 0x088d75f0u, 2947547136u, 2947612676u },   // menu selection validator
};

static bool BuildOk(void)
{
    for (const Fp& f : kFingerprint)
        if (u32(f.addr + 4) != f.w1 || u32(f.addr + 8) != f.w2) return false;
    return true;
}

static bool Attach(const Host* host)
{
    g_host = host;
    if (!BuildOk()) {
        if (host && host->log) host->log(host->ctx, "dissidia012: build fingerprint mismatch");
        g_host = nullptr;
        return false;
    }
    if (host && host->log) host->log(host->ctx, "dissidia012: attached (ULUS10566, static map only)");
    return true;
}

// ---- Title menu (RUNTIME-CONFIRMED headless, two boots, USA CSO) ----
// New Game / Load Game / Extras. Cursor u32 0..2 at 0x096AE20C: stepped Down x3 read
// 0,1,2,0 and Up read 2; Cross on 2 opened the Extras screen (screenshot). The neighbours
// below fingerprint the struct so a stale heap word is never spoken. Heap address, so a
// mismatch fails closed.
static const uint32_t TITLE_CURSOR = 0x096AE20Cu;
static const char* kTitleEntries[3] = { "New Game", "Load Game", "Extras" };

static int TitleIndex(void)
{
    if (!g_host) return -1;
    if (u32(TITLE_CURSOR - 4) != 65793u) return -1;
    if (u32(TITLE_CURSOR + 4) != 16447u) return -1;
    if (u32(TITLE_CURSOR + 8) != 0xFFFFFFFFu) return -1;
    uint32_t i = u32(TITLE_CURSOR);
    return i <= 2 ? (int) i : -1;
}

static int g_lastTitle = -1;

static void SayTitle(int i, oga::Priority pri)
{
    const char* s = kTitleEntries[i];
    if (g_host->announce_q)
        oga::announce(g_host->announce_q, oga::Announcement{s, pri, "menu", nullptr, 0, 0, -1}, g_host->now_ms);
    else if (g_host->speak)
        g_host->speak(g_host->ctx, s, pri == oga::Priority::High);
}

static void OnFrame(void)
{
    int t = TitleIndex();
    if (t != g_lastTitle) {
        g_lastTitle = t;
        if (t >= 0) SayTitle(t, oga::Priority::Normal);
    }
}

static void Command(oga::Command cmd)
{
    if (cmd == oga::Command::WhereAmI || cmd == oga::Command::MenuState) {
        int t = TitleIndex();
        if (t >= 0) SayTitle(t, oga::Priority::High);
    }
}
static bool Ready(void) { return g_host && BuildOk(); }
int TitleIndexForTest(void) { return TitleIndex(); }
static void Detach(void) { g_host = nullptr; g_lastTitle = -1; }

} // namespace dissidia012

extern const Adapter kDissidia012;  // extern first: namespace-scope const is otherwise internal
const Adapter kDissidia012 = {
    "dissidia012",
    "Dissidia 012 Final Fantasy",
    "ULUS10566",
    dissidia012::Attach,
    dissidia012::OnFrame,
    dissidia012::Command,
    dissidia012::Ready,
    dissidia012::Detach,
    nullptr,
    "Title menu speech (New Game, Load Game, Extras); everything else not wired yet",
    "Dissidia 012 Final Fantasy, USA (ULUS10566). Early build: the title menu "
    "speaks as you move. Everything past the title is not wired yet.",
};

} // namespace oga
