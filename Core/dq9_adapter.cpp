/*
 * dq9_adapter.cpp — Dragon Quest IX: Sentinels of the Starry Skies (US, YDQE).
 *
 * SCAFFOLD (native side). The full reader is third-party/DQ9-Access, a
 * BizHawk (melonDS core) Lua mod by RetroSanity that makes the whole game
 * playable blind: finished-text-buffer menus, a nearby list, camera-relative
 * directions, route planning + auto-walk over a pre-baked collision map, and
 * persistent place marks. This adapter is the native OGA front for the same
 * game: WhereAmI / party cycling / debug dump straight from the mod's own
 * documented addresses, no Lua port required.
 *
 * ⛔ WHAT IS VERIFIED, AND WHAT IS NOT:
 *   * The addresses below are the MOD's own, copied from its header comment
 *     (third-party/DQ9-Access "DQ9 Access Mod.zip" -> dq9-access.lua). They
 *     were found by the mod author running the US ROM and watching memory.
 *   * What this adapter DOES with them (validation, wording, refusal paths)
 *     is covered by Core/dq9_adapter_test.cpp on synthetic RAM.
 *   * NOT verified: a live ROM run through THIS adapter. The mod itself is
 *     battle-tested in EmuHawk; this front has not yet been held against a
 *     running game the way FE/DBZ were. Treat menu HP/MP deltas, shop
 *     quantity windows and auto-walk as mod territory until a live pass
 *     confirms them here.
 *
 * ⛔ READ-ONLY. Like every other adapter, this inspects memory and never
 * writes gameplay state.
 */
#include "adapter.h"

#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

// ---- the mod's map (dq9-access.lua header; US ROM, game code YDQE) ------------
constexpr uint32_t MSG_TEXT    = 0x0211819Cu; // message box text (pages: 0D FF)
constexpr uint32_t MENU_MARKUP = 0x02118FFCu; // menu markup, <CURSOR=n><N=i>..</N>
constexpr uint32_t MAP_CODE    = 0x020FB3FCu; // 8 ASCII chars, e.g. "M01M0100"
constexpr uint32_t PLAYER_PTR  = 0x020F33E0u; // -> player object; pos +0x44/+0x48/+0x4C (fixed /4096)
constexpr uint32_t POS_X_OFF   = 0x44u;
constexpr uint32_t POS_Y_OFF   = 0x48u;
constexpr uint32_t POS_Z_OFF   = 0x4Cu;
constexpr uint32_t PARTY0_NAME = 0x020F3888u; // first party record name; stride 0x964
constexpr uint32_t PARTY_STRIDE = 0x964u;
constexpr uint32_t PARTY_MAX   = 4u;          // DQ9 travels as a party of four
constexpr uint32_t GOLD        = 0x020F6D48u;
constexpr uint32_t BATTLE_FLAG = 0x020EF0E8u; // 1 while a battle is running

// The mod also reads the on-screen map NAME at 0x022A4266, but that sits
// above the 4 MiB main-RAM window the Host exposes (0x02000000..0x023FFFFF),
// so out-of-window reads come back 0 here. WhereAmI therefore reports the
// map CODE ("M01M0100") plus coordinates — never a guessed name — until the
// host window widens or the Lua side is ported. A wrong name is worse than
// a code.

const Host* g_host = nullptr;
int g_cursor = -1;

uint8_t u8(uint32_t a)   { return g_host ? g_host->read8(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

void Say(const char* s, bool interrupt = true)
{
    if (!oga::AdapterNoteSpoken(s)) return;
    if (g_host && g_host->speak) g_host->speak(g_host->ctx, s, interrupt);
}
void Log(const char* s)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, s);
}

bool Printable(char c) { return c >= 0x20 && c <= 0x7E; }

// Party record name. False when the bytes are not text — the signal we are
// NOT looking at a record yet, same "prove it before you speak it" rule as DBZ.
bool PartyName(int slot, char* out, size_t cap)
{
    if (slot < 0 || slot >= (int) PARTY_MAX || !out || cap < 2) return false;
    uint32_t base = PARTY0_NAME + (uint32_t) slot * PARTY_STRIDE;
    size_t n = 0;
    for (; n + 1 < cap; n++) {
        char c = (char) u8(base + (uint32_t) n);
        if (c == 0) break;
        if (!Printable(c)) return false;
        out[n] = c;
    }
    out[n] = 0;
    return n > 0;
}

bool MapCode(char* out, size_t cap)
{
    if (!out || cap < 9) return false;
    for (int i = 0; i < 8; i++) {
        char c = (char) u8(MAP_CODE + (uint32_t) i);
        if (!Printable(c)) return false;
        out[i] = c;
    }
    out[8] = 0;
    return true;
}

// Player position in whole tiles. False when the player pointer is not yet
// pointing at RAM (pre-boot / mid-transition): callers stay silent then.
bool PlayerTile(int32_t* x, int32_t* z)
{
    uint32_t p = u32(PLAYER_PTR);
    if (p < 0x02000000u || p > 0x02400000u) return false;
    int32_t fx = (int32_t) u32(p + POS_X_OFF);
    int32_t fz = (int32_t) u32(p + POS_Z_OFF);
    if (x) *x = fx / 4096;
    if (z) *z = fz / 4096;
    return true;
}

void CmdWhereAmI()
{
    char map[16];
    if (!MapCode(map, sizeof(map))) {
        Say("Location unknown. The map is still loading.");
        return;
    }
    int32_t x = 0, z = 0;
    bool battle = u8(BATTLE_FLAG) == 1;
    if (PlayerTile(&x, &z)) {
        char line[128];
        snprintf(line, sizeof(line), "On %s, position %d, %d. %s.",
                 map, x, z, battle ? "In battle" : "Exploring");
        Say(line);
    } else {
        char line[96];
        snprintf(line, sizeof(line), "On %s. %s.",
                 map, battle ? "In battle" : "Exploring");
        Say(line);
    }
}

void CmdStepAlly(int dir)
{
    // Walk the four record slots from the cursor, skipping empties; wrap once.
    for (int step = 1; step <= (int) PARTY_MAX; step++) {
        int slot = (g_cursor + dir * step + (int) PARTY_MAX * 4) % (int) PARTY_MAX;
        char nm[32];
        if (PartyName(slot, nm, sizeof(nm))) {
            g_cursor = slot;
            char line[64];
            snprintf(line, sizeof(line), "%s, %d of %d.", nm, slot + 1, PARTY_MAX);
            Say(line);
            return;
        }
    }
    Say("No party members found.");
}

void CmdDump()
{
    char line[160];
    char map[16];
    if (MapCode(map, sizeof(map))) {
        snprintf(line, sizeof(line), "[dq9] map=%s battle=%u gold=%u",
                 map, (unsigned) u8(BATTLE_FLAG), (unsigned) u32(GOLD));
    } else {
        snprintf(line, sizeof(line), "[dq9] map=<loading> battle=%u gold=%u",
                 (unsigned) u8(BATTLE_FLAG), (unsigned) u32(GOLD));
    }
    Log(line);
    uint32_t p = u32(PLAYER_PTR);
    int32_t x = 0, z = 0;
    if (PlayerTile(&x, &z))
        snprintf(line, sizeof(line), "[dq9] player=0x%08X tile=%d,%d", p, x, z);
    else
        snprintf(line, sizeof(line), "[dq9] player=0x%08X (not placed)", p);
    Log(line);
    for (int i = 0; i < (int) PARTY_MAX; i++) {
        char nm[32];
        if (PartyName(i, nm, sizeof(nm)))
            snprintf(line, sizeof(line), "[dq9] slot %d name=%s", i, nm);
        else
            snprintf(line, sizeof(line), "[dq9] slot %d <empty>", i);
        Log(line);
    }
}

bool Dq9Ready()
{
    // Ready only when record data is actually there: a printable name behind
    // slot 0. Before the game allocates its party, reads are zero and speaking
    // then would read nonsense to the player.
    char nm[32];
    return PartyName(0, nm, sizeof(nm));
}

} // namespace

static bool dq9_attach(const Host* host)
{
    g_host = host;
    g_cursor = -1;
    return true;   // the game code check already happened in the registry
}

static void dq9_on_frame(void) { /* nothing per-frame: this adapter polls on demand */ }

static void dq9_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:        CmdWhereAmI();    break;
        case Command::NextAlly:        CmdStepAlly(+1);  break;
        case Command::PrevAlly:        CmdStepAlly(-1);  break;
        case Command::NextUnactedAlly: CmdStepAlly(+1);  break;
        case Command::DumpState:       CmdDump();        break;
        default: break;  // menus, battles and travel are mod (Lua) territory
    }
}

static bool dq9_ready(void) { return Dq9Ready(); }

static void dq9_detach(void) { g_host = nullptr; g_cursor = -1; }

extern const Adapter kDragonQuestIX;
const Adapter kDragonQuestIX = {
    "dq9",
    "Dragon Quest IX: Sentinels of the Starry Skies",
    "YDQE",                  // ROM header game code, US version (the mod reads YDQE)
    dq9_attach,
    dq9_on_frame,
    dq9_command,
    dq9_ready,
    dq9_detach,
};

} // namespace oga
