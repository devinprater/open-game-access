/*
 * dq9_adapter.cpp — Dragon Quest IX: Sentinels of the Starry Skies (US, YDQE).
 *
 * NATIVE FRONT for third-party/DQ9-Access, RetroSanity's BizHawk (melonDS
 * core) Lua mod. The mod itself plays the whole game blind; this adapter
 * re-implements its stateless reads natively so OGA hosts get them with no
 * Lua port: WhereAmI / party cycling / nearby-object scan with
 * camera-relative directions / menu-cursor echo / debug dump, straight from
 * the mod's own documented addresses.
 *
 * ⛔ WHAT IS VERIFIED, AND WHAT IS NOT:
 *   * The addresses below are the MOD's own, copied from its header comment
 *     (third-party/DQ9-Access "DQ9 Access Mod.zip" -> dq9-access.lua) and its
 *     field formats (fixed-point /4096 positions, <CURSOR=n> markup, object
 *     table layout) copied from the functions that read them. They were found
 *     by the mod author running the US ROM and watching memory.
 *   * What this adapter DOES with them (validation, wording, refusal paths)
 *     is covered by Core/dq9_adapter_test.cpp on synthetic RAM.
 *   * LIVE-CHECKED AGAINST A TITLE SNAPSHOT (Oct 2026, dq9-t0.ram): the gates
 *     above refuse title RAM — "NineRZ" junk, set battle flag, empty buffers
 *     all read as not-ready / no-menu / unknown, never spoken. NOT yet
 *     verified: in-game speech against a running game (needs scripted play
 *     past name entry); the mod itself covers that ground in EmuHawk.
 *   * DELIBERATELY MOD TERRITORY (stateful, per-frame): dialogue queueing,
 *     learned NPC names, shop/skill flows, route planning + auto-walk, place
 *     marks. Porting those would fork the mod's state machine into a second
 *     copy that drifts. Native labels are therefore generic (someone /
 *     something / story character); names the mod learns stay in the mod.
 *
 * ⛔ READ-ONLY. Like every other adapter, this inspects memory and never
 * writes gameplay state.
 */
#include "adapter.h"

#include <math.h>
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
constexpr uint32_t CAMERA      = 0x0210A134u; // s32 fixed /4096 camera x@+0 z@+8
constexpr uint32_t OBJ_TABLE   = 0x02107600u; // array of u32 object pointers
constexpr uint32_t OBJ_COUNT   = 0x02107680u; // u32 object count (mod refuses >64)
constexpr uint32_t OBJ_CAP     = 64u;
constexpr uint32_t PARTY0_NAME = 0x020F3888u; // first party record name; stride 0x964
constexpr uint32_t PARTY_STRIDE = 0x964u;
constexpr uint32_t PARTY_MAX   = 4u;          // DQ9 travels as a party of four
constexpr uint32_t GOLD        = 0x020F6D48u;
constexpr uint32_t BATTLE_FLAG = 0x020EF0E8u; // 1 while a battle is running
constexpr uint32_t BATTLE_PHASE = 0x02109DA6u; // mod trusts menus only at phase 3
constexpr uint32_t EVENT_CHOICE_COUNT = 0x021153A4u; // yes/no prompt count
constexpr uint32_t MENU_BYTES  = 1200u;       // mod reads the markup as cstring(MENU, 1200)

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
int32_t s32(uint32_t a)  { return (int32_t) u32(a); }
bool InRam(uint32_t a, uint32_t n)
{
    return a >= 0x02000000u && (uint64_t) a + n <= (uint64_t) 0x02000000u + 0x400000u;
}

// Speech routed through the announcement queue with a per-site group and
// priority. Null-queue hosts fall back to the direct wire, which keeps the
// host tests' synchronous stubs working unchanged.
void Say(const char* s, const char* group, oga::Priority pri)
{
    if (!oga::AdapterNoteSpoken(s)) return;
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

bool Printable(char c) { return c >= 0x20 && c <= 0x7E; }

// Ready gate, defined with the commands below; forward-declared so the
// cycling commands can refuse through it (title-RAM junk must not cycle).
bool Dq9Ready();

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

// Player position in fractional tiles (the mod's player_pos: s32 / 4096).
bool PlayerPos(double* x, double* z)
{
    uint32_t p = u32(PLAYER_PTR);
    if (!InRam(p, 0x60)) return false;
    if (x) *x = s32(p + POS_X_OFF) / 4096.0;
    if (z) *z = s32(p + POS_Z_OFF) / 4096.0;
    return true;
}

// The mod's screen_axes: "up" on screen is the unit vector from the camera to
// the player in world x/z; "right" is its perpendicular. Falls back to
// north-up when the camera reads degenerate, exactly like the mod.
void ScreenAxes(double* ux, double* uz, double* rx, double* rz)
{
    double px = 0, pz = 0;
    if (!PlayerPos(&px, &pz)) { *ux = 0; *uz = -1; *rx = 1; *rz = 0; return; }
    double cx = s32(CAMERA) / 4096.0, cz = s32(CAMERA + 8) / 4096.0;
    double vx = px - cx, vz = pz - cz;
    double len = sqrt(vx * vx + vz * vz);
    if (len < 0.01) { *ux = 0; *uz = -1; *rx = 1; *rz = 0; return; }
    *ux = vx / len; *uz = vz / len;
    *rx = -*uz; *rz = *ux;
}

// The mod's directions_to: project the target offset onto the screen axes and
// speak whole steps, dominant axis first; under 1.2 tiles is "right next to
// you". Returns false (out stays empty) only when the player is unplaced.
bool DirectionsTo(double tx, double tz, char* out, size_t cap)
{
    double px = 0, pz = 0;
    if (!PlayerPos(&px, &pz)) return false;
    double ux, uz, rx, rz;
    ScreenAxes(&ux, &uz, &rx, &rz);
    double dx = tx - px, dz = tz - pz;
    double dist = sqrt(dx * dx + dz * dz);
    if (dist < 1.2) { snprintf(out, cap, "right next to you"); return true; }
    double fwd = dx * ux + dz * uz, right = dx * rx + dz * rz;
    char a[48] = "", b[48] = "";
    long na = (long) floor(fabs(fwd) + 0.5), nb = (long) floor(fabs(right) + 0.5);
    if (na > 0) snprintf(a, sizeof(a), "%ld step%s %s", na, na == 1 ? "" : "s", fwd >= 0 ? "up" : "down");
    if (nb > 0) snprintf(b, sizeof(b), "%ld step%s %s", nb, nb == 1 ? "" : "s", right >= 0 ? "right" : "left");
    if (a[0] && b[0]) {
        if (fabs(fwd) >= fabs(right)) snprintf(out, cap, "%s, %s", a, b);
        else                          snprintf(out, cap, "%s, %s", b, a);
    } else if (a[0]) snprintf(out, cap, "%s", a);
    else if (b[0]) snprintf(out, cap, "%s", b);
    else { snprintf(out, cap, "right next to you"); }
    return true;
}

// ---- nearby scan (the mod's characters(): the N key, natively) ----------------
//
// Object table: OBJ_TABLE holds count pointers; each object carries a model
// pointer at +0x08 whose 8-char id sits at m+4. Story models (^sNNN) keep
// their position further in (+0x64/+0x6C); everything else uses +0x24/+0x2C,
// all s32 fixed-point /4096. Parked off-map (>1000) and the player's own
// tile (<0.05) are skipped, exactly like the mod. Labels stay generic —
// learned names (who people are, learned when you talk to them) need the
// mod's dialogue state machine, so a person is "Someone", a story model a
// "Story character", anything else "Something".
struct Nearby { char label[24]; double x, z, dist; };

bool ModelId(uint32_t o, char* out, size_t cap)
{
    uint32_t m = u32(o + 0x08);
    if (!InRam(m, 12)) return false;
    size_t n = 0;
    for (; n + 1 < cap && n < 8; n++) {
        char c = (char) u8(m + 4 + (uint32_t) n);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;
        out[n] = c;
    }
    out[n] = 0;
    return n > 0;
}

int CollectNearby(Nearby* out, int cap, double px, double pz)
{
    uint32_t n = u32(OBJ_COUNT);
    if (n > OBJ_CAP) return 0;   // the mod treats an over-count as no list
    int found = 0;
    for (uint32_t i = 0; i < n && found < cap; i++) {
        uint32_t o = u32(OBJ_TABLE + 4 * i);
        if (!InRam(o, 0x70)) continue;
        char model[16];
        if (!ModelId(o, model, sizeof(model))) continue;
        bool story = model[0] == 's' && model[1] >= '0' && model[1] <= '9' &&
                     model[2] >= '0' && model[2] <= '9' && model[3] >= '0' && model[3] <= '9';
        bool person = model[0] >= 'a' && model[0] <= 'z' &&
                      model[1] >= '0' && model[1] <= '9' &&
                      model[2] >= '0' && model[2] <= '9' &&
                      model[3] >= '0' && model[3] <= '9';
        double x, z;
        if (story) { x = s32(o + 0x64) / 4096.0; z = s32(o + 0x6C) / 4096.0; }
        else       { x = s32(o + 0x24) / 4096.0; z = s32(o + 0x2C) / 4096.0; }
        if (fabs(x) > 1000 || fabs(z) > 1000) continue;
        if (fabs(x - px) < 0.05 && fabs(z - pz) < 0.05) continue;
        Nearby* nb = &out[found++];
        snprintf(nb->label, sizeof(nb->label), "%s",
                 story ? "Story character" : person ? "Someone" : "Something");
        nb->x = x; nb->z = z;
        double dx = x - px, dz = z - pz;
        nb->dist = sqrt(dx * dx + dz * dz);
    }
    // Nearest first (insertion sort; the table caps at 64).
    for (int i = 1; i < found; i++) {
        Nearby t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].dist > t.dist) { out[j + 1] = out[j]; j--; }
        out[j + 1] = t;
    }
    return found;
}

int g_objcursor = -1;

void CmdNearby(int dir)
{
    double px = 0, pz = 0;
    if (!PlayerPos(&px, &pz)) { Say("Position unknown.", "nearby", oga::Priority::High); return; }
    Nearby list[64];
    int n = CollectNearby(list, 64, px, pz);
    if (n <= 0) { Say("Nobody nearby.", "nearby", oga::Priority::High); return; }
    g_objcursor = (g_objcursor + dir + n * 4) % n;
    char where[96];
    if (!DirectionsTo(list[g_objcursor].x, list[g_objcursor].z, where, sizeof(where)))
    { Say("Position unknown.", "nearby", oga::Priority::High); return; }
    char line[160];
    snprintf(line, sizeof(line), "%s. %s.", list[g_objcursor].label, where);
    Say(line, "nearby", oga::Priority::High);
}

// ---- menu echo (the mod's menu reader, stateless part) -------------------------
//
// MENU_MARKUP holds the current menu as text with <CURSOR=n> and
// <N=i>label</N> items, e.g. "<CURSOR=1><N=0>Fight</N> <N=1>Examine</N>".
// The mod only trusts a battle list while the battle is waiting for a command
// (phase 3 at BATTLE_PHASE); otherwise the buffer can hold a pre-built or
// stale list, and we say the menu is busy instead of reading old words.
bool MenuBuffer(char* out, size_t cap)
{
    if (cap < 2) return false;
    size_t n = 0;
    for (; n + 1 < cap && n < MENU_BYTES; n++) {
        char c = (char) u8(MENU_MARKUP + (uint32_t) n);
        if (c == 0) break;
        if (c != '\n' && (c < 0x20 || c > 0x7E)) return false;
        out[n] = c;
    }
    out[n] = 0;
    return n > 0;
}

int MenuCursor(const char* m)
{
    const char* p = strstr(m, "<CURSOR=");
    if (!p) return -1;
    int v = 0, digits = 0;
    for (p += 8; *p >= '0' && *p <= '9'; p++) { v = v * 10 + (*p - '0'); digits++; }
    if (digits == 0 || *p != '>') return -1;
    return v;
}

// Labels of the <N=i>..</N> items, in order. Inner tags are cut: a label is
// words, not markup.
int MenuItems(const char* m, char items[][48], int cap)
{
    int count = 0;
    const char* p = m;
    while (count < cap && (p = strstr(p, "<N=")) != nullptr) {
        p += 3;
        while (*p >= '0' && *p <= '9') p++;
        if (*p != '>') continue;
        p++;
        const char* e = strstr(p, "</N>");
        if (!e) break;
        size_t n = 0;
        for (const char* q = p; q < e && n + 1 < 48; q++) {
            if (*q == '<') break;
            if (*q >= 0x20 && (unsigned char) *q <= 0x7E) items[count][n++] = *q;
        }
        items[count][n] = 0;
        if (n > 0) count++;
        p = e + 4;
    }
    return count;
}

void CmdMenuState()
{
    // Content first, battle-phase second. A live title snapshot has the battle
    // flag SET with an empty menu buffer (the flag is meaningless pre-game):
    // checking the buffer first reads that as "No menu.", while a stale
    // buffer mid-battle still reads as busy via the phase rule below.
    char m[1200];
    bool haveMenu = false;
    int cursor = -1, count = 0;
    char items[16][48];
    if (MenuBuffer(m, sizeof(m))) {
        cursor = MenuCursor(m);
        if (cursor >= 0) { count = MenuItems(m, items, 16); haveMenu = count > 0; }
    }
    bool battle = u8(BATTLE_FLAG) == 1;
    if (haveMenu) {
        if (battle && u8(BATTLE_PHASE) != 3) { Say("Menu. Busy.", "menu", oga::Priority::High); return; }
        if (cursor < count) {
            char line[128];
            snprintf(line, sizeof(line), "Menu. %s. %d item%s.",
                     items[cursor], count, count == 1 ? "" : "s");
            Say(line, "menu", oga::Priority::High);
            return;
        }
        char line[64];
        snprintf(line, sizeof(line), "Menu. %d item%s.", count, count == 1 ? "" : "s");
        Say(line, "menu", oga::Priority::High);
        return;
    }
    // A yes/no prompt with no item list (the mod's EVENT_CHOICE). The 0/1 ->
    // Yes/No assignment is the mod's runtime knowledge, not in the addresses,
    // so the native side names the choice, never the cursor.
    if (u32(EVENT_CHOICE_COUNT) > 0 && u32(EVENT_CHOICE_COUNT) < 256) {
        Say("Choose. Yes or no.", "menu", oga::Priority::High);
        return;
    }
    Say("No menu.", "menu", oga::Priority::High);
}

void CmdWhereAmI()
{
    char map[16];
    if (!MapCode(map, sizeof(map))) {
        Say("Location unknown. The map is still loading.", "whereami", oga::Priority::High);
        return;
    }
    int32_t x = 0, z = 0;
    bool battle = u8(BATTLE_FLAG) == 1;
    if (PlayerTile(&x, &z)) {
        char line[128];
        snprintf(line, sizeof(line), "On %s, position %d, %d. %s.",
                 map, x, z, battle ? "In battle" : "Exploring");
        Say(line, "whereami", oga::Priority::High);
    } else {
        char line[96];
        snprintf(line, sizeof(line), "On %s. %s.",
                 map, battle ? "In battle" : "Exploring");
        Say(line, "whereami", oga::Priority::High);
    }
}

void CmdStepAlly(int dir)
{
    // The party slots can hold printable junk before the game allocates them
    // (a live title snapshot reads "NineRZ" at slot 0): the ready gate owns
    // the allocated-or-not decision, and cycling refuses when it is shut.
    if (!Dq9Ready()) { Say("No party members found.", "party", oga::Priority::High); return; }
    // Walk the four record slots from the cursor, skipping empties; wrap once.
    for (int step = 1; step <= (int) PARTY_MAX; step++) {
        int slot = (g_cursor + dir * step + (int) PARTY_MAX * 4) % (int) PARTY_MAX;
        char nm[32];
        if (PartyName(slot, nm, sizeof(nm))) {
            g_cursor = slot;
            char line[64];
            snprintf(line, sizeof(line), "%s, %d of %d.", nm, slot + 1, PARTY_MAX);
            Say(line, "party", oga::Priority::High);
            return;
        }
    }
    Say("No party members found.", "party", oga::Priority::High);
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
    snprintf(line, sizeof(line), "[dq9] camera=%d,%d objs=%u phase=%u choice=%u",
             s32(CAMERA) / 4096, s32(CAMERA + 8) / 4096,
             (unsigned) u32(OBJ_COUNT), (unsigned) u8(BATTLE_PHASE),
             (unsigned) u32(EVENT_CHOICE_COUNT));
    Log(line);
}

bool Dq9Ready()
{
    // Ready only when the game is actually up: a printable name behind slot 0
    // AND a map code. Either alone lies — a live title snapshot holds the
    // printable junk "NineRZ" at slot 0 with no map, and map code without
    // party reads zeros mid-transition. Both must agree.
    char map[16], nm[32];
    return MapCode(map, sizeof(map)) && PartyName(0, nm, sizeof(nm));
}

} // namespace

static bool dq9_attach(const Host* host)
{
    g_host = host;
    g_cursor = -1;
    g_objcursor = -1;
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
        case Command::NextEnemy:       CmdNearby(+1);    break;
        case Command::PrevEnemy:       CmdNearby(-1);    break;
        case Command::MenuState:       CmdMenuState();   break;
        case Command::DumpState:       CmdDump();        break;
        default: break;  // dialogue, shops and travel are mod (Lua) territory
    }
}

static bool dq9_ready(void) { return Dq9Ready(); }

static void dq9_detach(void) { g_host = nullptr; g_cursor = -1; g_objcursor = -1; }

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
