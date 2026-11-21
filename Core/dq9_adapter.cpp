/*
 * dq9_adapter.cpp — Dragon Quest IX: Sentinels of the Starry Skies (US, YDQE).
 *
 * NATIVE FRONT for third-party/DQ9-Access, RetroSanity's BizHawk (melonDS
 * core) Lua mod. The mod itself plays the whole game blind; this adapter
 * re-implements its reads natively so OGA hosts get them with no Lua port:
 * WhereAmI / party cycling / nearby-object scan with camera-relative
 * directions / menu speech / debug dump, straight from the mod's own
 * documented addresses — plus, since Nov 2026, the frame-polled half the
 * mod's design calls for: menus announce themselves as they open and as
 * the cursor moves, and the whole start flow (title menus, character
 * creation, the name-entry keyboard) is read the way the mod reads it.
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
 *     all read as not-ready / no-menu / unknown, never spoken.
 *   * LIVE CAPTURE of the whole start flow (Nov 2026, the real US ROM in the
 *     production core, driven title -> creation -> name entry -> in game):
 *     the title menus are script-engine objects (0x0231624C / 0x023162D4,
 *     class 0x021FE80C, cursor at +0x28, option list filled at +0x74) and
 *     the menu markup buffer is EMPTY at the title, exactly as the mod
 *     says; creation builds its <PLTT> field list in that buffer line by
 *     line; the name keyboard is the grid control at 0x022A517C in an
 *     11x6 shape with its cursor at +0x60/+0x62, and the name being typed
 *     sits in the buffer as "<X=n>name****". The capture also settled the
 *     map-name question below: 0x022A4266 IS inside the host's Main RAM
 *     domain (pokecore.cpp serves adapters the flat 4 MiB), so the old
 *     header note claiming otherwise was wrong; WhereAmI still reports
 *     the map CODE for now — speaking the name is a separate change.
 *   * The keyboard's letter-case state is NOT in the control: diffing
 *     0x022A517C..+0x100 across a live "Capital letters" press showed no
 *     byte change, while the typed letter did come out capital. The mod
 *     tracks case from BizHawk button events, which the native Host does
 *     not expose, so key echoes here use the mod's base glyph tables and
 *     the buffer's own name text (case-accurate) carries the truth.
 *   * STILL MOD TERRITORY (stateful): dialogue queueing, learned NPC
 *     names, shop/skill flows, route planning + auto-walk, place marks.
 *     Native labels are therefore generic (someone / something / story
 *     character); names the mod learns stay in the mod.
 *
 * ⛔ READ-ONLY. Like every other adapter, this inspects memory and never
 * writes gameplay state.
 */
#include "adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

// The mod also reads the on-screen map NAME at 0x022A4266. An earlier note
// here claimed that address sat above the Host's Main RAM window and read
// back 0; it does not — pokecore.cpp serves adapters the flat 4 MiB domain
// (0x02000000 + 0x400000), so the name is reachable. WhereAmI still reports
// the map CODE ("M01M0100") plus coordinates: switching it to the name is
// a wording change that deserves its own verification, not a drive-by.

// ---- start flow (the mod's title / creation / name-entry reads) -----------
// All live-verified against the US ROM (Nov 2026 capture; see header note).
constexpr uint32_t LOG_NAME    = 0x020FB258u; // hero name copy; first byte 0 = no log
constexpr uint32_t TITLE_CLASS = 0x021FE80Cu; // class word of both title objects
constexpr uint32_t TITLE_OBJ[2]   = {0x0231624Cu, 0x023162D4u}; // no-log / with-log menu
constexpr uint32_t TITLE_ID[2]    = {0x12Du, 0x12Eu};           // id word at +6
constexpr int      TITLE_COUNT[2] = {2, 3};
const char* const  TITLE_ITEMS[2][3] = {
    {"Create a new adventure log", "Configure Nintendo Wi-Fi Connection settings", nullptr},
    {"Continue your adventure", "Delete adventure log", "Configure Nintendo Wi-Fi Connection settings"},
};
// One control serves creation's option grids AND the name keyboard: the
// pointers at +0x20/+0x24 (and, for the keyboard, +0x28/+0x2C) must
// self-reference (+0xC / +8 / +0x1C / +0x18) before the mod trusts it.
// Grid width/height at +8/+0xC, D-pad cell at +0x18/+0x1C, keyboard
// cursor pixel x/y at +0x60/+0x62.
constexpr uint32_t GRID_CTRL   = 0x022A517Cu;

const Host* g_host = nullptr;
int g_cursor = -1;

uint8_t u8(uint32_t a)   { return g_host ? g_host->read8(g_host->ctx, a) : 0; }
uint16_t u16(uint32_t a) { return g_host ? g_host->read16(g_host->ctx, a) : 0; }
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
    // "N of M", the target-cycling house style (this adapter's own party
    // cycling does the same): with generic labels — three "Someone"s in a
    // row — the ordinal is the only thing that tells the player the cycle
    // is actually moving.
    snprintf(line, sizeof(line), "%s, %d of %d. %s.",
             list[g_objcursor].label, g_objcursor + 1, n, where);
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

// ---- shared menu view -----------------------------------------------------
//
// CmdMenuState and the frame poller read the same parsed view, so an
// automatic announcement and the player's own MenuState query can never
// disagree about what is on screen.
struct MenuView {
    int cursor = -1;
    int count = 0;
    char items[16][48];
};

bool ParseMenu(const char* m, MenuView* v)
{
    v->cursor = MenuCursor(m);
    if (v->cursor < 0) return false;
    v->count = MenuItems(m, v->items, 16);
    return v->count > 0;
}

// Change signatures for the frame poller: a light FNV-1a over the parsed
// view, so steady-state frames cost one small parse, never a re-read.
uint32_t MenuItemsSig(const MenuView& v)
{
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t) v.count) * 16777619u;
    for (int i = 0; i < v.count; i++)
        for (const char* p = v.items[i]; *p; p++)
            h = (h ^ (uint8_t) *p) * 16777619u;
    return h;
}
uint32_t MenuSig(const MenuView& v)
{
    return (MenuItemsSig(v) ^ (uint32_t) (v.cursor + 2)) * 16777619u;
}

// A battle the adapter can trust: the ready gate is up AND the flag is set.
// The flag alone lies before the game starts (a live title snapshot has it
// SET), so every busy rule is scoped through here.
bool InRealBattle() { return Dq9Ready() && u8(BATTLE_FLAG) == 1; }

bool ChoiceUp()
{
    uint32_t c = u32(EVENT_CHOICE_COUNT);
    return c > 0 && c < 256;
}

// ---- start flow: title menus (the mod's title_menu) -----------------------
//
// At the title the markup buffer is EMPTY (live-verified): the menus are
// script-engine objects, and they are ABANDONED, not freed, when creation
// starts — the class words sit valid through the whole start flow until
// the world loads over them. So a valid title object only means "a title
// menu was built"; the watchers and queries below additionally require
// the markup buffer to be empty (the mod's own guard) before believing
// the title is what is on screen.
bool TitleObjOk(int i)
{
    return u32(TITLE_OBJ[i]) == TITLE_CLASS && (uint32_t) u16(TITLE_OBJ[i] + 6) == TITLE_ID[i];
}
bool TitleObjFilled(int i)
{
    return TitleObjOk(i) && u16(TITLE_OBJ[i] + 0x74) != 0;
}

// Which title menu is on screen: a TITLE_OBJ index, or -1. When exactly
// one object has its option list filled, that one wins (the mod's rule);
// otherwise fall back to whether a hero name exists on the cartridge.
int TitlePick()
{
    bool f0 = TitleObjFilled(0), f1 = TitleObjFilled(1);
    if (f0 != f1) return f0 ? 0 : 1;
    int want = u8(LOG_NAME) != 0 ? 1 : 0;
    return TitleObjOk(want) ? want : -1;
}
int TitleCursor(int idx) { return (int) u32(TITLE_OBJ[idx] + 0x28); }

bool TitleContext()
{
    // The mod's title test: no map loaded and no player object. Character
    // creation shares it — the map loads only when the adventure starts.
    return u8(MAP_CODE) == 0 && u32(PLAYER_PTR) == 0;
}

// ---- start flow: character creation (the mod's CREATE_* reads) ------------
//
// Creation speaks through the markup buffer as a palette list: one
// <PLTT=14> line (the field being edited) among <PLTT=15> lines, each
// value possibly preceded by <X=n>/<W=n> tags and followed by a literal
// backslash-n. The highlighted line's 1-based position IS the field:
// 1 Gender, 2 Body type, 3 Hairstyle, 4 Hair colour, 5 Face, 6 Skin
// colour, 7 Eye colour, 8 Name. The list builds line by line as fields
// are confirmed, and the teardown can flash a ninth, EMPTY highlighted
// line — an ordinal outside 1..8 is not a field.
const char* const kCreateFields[8] = {
    "Gender", "Body type", "Hairstyle", "Hair colour",
    "Face", "Skin colour", "Eye colour", "Name",
};

bool ParseCreation(const char* m, int* fieldOut, char* valOut, size_t valCap,
                   char* genderOut, size_t genderCap)
{
    if (strncmp(m, "<PLTT=", 6) != 0) return false;
    int ordinal = 0, field = -1;
    char gender[16] = "";
    const char* p = m;
    while ((p = strstr(p, "<PLTT=")) != nullptr) {
        p += 6;
        int pltt = 0; bool digits = false;
        while (*p >= '0' && *p <= '9') { pltt = pltt * 10 + (*p - '0'); p++; digits = true; }
        if (!digits || *p != '>') break;
        p++;
        char val[64]; size_t n = 0;
        while (*p && *p != '\\') {
            if (*p == '<') {
                if (strncmp(p, "<PLTT=", 6) == 0) break;
                const char* e = strchr(p, '>');
                if (!e) { p += strlen(p); break; }
                p = e + 1;
                continue;
            }
            if (n + 1 < sizeof(val)) val[n++] = *p;
            p++;
        }
        val[n] = 0;
        ordinal++;
        if (ordinal == 1) {
            // The first line's value opens with the gender word; the
            // picture descriptions for fields 3 and 5 are gendered.
            size_t k = 0;
            while (val[k] && ((val[k] >= 'A' && val[k] <= 'Z') || (val[k] >= 'a' && val[k] <= 'z'))
                   && k + 1 < sizeof(gender)) { gender[k] = val[k]; k++; }
            gender[k] = 0;
        }
        if (pltt == 14) {
            field = ordinal;
            if (valOut && valCap) snprintf(valOut, valCap, "%s", val);
        }
        if (!*p) break;
    }
    if (field < 1 || field > 8) return false;
    if (genderOut && genderCap) snprintf(genderOut, genderCap, "%s", gender);
    if (fieldOut) *fieldOut = field;
    return true;
}

// The mod's CREATE_LOOKS: what each "Type N" picture actually looks like.
// N is the 1-based index the list itself prints.
const char* CreateLook(int field, const char* value, const char* gender)
{
    if (strncmp(value, "Type ", 5) != 0) return nullptr;
    int n = atoi(value + 5);
    bool female = gender && strcmp(gender, "Female") == 0;
    static const char* const kBody[5] = {
        "tallest and slim", "tall", "average height", "short and stocky", "shortest and small"};
    static const char* const kHairM[10] = {
        "short and messy", "wide, flicked out at the sides", "bowl cut",
        "short crop with a fringe", "long, wild and spiky",
        "straight, shoulder length, parted in the middle", "tall and spiky",
        "neat, with a side-swept fringe", "swept up on top, short at the sides", "bald"};
    static const char* const kHairF[10] = {
        "bob with a straight fringe", "bob with a fringe over one eye",
        "bunches sticking out", "big curly up-do", "short and swept to the side",
        "shoulder length, flicked out", "long and curly", "short and spiky",
        "high ponytail", "short rounded bob"};
    static const char* const kHairColour[10] = {
        "very dark brown", "reddish brown", "red", "pink", "blonde",
        "green", "blue", "purple", "light blue", "grey"};
    static const char* const kFaceM[10] = {
        "big round friendly eyes", "determined eyes, bold eyebrows",
        "narrow, stern eyes", "fierce eyes, heavy eyebrows",
        "sleepy, half-closed eyes", "small round eyes, surprised look",
        "tiny dot eyes", "round eyes, gentle look",
        "intense, angry eyes", "eyes closed, smiling"};
    static const char* const kFaceF[10] = {
        "big round sweet eyes", "determined eyes, raised eyebrows",
        "tall oval eyes, calm", "narrow eyes with long lashes",
        "sharp eyes, arched eyebrows", "eyes closed, smiling",
        "sleepy, half-closed eyes", "made-up, with eye shadow and lipstick",
        "small round eyes and freckles", "wide round eyes, rosy cheeks"};
    static const char* const kSkin[8] = {
        "very fair", "fair", "light, rosy", "light tan",
        "tan", "medium brown", "dark brown", "deep brown"};
    static const char* const kEyes[8] = {
        "black", "brown", "red", "gold", "green", "blue", "purple", "grey"};
    const char* const* table = nullptr;
    int count = 0;
    switch (field) {
        case 2: table = kBody; count = 5; break;
        case 3: table = female ? kHairF : kHairM; count = 10; break;
        case 4: table = kHairColour; count = 10; break;
        case 5: table = female ? kFaceF : kFaceM; count = 10; break;
        case 6: table = kSkin; count = 8; break;
        case 7: table = kEyes; count = 8; break;
        default: return nullptr;
    }
    if (n < 1 || n > count) return nullptr;
    return table[n - 1];
}

void CreationLine(int field, const char* value, const char* gender, char* out, size_t cap)
{
    const char* look = CreateLook(field, value, gender);
    if (look) snprintf(out, cap, "%s: %s, %s", kCreateFields[field - 1], value, look);
    else      snprintf(out, cap, "%s: %s", kCreateFields[field - 1], value);
}

// ---- start flow: the shared grid control and the name keyboard ------------
//
// Gender is picked on a 1x2 grid over the same control the name keyboard
// later uses; fields 2..7 use picture grids there too, but their PLTT
// line already speaks the value, so only the gender shape is read from
// the grid itself.
bool GridOk(bool keyboard)
{
    uint32_t b = GRID_CTRL;
    if (u32(b + 0x20) != b + 0xC || u32(b + 0x24) != b + 8) return false;
    if (keyboard && (u32(b + 0x28) != b + 0x1C || u32(b + 0x2C) != b + 0x18)) return false;
    return true;
}
bool KeyboardShape()
{
    return GridOk(true) && u32(GRID_CTRL + 8) == 11 && u32(GRID_CTRL + 0xC) == 6;
}

// The name being typed, from whichever of the two buffer shapes the game
// is showing this frame (the two creation windows are drawn in turn):
// the dedicated "<X=n>name****" form, or the creation list's eighth line.
bool NameFromBuffer(const char* m, char* name, size_t cap)
{
    if (strncmp(m, "<X=", 3) == 0) {
        const char* p = m + 3;
        bool digits = false;
        while (*p >= '0' && *p <= '9') { p++; digits = true; }
        if (!digits || *p != '>') return false;
        p++;
        size_t n = 0;
        while (*p && *p != '*') {
            if (*p == '<' || n + 1 >= cap) return false;
            name[n++] = *p++;
        }
        while (*p == '*') p++;
        if (*p != 0) return false;
        name[n] = 0;
        return true;
    }
    int field; char val[64], gender[16];
    if (!ParseCreation(m, &field, val, sizeof(val), gender, sizeof(gender))) return false;
    if (field != 8) return false;
    size_t n = strlen(val);
    while (n > 0 && val[n - 1] == '*') val[--n] = 0;
    snprintf(name, cap, "%s", val);
    return true;
}

// The mod's kb_key tables, geometry live-verified (home key (48,64) is
// "2"; OK sits at (208,136)). Letter-case is deliberately NOT prefixed:
// the keyboard's case state is not stored in the control (a live diff of
// +0x00..+0x100 across a Capital-letters press changed no byte), and the
// mod knows the case only from BizHawk button events the Host does not
// expose. The name-so-far lines read the real buffer, case included.
bool KbKey(int x, int y, char* out, size_t cap)
{
    static const struct { int x, y; const char* name; } kSpecial[] = {
        {24, 96, "Small letters"}, {24, 112, "Capital letters"},
        {24, 136, "Change letters"}, {48, 136, "Accented letters"},
        {72, 136, "Symbols"}, {96, 136, "Space"},
        {160, 136, "question mark"}, {184, 136, "Delete"},
        {208, 136, "OK, finish the name"},
    };
    for (size_t i = 0; i < sizeof(kSpecial) / sizeof(kSpecial[0]); i++)
        if (x == kSpecial[i].x && y == kSpecial[i].y) {
            snprintf(out, cap, "%s", kSpecial[i].name);
            return true;
        }
    static const struct { int y, x0; const char* chars; } kRows[] = {
        {64, 32, "1234567890-="}, {80, 40, "qwertyuiop[]"},
        {96, 48, "asdfghjkl;'"}, {112, 56, "zxcvbnm,./"},
    };
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
        if (y != kRows[i].y || x < kRows[i].x0) continue;
        int idx = (x - kRows[i].x0 + 8) / 16;
        if (idx < 0 || idx >= (int) strlen(kRows[i].chars)) return false;
        char c = kRows[i].chars[idx];
        const char* spoken = nullptr;
        switch (c) {
            case '-':  spoken = "dash"; break;
            case '=':  spoken = "equals"; break;
            case '[':  spoken = "open bracket"; break;
            case ']':  spoken = "close bracket"; break;
            case ';':  spoken = "semicolon"; break;
            case '\'': spoken = "apostrophe"; break;
            case ',':  spoken = "comma"; break;
            case '.':  spoken = "full stop"; break;
            case '/':  spoken = "slash"; break;
        }
        if (spoken) snprintf(out, cap, "%s", spoken);
        else        snprintf(out, cap, "%c", c);
        return true;
    }
    if (y >= 130) {
        const char* name = x < 36 ? "Change letters" : x < 60 ? "Accented letters"
                         : x < 84 ? "Symbols" : x < 150 ? "Space"
                         : x < 172 ? "question mark" : x < 196 ? "Delete"
                         : "OK, finish the name";
        snprintf(out, cap, "%s", name);
        return true;
    }
    return false;
}

// ---- frame-poll state (all of it reset on attach and detach) --------------
int  g_frameNo = 0;
int  g_createStep = 0;        // creation field last seen highlighted (0 = none)
int  g_createStepFrame = 0;   // frame that step was seen on
// markup menu watcher
bool     g_menuUp = false;
uint32_t g_menuSig = 0, g_menuItemsSig = 0;
uint32_t g_menuCandSig = 0;
int      g_menuCandFrames = 0;
int      g_menuAbsent = 0;    // consecutive frames with no valid menu
bool     g_choiceWasUp = false;
// title watcher
int  g_titleIdx = -1;
int  g_titleSettle = 0;
int  g_titleCursorAnn = -1;
bool g_titleSpoke = false;
int  g_movieFrames = 0;
bool g_movieSpoke = false;
// creation watcher
int  g_createFieldAnn = -1;
char g_createValAnn[64] = "";
int  g_createCandField = -1;
char g_createCandVal[64] = "";
int  g_createCandFrames = 0;
char g_genderAnn[64] = "";
// name-entry watcher
bool g_nameUp = false;
char g_nameAnn[24] = "";
char g_nameCand[24] = "";
int  g_nameCandFrames = 0;
int  g_kbXAnn = -1, g_kbYAnn = -1;

void ResetAutoState()
{
    g_frameNo = 0;
    g_createStep = 0; g_createStepFrame = 0;
    g_menuUp = false; g_menuSig = 0; g_menuItemsSig = 0;
    g_menuCandSig = 0; g_menuCandFrames = 0; g_menuAbsent = 0;
    g_choiceWasUp = false;
    g_titleIdx = -1; g_titleSettle = 0; g_titleCursorAnn = -1; g_titleSpoke = false;
    g_movieFrames = 0; g_movieSpoke = false;
    g_createFieldAnn = -1; g_createValAnn[0] = 0;
    g_createCandField = -1; g_createCandVal[0] = 0; g_createCandFrames = 0;
    g_genderAnn[0] = 0;
    g_nameUp = false; g_nameAnn[0] = 0; g_nameCand[0] = 0; g_nameCandFrames = 0;
    g_kbXAnn = -1; g_kbYAnn = -1;
}

void SpeakMenuForm(const MenuView& v, oga::Priority pri)
{
    char line[128];
    if (v.cursor >= 0 && v.cursor < v.count)
        snprintf(line, sizeof(line), "Menu. %s. %d item%s.",
                 v.items[v.cursor], v.count, v.count == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "Menu. %d item%s.",
                 v.count, v.count == 1 ? "" : "s");
    Say(line, "menu", pri);
}

bool IsConfirmMenu(const MenuView& v)
{
    return v.count == 2 && strcmp(v.items[0], "Yes") == 0 && strcmp(v.items[1], "No") == 0;
}

// After name entry, creation's last step is a Yes/No menu whose question
// the game does not keep in the buffer; the mod supplies it, and so do
// we — but only in the tracked creation context (step 8 armed within the
// last few seconds, no game running), never for an ordinary in-game
// Yes/No, and never for the title's own delete confirmation.
bool CreationConfirm(const MenuView& v)
{
    return IsConfirmMenu(v) && g_createStep == 8 && !Dq9Ready()
        && g_frameNo - g_createStepFrame < 900;
}

void SpeakConfirmForm(const MenuView& v, oga::Priority pri)
{
    int cur = v.cursor >= 0 && v.cursor < v.count ? v.cursor : 0;
    char line[128];
    snprintf(line, sizeof(line), "Begin your adventure with this character? %s, %d of 2.",
             v.items[cur], cur + 1);
    Say(line, "menu", pri);
}

void CmdMenuState()
{
    // Content first, battle-phase second. A live title snapshot has the battle
    // flag SET with an empty menu buffer (the flag is meaningless pre-game):
    // checking the buffer first reads that as "No menu.", while a stale
    // buffer mid-battle still reads as busy via the phase rule below.
    char m[1200];
    const char* buf = MenuBuffer(m, sizeof(m)) ? m : nullptr;

    MenuView v;
    if (buf && ParseMenu(buf, &v)) {
        // Creation's confirm question replaces the generic list read.
        if (CreationConfirm(v)) { SpeakConfirmForm(v, oga::Priority::High); return; }
        // The busy rule, scoped to a battle the gate trusts (InRealBattle):
        // the flag is SET on title RAM, where a list must still read as a
        // list — menus are not gated on party readiness.
        if (InRealBattle() && u8(BATTLE_PHASE) != 3) { Say("Menu. Busy.", "menu", oga::Priority::High); return; }
        SpeakMenuForm(v, oga::Priority::High);
        return;
    }
    // Start flow, in the order the screens own the buffer: creation's
    // field list, then the name keyboard, then the title objects (which
    // stay valid in RAM long after the title is gone — see the note at
    // TitleObjOk — so they are only believed when nothing else claims
    // the screen and the buffer is empty, the mod's own guard).
    if (TitleContext()) {
        int field; char value[64], gender[16];
        if (buf && ParseCreation(buf, &field, value, sizeof(value), gender, sizeof(gender))) {
            char line[192];
            if (field == 8) {
                if (value[0]) snprintf(line, sizeof(line), "Name: %s.", value);
                else          snprintf(line, sizeof(line), "Name: nothing yet.");
            } else if (field == 1 && GridOk(false) && u32(GRID_CTRL + 8) == 1
                       && u32(GRID_CTRL + 0xC) == 2 && u32(GRID_CTRL + 0x1C) < 2) {
                uint32_t cy = u32(GRID_CTRL + 0x1C);
                snprintf(line, sizeof(line), "Gender: %s, %u of 2.",
                         cy == 0 ? "Male" : "Female", (unsigned) cy + 1);
            } else {
                CreationLine(field, value, gender, line, sizeof(line));
                size_t n = strlen(line);
                if (n && line[n - 1] != '.' && n + 1 < sizeof(line)) {
                    line[n] = '.'; line[n + 1] = 0;
                }
            }
            Say(line, "menu", oga::Priority::High);
            return;
        }
        char nm[24];
        if (buf && KeyboardShape() && NameFromBuffer(buf, nm, sizeof(nm))) {
            char line[96];
            if (nm[0]) snprintf(line, sizeof(line), "Name entry. Name so far: %s.", nm);
            else       snprintf(line, sizeof(line), "Name entry.");
            Say(line, "menu", oga::Priority::High);
            return;
        }
        if (!buf) {
            int idx = TitlePick();
            if (idx >= 0) {
                int cur = TitleCursor(idx);
                if (cur >= 0 && cur < TITLE_COUNT[idx]) {
                    char line[128];
                    snprintf(line, sizeof(line), "Menu. %s. %d item%s.",
                             TITLE_ITEMS[idx][cur], TITLE_COUNT[idx],
                             TITLE_COUNT[idx] == 1 ? "" : "s");
                    Say(line, "menu", oga::Priority::High);
                    return;
                }
            }
        }
    }
    // A yes/no prompt with no item list (the mod's EVENT_CHOICE). The 0/1 ->
    // Yes/No assignment is the mod's runtime knowledge, not in the addresses,
    // so the native side names the choice, never the cursor.
    if (ChoiceUp()) {
        Say("Choose. Yes or no.", "menu", oga::Priority::High);
        return;
    }
    Say("No menu.", "menu", oga::Priority::High);
}

// ---- frame polling: automatic announcements --------------------------------
//
// The mod polls every frame and speaks changes only; these watchers do
// the same. Every automatic line is Priority::Normal (the player did not
// ask), and every watcher debounces: the game writes half-built buffers
// for a frame, so nothing speaks until what it sees is identical two
// frames running (DbzMenuFrame's candidate discipline, and the mod's).
void PollMenu(const char* buf)
{
    // Battle rule: away from the command phase the buffer can hold a
    // pre-built list. Freeze the watcher whole — nothing announces, and
    // the absence clock does not run either.
    if (InRealBattle() && u8(BATTLE_PHASE) != 3) return;

    MenuView v;
    bool valid = buf && ParseMenu(buf, &v);
    if (!valid) {
        g_menuCandSig = 0; g_menuCandFrames = 0;
        // A menu is only "closed" once it has been gone a little while:
        // the game blanks the buffer for a frame between window redraws,
        // and a close-then-reopen inside that grace is one menu, not two.
        if (g_menuUp && ++g_menuAbsent >= 12) {
            g_menuUp = false; g_menuSig = 0; g_menuItemsSig = 0;
        }
        // The yes/no edge is watched only while no menu is up — a markup
        // menu takes precedence (it usually IS the choice).
        bool up = ChoiceUp();
        if (up && !g_choiceWasUp) Say("Choose. Yes or no.", "menu", oga::Priority::Normal);
        g_choiceWasUp = up;
        return;
    }
    g_menuAbsent = 0;

    uint32_t sig = MenuSig(v), isig = MenuItemsSig(v);
    if (sig == g_menuCandSig) g_menuCandFrames++;
    else { g_menuCandSig = sig; g_menuCandFrames = 1; }
    if (g_menuCandFrames < 2) return;         // not yet steady
    if (g_menuUp && sig == g_menuSig) return; // nothing changed

    if (g_menuUp && isig == g_menuItemsSig) {
        // Same list, new selection: the newly selected item only.
        if (v.cursor >= 0 && v.cursor < v.count) Say(v.items[v.cursor], "menu", oga::Priority::Normal);
        else SpeakMenuForm(v, oga::Priority::Normal);
    } else if (CreationConfirm(v)) {
        SpeakConfirmForm(v, oga::Priority::Normal);
    } else {
        SpeakMenuForm(v, oga::Priority::Normal);
    }
    g_menuUp = true; g_menuSig = sig; g_menuItemsSig = isig;
}

void PollTitle(const char* buf)
{
    // The mod's guard: while the markup buffer holds anything (creation's
    // list, the name shape, a Yes/No), the title is not what is on
    // screen, whatever the abandoned title objects still say.
    if (!TitleContext() || buf) {
        g_titleIdx = -1; g_titleSettle = 0; g_titleSpoke = false; g_titleCursorAnn = -1;
        g_movieFrames = 0; g_movieSpoke = false;
        return;
    }
    int idx = TitlePick();
    if (idx < 0) {
        g_titleIdx = -1; g_titleSettle = 0; g_titleSpoke = false; g_titleCursorAnn = -1;
        // Script objects are up but neither is a menu: the opening movie.
        if ((u32(TITLE_OBJ[0]) || u32(TITLE_OBJ[1])) && u8(MENU_MARKUP) == 0) {
            if (!g_movieSpoke && ++g_movieFrames >= 90) {
                Say("Opening movie. Press Start to begin your adventure.",
                    "title", oga::Priority::Normal);
                g_movieSpoke = true;
            }
        } else g_movieFrames = 0;
        return;
    }
    g_movieFrames = 0;
    int cur = TitleCursor(idx);
    if (idx != g_titleIdx) {
        g_titleIdx = idx; g_titleSettle = 0; g_titleSpoke = false; g_titleCursorAnn = -1;
    }
    if (cur < 0 || cur >= TITLE_COUNT[idx]) return;   // cursor not settled yet
    g_titleSettle++;
    if (!g_titleSpoke) {
        // The mod lets the menu settle 40 frames before the first line.
        if (g_titleSettle >= 40) {
            char line[128];
            snprintf(line, sizeof(line), "Title screen. %s", TITLE_ITEMS[idx][cur]);
            Say(line, "title", oga::Priority::Normal);
            g_titleSpoke = true; g_titleCursorAnn = cur;
        }
        return;
    }
    if (cur != g_titleCursorAnn) {
        Say(TITLE_ITEMS[idx][cur], "title", oga::Priority::Normal);
        g_titleCursorAnn = cur;
    }
}

void PollCreation(const char* buf)
{
    int field; char value[64], gender[16];
    if (!buf || !ParseCreation(buf, &field, value, sizeof(value), gender, sizeof(gender))) {
        g_createCandField = -1; g_createCandFrames = 0;
        return;
    }
    g_createStep = field;
    g_createStepFrame = g_frameNo;
    if (field == g_createCandField && strcmp(value, g_createCandVal) == 0) g_createCandFrames++;
    else {
        g_createCandField = field;
        snprintf(g_createCandVal, sizeof(g_createCandVal), "%s", value);
        g_createCandFrames = 1;
    }
    if (g_createCandFrames < 2) return;
    if (field == g_createFieldAnn && strcmp(value, g_createValAnn) == 0) return;
    g_createFieldAnn = field;
    snprintf(g_createValAnn, sizeof(g_createValAnn), "%s", value);
    if (field == 1 || field == 8) return;   // gender: the grid speaks; name: the keyboard speaks
    char line[192];
    CreationLine(field, value, gender, line, sizeof(line));
    Say(line, "create", oga::Priority::Normal);
}

void PollGridGender()
{
    char line[64] = "";
    if (TitleContext() && GridOk(false) && u32(GRID_CTRL + 8) == 1
        && u32(GRID_CTRL + 0xC) == 2 && u32(GRID_CTRL + 0x1C) < 2) {
        uint32_t cy = u32(GRID_CTRL + 0x1C);
        snprintf(line, sizeof(line), "Gender: %s, %u of 2",
                 cy == 0 ? "Male" : "Female", (unsigned) cy + 1);
    }
    if (!line[0]) { g_genderAnn[0] = 0; return; }
    if (strcmp(line, g_genderAnn) != 0) {
        snprintf(g_genderAnn, sizeof(g_genderAnn), "%s", line);
        Say(line, "create", oga::Priority::Normal);
    }
}

void PollNameEntry(const char* buf)
{
    char name[24];
    bool up = buf && KeyboardShape() && NameFromBuffer(buf, name, sizeof(name));
    if (!up) {
        if (g_nameUp) { g_nameUp = false; g_kbXAnn = -1; g_kbYAnn = -1; }
        g_nameCandFrames = 0;
        return;
    }
    int kx = (int) u16(GRID_CTRL + 0x60), ky = (int) u16(GRID_CTRL + 0x62);
    char key[48] = "";
    KbKey(kx, ky, key, sizeof(key));
    if (!g_nameUp) {
        g_nameUp = true;
        snprintf(g_nameAnn, sizeof(g_nameAnn), "%s", name);
        g_nameCandFrames = 0;
        g_kbXAnn = kx; g_kbYAnn = ky;
        char line[224];
        int n = snprintf(line, sizeof(line), "Name entry. ");
        if (name[0]) n += snprintf(line + n, sizeof(line) - (size_t) n, "Name so far: %s. ", name);
        n += snprintf(line + n, sizeof(line) - (size_t) n, "Move with the D-pad, A types a letter.");
        if (key[0]) n += snprintf(line + n, sizeof(line) - (size_t) n, " %s", key);
        Say(line, "name", oga::Priority::Normal);
        return;
    }
    // The name itself: speak the edit once the new text is steady. The
    // buffer's words are the truth here, letter case included.
    if (strcmp(name, g_nameAnn) != 0) {
        if (strcmp(name, g_nameCand) == 0) g_nameCandFrames++;
        else {
            snprintf(g_nameCand, sizeof(g_nameCand), "%s", name);
            g_nameCandFrames = 1;
        }
        if (g_nameCandFrames >= 2) {
            char line[128];
            size_t oldLen = strlen(g_nameAnn), newLen = strlen(name);
            if (newLen > oldLen && strncmp(name, g_nameAnn, oldLen) == 0)
                snprintf(line, sizeof(line), "%s, name is now %s.", name + oldLen, name);
            else if (newLen < oldLen && strncmp(g_nameAnn, name, newLen) == 0)
                snprintf(line, sizeof(line), "Deleted, name is now %s.", name[0] ? name : "empty");
            else
                snprintf(line, sizeof(line), "Name is now %s.", name[0] ? name : "empty");
            Say(line, "name", oga::Priority::Normal);
            snprintf(g_nameAnn, sizeof(g_nameAnn), "%s", name);
        }
    } else g_nameCandFrames = 0;
    // Cursor moves echo the key under the cursor at once (no debounce —
    // the cursor is already where the player put it).
    if (kx != g_kbXAnn || ky != g_kbYAnn) {
        g_kbXAnn = kx; g_kbYAnn = ky;
        if (key[0]) Say(key, "name", oga::Priority::Normal);
    }
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
    ResetAutoState();
    return true;   // the game code check already happened in the registry
}

static void dq9_on_frame(void)
{
    if (!g_host) return;
    g_frameNo++;
    char m[1200];
    const char* buf = MenuBuffer(m, sizeof(m)) ? m : nullptr;
    PollTitle(buf);
    PollCreation(buf);
    PollGridGender();
    PollNameEntry(buf);
    PollMenu(buf);
}

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

static bool dq9_ready(void)
{
    if (Dq9Ready()) return true;
    if (!g_host) return false;
    // Start flow: a screen the player can act on is a ready adapter, even
    // before any party exists — otherwise the host will not even dispatch
    // the MenuState query that reads the title (poke_command gates every
    // command on ready()). The party and location commands keep their own
    // gates and still refuse here; only menus widen.
    char m[1200];
    if (MenuBuffer(m, sizeof(m))) {
        MenuView v;
        if (ParseMenu(m, &v)) return true;
        int field; char value[64], gender[16];
        if (ParseCreation(m, &field, value, sizeof(value), gender, sizeof(gender))) return true;
        char nm[24];
        if (KeyboardShape() && NameFromBuffer(m, nm, sizeof(nm))) return true;
    }
    if (TitleContext() && TitlePick() >= 0) return true;
    return ChoiceUp();
}

static void dq9_detach(void)
{
    g_host = nullptr;
    g_cursor = -1;
    g_objcursor = -1;
    ResetAutoState();
}

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
    nullptr,  // cue_snapshot
    "Menu, party and location speech",
    "Menus announce themselves as they open and as the cursor moves, "
    "and are spoken with their item counts when asked. The title "
    "menus, character creation and the name-entry keyboard are read "
    "from the first screen. Where Am I speaks the map code and your "
    "position on it, and the adapter can report your party and who "
    "is nearby.\n"
    "Supported version: Dragon Quest IX: Sentinels of the Starry "
    "Skies, USA (game code YDQE).",
};

} // namespace oga
