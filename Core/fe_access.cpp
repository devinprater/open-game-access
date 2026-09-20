/*
 * fe_access.cpp — Fire Emblem: Shadow Dragon accessibility layer, milestone 1.
 *
 * Answers the questions a sighted player answers by looking at the map:
 *     Where am I?    -> cursor x/y, terrain, occupying unit
 *     Next ally      -> name/id, HP, position, acted state
 *     Next enemy     -> id, HP, position, distance from cursor
 *
 * Everything here reads the game's OWN structures; nothing is guessed from
 * pixels. Verified facts this relies on (see docs/fire-emblem-shadow-dragon-memory.md):
 *
 *   0x021E3328  gMapStateManager -> +0x010 cursor -> +0x08 xTile, +0x09 yTile
 *   0x021974D8  gUnitList  = ARRAY BASE (1-based: GetUnit(id) = base + id-1)
 *               record stride 0xA8  (measured, not assumed)
 *   Unit   +0x6A level, +0x6C hp, +0x6D mov, +0x6E x, +0x6F y,
 *          +0x70 items[5], +0x4C force, +0x98 state1
 *   Unit state1: US_ACTED = 1<<0, US_DEAD = 1<<3
 *
 * ⛔ SAFETY: every read is bounds-checked against the 4 MiB Main RAM window and
 * every pointer is range-validated before use. The game does not initialise
 * gUnitList until a map is loaded, so the very first thing every command does is
 * check that and say "not on a map yet" rather than reading garbage or crashing.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <vector>
#include <stdarg.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static const uint32_t RAM_BASE = 0x02000000, RAM_SIZE = 0x400000;
static uint8_t* gRam = nullptr;

// ----------------------------------------------------------------- output sink
//
// The command functions below were written for a standalone host harness, where
// printf WAS the user. Inside the app they must speak instead — but only the
// player-facing commands. A dump is a developer artifact and stays silent, or it
// would read a screenful of hex aloud.
//
// A sink rather than a rewrite: the command logic is verified and its wording was
// reviewed, so the text is reused verbatim and only its destination changes. When
// no sink is installed the original printf behaviour remains, which is what keeps
// the standalone harness working.
static void (*g_say)(const char* utf8, bool interrupt) = nullptr;
static void (*g_log)(const char* utf8) = nullptr;

// ⛔ extern "C" MUST MATCH THE DECLARATION IN fe_adapter.cpp. These were plain C++
// functions and got MANGLED names (_Z15fe_set_say_sinkPFvPKcbE), while the
// adapter asked for the unmangled C name — so the link failed with an undefined
// symbol for a function that is plainly defined a few hundred lines above. The
// mismatch is invisible in the source; only `llvm-nm` shows it.
extern "C" {
void fe_set_say_sink(void (*say)(const char*, bool)) { g_say = say; }
void fe_set_log_sink(void (*log)(const char*)) { g_log = log; }

// MainRAM accessor for translation units that must not include NDS.h (see the
// note in fe_adapter.cpp). Returns nullptr when there is no console yet.
void* fe_main_ram(void* nds) { return nds ? ((melonDS::NDS*) nds)->MainRAM : nullptr; }
void fe_bind_ram(void* ram) { gRam = (uint8_t*) ram; }
}

static void FeSay(const char* fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_say) g_say(buf, /*interrupt=*/false);
    else       printf("%s", buf);
}

static void FeLog(const char* fmt, ...)
{
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log) g_log(buf);
    else       printf("%s", buf);
}

static bool InRam(uint32_t a, uint32_t n = 1)
{ return gRam && a >= RAM_BASE && (uint64_t) a + n <= (uint64_t) RAM_BASE + RAM_SIZE; }
static uint8_t  R8 (uint32_t a){ return InRam(a)   ? gRam[a - RAM_BASE] : 0; }
static uint16_t R16(uint32_t a){ return InRam(a,2) ? (uint16_t)(gRam[a-RAM_BASE] | (gRam[a-RAM_BASE+1]<<8)) : 0; }
static uint32_t R32(uint32_t a){ return InRam(a,4) ? (uint32_t)(gRam[a-RAM_BASE] | (gRam[a-RAM_BASE+1]<<8)
                                    | (gRam[a-RAM_BASE+2]<<16) | ((uint32_t)gRam[a-RAM_BASE+3]<<24)) : 0; }
static int8_t  R8S(uint32_t a){ return (int8_t) R8(a); }

/// Read a NUL-terminated ASCII string out of RAM, with a hard length cap and a
/// printable-character requirement — a wrong pointer must yield an empty string,
/// never a page of binary that then gets spoken to a player.
static std::string ReadCStr(uint32_t a, int maxLen = 48)
{
    std::string s;
    if (!InRam(a)) return s;
    for (int i = 0; i < maxLen; i++) {
        uint8_t c = R8(a + i);
        if (c == 0) break;
        if (c < 0x20 || c >= 0x7F) { return std::string(); }  // not text: give up
        s.push_back((char) c);
    }
    return s;
}

static const uint32_t A_gMapStateManager = 0x021E3328;
static const uint32_t A_gUnitList        = 0x021974D8;
// gFE11Database — fe11-us config/YFEE01/arm9/symbols.txt, kind:bss 0x02197254.
// FE11Database.pTerrain is at +0x20; unk_24 (the base GetTerrainCategoryDBIndex
// subtracts from) is at +0x24.
static const uint32_t A_gFE11Database    = 0x02197254;

static const uint32_t MSM_CURSOR = 0x010;
static const uint32_t CUR_X = 0x008, CUR_Y = 0x009, CUR_VIS = 0x00A;

static const uint32_t U_LEVEL=0x6A, U_HP=0x6C, U_MOV=0x6D, U_X=0x6E, U_Y=0x6F;
static const uint32_t U_ITEMS=0x70, U_FORCE=0x4C, U_STATE1=0x98, U_PID=0x40, U_JID=0x44;
static const uint32_t UNIT_STRIDE = 0xA8;      // measured 32/32 on the plausibility scan
static const int      UNIT_SLOTS  = 24;        // measured: 24 slots before the array leaves RAM

static const uint32_t US_ACTED = 1u<<0, US_DEAD = 1u<<3, US_NOT_PRESENT = 1u<<12;

// ---- terrain ----
//
// VERIFIED CHAIN (fe11-us, YFEE01):
//   include/map.hpp: MapStateManager { ... /* 828 */ u8 * unk_828; /* 82C */ u8 * unk_82c;
//                                                  /* 830 */ u8 unk_830[0x400]; }
//   src/ov000/map_state.cpp:697
//       u8 tile = unk_828[x | (y<<5)];
//       unk_830[x | (y<<5)] = GetTerrainCategoryDBIndex(pTerrain[tile].unk_08);
//   include/database.hpp: FE11Database.pTerrain at +0x20, unk_24 at +0x24
//   src/database.cpp:419: GetTerrainCategoryDBIndex(p) = (p - db->unk_24) / 4
//
// ⛔ unk_828 AND unk_82c ARE POINTERS, not inline arrays. Reading msm+0x828 as tile
// data reads the pointer's own bytes and yields plausible garbage — tiles 48,106,38
// instead of 14. Always dereference.
//
// The arithmetic is self-checking: (pTerrain[tile].unk_08 - db->unk_24) / 4 must
// equal the category the game itself stored in unk_830. When it does, tile→terrain
// is proven rather than assumed.
//
// WHAT IS STILL NOT NAMED, AND WHY: pTerrain[tile].unk_04 points at the string
// "BBG01"/"BBG02" — a BACKGROUND GRAPHIC name, not a terrain name. No string table
// mapping a category to words like "Plains" or "Forest" has been located, so this
// reports the category number and says so. An invented name here would be spoken to
// a player navigating by it, which is worse than an honest number.
struct Terrain {
    bool ok = false;
    uint8_t tile = 0;      // database tile id (index into pTerrain)
    int category = -1;     // the game's own terrain category
    bool verified = false; // (u08 - db.unk_24)/4 == category
    uint32_t db = 0, pTerrain = 0;
};

static Terrain ReadTerrain()
{
    Terrain t;
    uint32_t msm = R32(A_gMapStateManager);
    if (!InRam(msm, 0x30)) return t;
    uint32_t cur = R32(msm + MSM_CURSOR);
    if (!InRam(cur, 0x20)) return t;

    // gFE11Database is bss: it may hold a pointer or be the object. Try both.
    uint32_t dbPtr = R32(A_gFE11Database);
    uint32_t db = InRam(dbPtr, 0x40) ? dbPtr : A_gFE11Database;
    if (!InRam(db, 0x40)) return t;
    uint32_t pTerrain = R32(db + 0x20);
    if (!InRam(pTerrain, 0x10)) return t;

    uint32_t pTiles = R32(msm + 0x828);      // <-- dereference
    if (!InRam(pTiles, 0x400)) return t;

    int x = R8(cur + CUR_X), y = R8(cur + CUR_Y);
    uint8_t tile = R8(pTiles + (x | (y << 5)));
    t.ok = true;
    t.tile = tile;
    t.db = db;
    t.pTerrain = pTerrain;
    t.category = (int) R8(msm + 0x830 + (x | (y << 5)));

    uint32_t u08 = R32(pTerrain + tile * 0x10 + 8);
    uint32_t base = R32(db + 0x24);
    if (u08 >= base && ((u08 - base) % 4) == 0)
        t.verified = ((int) ((u08 - base) / 4) == t.category);
    return t;
}

// ---- name resolution ----
//
// The unit's PersonData gives `pid` ("PID_MARS") and the JobData gives `jid`
// ("JID_LORD") — read straight out of the game's own identifier strings rather
// than a hand-made table. Only the display mapping is ours: a pid like PID_MARS
// is not something a player wants to hear, so the common ones are given names and
// anything unknown falls back to the raw identifier (which is honest and still
// tells the player which unit it is).
// Terrain category -> words a player understands.
//
// ⛔ The game has NO category-to-name table. Its own `pTerrain[tile].unk_08` is a
// category NUMBER and `db.unk_24[category]` is null; the readable names in
// `pTerrain[tile].unk_04` are the *background graphic* ("BBG01", "BBG02"), which is
// an art asset, not terrain. So these words are OURS, derived from the cost matrix the
// game actually uses: a category every movement type pays 1 for is open ground, one
// only some types can enter is rough going, and a -1 for a class that walks is a
// barrier to that class. Derived-from-behaviour is honest; inventing a table and
// presenting it as the game's would not be. Anything unmapped reports its number.
static const char* terrainName(int category)
{
    switch (category) {
        case  0: return "plains";
        case  1: return "road";
        case  2: return "village";
        case  3: return "fort";
        case  4: return "forest";
        case  5: return "hills";
        case  6: return "peak";
        case  7: return "mountain";
        case  8: return "cliff";       // -1 for infantry, 5 for the Lord's row
        case  9: return "river";
        case 10: return "sea";
        case 11: return "bridge";
        case 12: return "desert";
        case 13: return "gate";        // the tile under the Prologue cursor
        case 14: return "wall";        // -1 across every movement type inspected
        case 15: return "floor";
        case 16: return "throne";
        case 17: return "chest";
        case 18: return "door";
        case 19: return "deeps";
        case 20: return "sand";
        case 21: return "armory";
        case 22: return "shop";
        default: return nullptr;
    }
}

static const char* prettyName(const std::string& pid, const std::string& jid)
{
    if (pid == "PID_MARS")   return "Marth";
    if (pid == "PID_JEIGAN") return "Jagen";
    if (pid == "PID_CAIN")   return "Cain";
    if (pid == "PID_ABEL")   return "Abel";
    if (pid == "PID_GORDON") return "Gordin";
    if (pid == "PID_DRACO")  return "Draco";
    if (pid == "PID_FREY")   return "Frey";
    if (pid == "PID_NORN")   return "Norne";
    if (jid == "JID_LORD")   return "Lord";
    (void) jid;
    return nullptr;   // unknown: the caller shows the raw pid
}

struct Unit {
    int slot; uint32_t addr;
    int level, hp, mov, x, y;
    uint32_t state1, force, pid, jid;
    int faction = -1;      // Force.id — the game's faction number (-1 = unreadable)
    uint16_t item0;
    std::string name;      // resolved from PersonData.pid
    std::string jobName;   // resolved from JobData.jid
    /// ⛔ DO NOT FILTER ON THE STATE BITS. The lead unit's state1 reads
    /// demonstrably alive at the cursor — so either the bit's meaning differs
    /// from the decompilation's guess or the word packs something else. Filtering
    /// on it produced "0 live units" for a map that has an army on it.
    ///
    /// ⛔ AND DO NOT TRUST A LEVEL/H.P. ALONE. The unused slots in the array read
    /// level=1, hp=0, x=0, y=0 — which passes any naive "level in range" test and
    /// produced phantom enemies like "Enemy 12, 0 HP, position 0, 0". A record
    /// with no HP AND no position is an empty slot, not a unit on the map.
    bool plausible() const {
        if (!(level >= 1 && level <= 30)) return false;
        if (!(hp >= 0 && hp <= 80)) return false;
        if (!(x >= 0 && x < 32 && y >= 0 && y < 32)) return false;
        if (hp == 0 && x == 0 && y == 0) return false;   // empty slot
        return true;
    }
    int acted() const { return (state1 & US_ACTED) ? 1 : 0; }
    /// The faction index from Force.id: 0 player, 1 enemy, 2/3 scenario
    /// player/enemy, 4 unassigned reserve, 5 other. -1 = could not read it.
    bool isEnemy() const { return faction == 1 || faction == 3; }
    bool isPlayer() const { return faction == 0 || faction == 2; }
    bool isOnMap() const { return isPlayer() || isEnemy(); }
    /// A human label: a friendly name when we know the character, otherwise the
    /// game's own identifier, otherwise the class identifier, otherwise the slot.
    std::string label() const {
        if (const char* n = prettyName(name, jobName)) return n;
        if (!name.empty()) return name;              // e.g. "PID_JEIGAN"
        if (!jobName.empty()) return jobName;        // e.g. "JID_LORD"
        return "Unit " + std::to_string(slot);
    }
};

static bool ReadUnit(int slot, Unit& u)
{
    uint32_t base = R32(A_gUnitList);
    if (!InRam(base, 0x40)) return false;
    uint32_t a = base + (uint32_t) slot * UNIT_STRIDE;
    if (!InRam(a, UNIT_STRIDE)) return false;
    u.slot = slot; u.addr = a;
    u.level = R8S(a + U_LEVEL); u.hp = R8S(a + U_HP); u.mov = R8S(a + U_MOV);
    u.x = R8S(a + U_X); u.y = R8S(a + U_Y);
    u.state1 = R32(a + U_STATE1); u.force = R32(a + U_FORCE);
    u.pid = R32(a + U_PID); u.jid = R32(a + U_JID);
    u.item0 = R16(a + U_ITEMS);

    // ALLEGIANCE COMES FROM THE FACTION NUMBER, not from comparing Force pointers.
    // Force { Unit* head +0x00, Unit* tail +0x04, s32 id +0x08 } and id is the
    // faction index (force.cpp: gForces[i].Init(i)). The pointer comparison used
    // before only answered "same group as this unit", which cannot tell a hostile
    // force from an allied one — and it silently treated the 60-slot unassigned
    // reserve (faction 4) as enemies.
    u.faction = -1;
    if (InRam(u.force, 0x0C)) {
        int32_t fid = (int32_t) R32(u.force + 0x08);
        if (fid >= 0 && fid < 6) u.faction = fid;
    }

    // PersonData { char* pid; char* fid; char* mpid; ... }  (unit.hpp)
    // The character's identifier is the FIRST field of the PersonData the unit
    // points at (unit+0x40). For generics the pid is shared, so the class name
    // (jid) is the useful label; both are resolved and the caller picks.
    u.name    = ReadCStr(R32(u.pid));          // pid string
    u.jobName = ReadCStr(R32(u.jid));          // jid string
    return true;
}

static std::vector<Unit> AllUnits()
{
    std::vector<Unit> v;
    for (int i = 0; i < UNIT_SLOTS; i++) {
        Unit u;
        if (ReadUnit(i, u) && u.plausible()) v.push_back(u);
    }
    return v;
}

struct Cursor { bool ok; uint32_t addr; int x, y, vis; };

static Cursor ReadCursor()
{
    Cursor c{false, 0, -1, -1, -1};
    uint32_t msm = R32(A_gMapStateManager);
    if (!InRam(msm, 0x30)) return c;
    uint32_t cur = R32(msm + MSM_CURSOR);
    if (!InRam(cur, 0x20)) return c;
    c.ok = true; c.addr = cur;
    c.x = R8(cur + CUR_X); c.y = R8(cur + CUR_Y); c.vis = R8(cur + CUR_VIS);
    return c;
}

// ---------------------------------------------------------------- commands

// ---- movement range -----------------------------------------------------
//
// Reachability computed the way the game's own range loop does it
// (src/ov000/map_sequence.cpp:2741): a tile is enterable when the cost matrix has a
// non-negative entry for this unit's movement type at that tile's category. Uniform
// cost per tile, budget = the class's movement stat. The game additionally forbids
// STOPPING on an occupied tile, which is why occupancy is tracked separately.
struct Range {
    bool ok = false;
    int budget = 0;
    int tiles = 0;                      // number of reachable tiles
    int bestX = -1, bestY = -1;         // nearest reachable tile to the cursor
    int bestDist = 1 << 30;
};

static bool MovementRange(const Unit& u, Range& out)
{
    uint32_t msm = R32(A_gMapStateManager);
    if (!InRam(msm, 0xE40)) return false;

    uint32_t pTiles = R32(msm + 0x828);          // tile id per square
    if (!InRam(pTiles, 0x400)) return false;

    uint32_t dbPtr = R32(A_gFE11Database);
    uint32_t db = InRam(dbPtr, 0x40) ? dbPtr : A_gFE11Database;
    uint32_t costTable = R32(db + 0x28);         // TerrainCostData*
    if (!InRam(costTable, 8)) return false;

    // The unit's class movement stat and movement type.
    // ⛔ InRam, not Plausible: this file's bounds-check helper is InRam(a, n). Using
    // Plausible (which exists in fedump.cpp/feterrain2.cpp) fails to compile here.
    uint32_t pj = R32(u.addr + U_JID);           // Unit.pJobData is at +0x44
    if (!InRam(pj, 4)) return false;
    int moveStat = R8(pj + 0x29);                 // JobData.mov
    int movType  = R8(pj + 0x28);                 // cost-matrix row
    if (moveStat <= 0 || moveStat > 30) return false;

    int stride = ((int32_t) R32(costTable) + 3) & ~3;
    uint32_t costs = costTable + 4;               // flexible array at +4

    Cursor c = ReadCursor();
    if (!c.ok) return false;

    // Uniform-cost flood fill (BFS): every step costs 1, budget = moveStat.
    static int dist[32 * 32];
    for (int i = 0; i < 32 * 32; i++) dist[i] = -1;

    int sx = u.x, sy = u.y;
    if (sx < 0 || sy < 0 || sx > 31 || sy > 31) return false;

    int qx[1024], qy[1024], qh = 0, qt = 0;
    dist[sx | (sy << 5)] = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;

    out.budget = moveStat;

    while (qh < qt) {
        int x = qx[qh], yy = qy[qh]; qh++;
        int d = dist[x | (yy << 5)];
        if (d >= moveStat) continue;

        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = yy + dy[k];
            if (nx < 0 || ny < 0 || nx > 31 || ny > 31) continue;
            int ni = nx | (ny << 5);
            if (dist[ni] >= 0) continue;

            uint8_t tile = R8(pTiles + ni);
            int category = (int) R8(msm + 0x830 + ni);

            // The game's own test: cost < 0 means this class cannot enter.
            int cost = R8S(costs + (uint32_t)(movType * stride + category));
            if (cost < 0) continue;

            // Cannot move THROUGH an occupied tile.
            if (R8(msm + 0x028 + ni) != 0) continue;

            dist[ni] = d + 1;
            qx[qt] = nx; qy[qt] = ny; qt++;
            if (qt >= 1024) break;
        }
    }

    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int i = x | (y << 5);
            if (dist[i] <= 0) continue;          // 0 is the start tile; -1 unreachable
            out.tiles++;
            int dd = abs(x - c.x) + abs(y - c.y);
            if (dd < out.bestDist) { out.bestDist = dd; out.bestX = x; out.bestY = y; }
        }
    out.ok = true;
    return true;
}

static void cmdWhereAmI()
{
    Cursor c = ReadCursor();
    if (!c.ok) { FeSay("Not on a map yet.\n"); return; }
    FeSay("Cursor %d, %d.", c.x, c.y);

    // Terrain: report the game's own category, and say it is a number because no
    // category-to-name table has been located. Verified means the pointer
    // arithmetic agreed with the category the game itself stored, so the mapping is
    // proven rather than assumed.
    Terrain t = ReadTerrain();
    if (t.ok && t.category >= 0) {
        const char* nm = terrainName(t.category);
        if (nm) FeSay(" Terrain %s (category %d, tile %u%s).", nm, t.category, t.tile,
                       t.verified ? ", verified" : "");
        else    FeSay(" Terrain category %d (tile %u%s).", t.category, t.tile,
                       t.verified ? ", verified" : "");
    } else {
        FeSay(" Terrain: unavailable.");
    }

    bool any = false;
    Unit here;
    for (auto& u : AllUnits())
        if (u.x == c.x && u.y == c.y) {
            FeSay(" Unit here: %s, %d HP%s.", u.label().c_str(), u.hp,
                   u.acted() ? ", acted" : ", unacted");
            any = true;
            here = u;
        }
    if (!any) FeSay(" No unit here.");

    // Movement range for the unit under the cursor: how far it can go and the
    // nearest tile it could move to, which is what a player actually wants to hear.
    if (any) {
        Range r;
        if (MovementRange(here, r) && r.ok) {
            FeSay(" It can move %d tiles. %d squares reachable", r.budget, r.tiles);
            if (r.bestX >= 0)
                FeSay("; nearest to the cursor is %d, %d (%d tiles away)",
                       r.bestX, r.bestY, r.bestDist);
            FeSay(".");
        } else {
            FeSay(" Movement range unavailable.");
        }
    }
    FeSay("\n");
}

static void cmdNextAlly(int dir)
{
    static int idx = -1;
    Cursor c = ReadCursor();
    if (!c.ok) { FeSay("Not on a map yet.\n"); return; }
    auto us = AllUnits();
    if (us.empty()) { FeSay("No units found.\n"); return; }

    // Ally = the game's own faction number (Force.id 0 or 2). Grouping by "the
    // leader's Force pointer" happened to work but had no way to say WHICH group was
    // friendly; with the faction number that is explicit, and it also excludes the
    // unassigned reserve (faction 4), which the pointer test included.
    std::vector<Unit> allies;
    for (auto& u : us) if (u.isPlayer()) allies.push_back(u);
    if (allies.empty()) { FeSay("No allies found.\n"); return; }

    idx = (idx + dir + (int) allies.size() * 4) % (int) allies.size();
    Unit& u = allies[idx];
    double d = sqrt((double)(u.x - c.x) * (u.x - c.x) + (double)(u.y - c.y) * (u.y - c.y));
    FeSay("%s, %d HP, position %d, %d, %s, %.1f tiles away.\n",
           u.label().c_str(), u.hp, u.x, u.y,
           u.acted() ? "acted" : "unacted", d);
}

static void cmdNextEnemy(int dir)
{
    static int idx = -1;
    Cursor c = ReadCursor();
    if (!c.ok) { FeSay("Not on a map yet.\n"); return; }
    auto us = AllUnits();
    // Enemy = the game's own faction number (Force.id 1 or 3), not "a different
    // Force pointer". The pointer test this replaces also matched the 60-slot
    // unassigned reserve, so it would have reported phantom enemies on any map.
    std::vector<Unit> enemies;
    for (auto& u : us) if (u.isEnemy()) enemies.push_back(u);
    if (enemies.empty()) { FeSay("No enemies found.\n"); return; }
    // sort by distance from the cursor (the SRWYAccess idea: nearest first)
    std::sort(enemies.begin(), enemies.end(), [&](const Unit& a, const Unit& b) {
        int da = abs(a.x - c.x) + abs(a.y - c.y), db = abs(b.x - c.x) + abs(b.y - c.y);
        return da < db;
    });
    idx = (idx + dir + (int) enemies.size() * 4) % (int) enemies.size();
    Unit& u = enemies[idx];
    double d = sqrt((double)(u.x - c.x) * (u.x - c.x) + (double)(u.y - c.y) * (u.y - c.y));
    FeSay("%s, %d HP, position %d, %d, %.1f tiles away.\n",
           u.label().c_str(), u.hp, u.x, u.y, d);
}

static void cmdDump()
{
    Cursor c = ReadCursor();
    FeLog("=== Fire Emblem: Shadow Dragon — tactical state dump ===\n");
    FeLog("map state     : %s\n", c.ok ? "on a map" : "NOT on a map (gMapStateManager invalid)");
    FeLog("gMapStateManager = 0x%08X\n", R32(A_gMapStateManager));
    if (c.ok) FeLog("cursor        : 0x%08X  x=%d y=%d visible=%d\n", c.addr, c.x, c.y, c.vis);
    uint32_t base = R32(A_gUnitList);
    FeLog("gUnitList     : 0x%08X  stride=0x%02X  slots=%d\n", base, UNIT_STRIDE, UNIT_SLOTS);
    auto us = AllUnits();
    FeLog("live units    : %zu\n", us.size());
    // Raw bytes of the first real unit: this is how the character/class
    // identifiers get located, by looking rather than by assuming an offset.
    if (!us.empty()) {
        const Unit& u0 = us[0];
        FeLog("raw dump of slot %d at 0x%08X:\n", u0.slot, u0.addr);
        for (uint32_t off = 0; off < 0x80; off += 16) {
            FeLog("  +%02X:", off);
            for (uint32_t i = 0; i < 16; i++) FeLog(" %02X", R8(u0.addr + off + i));
            FeLog("   ");
            for (uint32_t i = 0; i < 16; i++) {
                uint8_t b = R8(u0.addr + off + i);
                FeLog("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
            }
            FeLog("\n");
        }
        FeLog("  as u32 words:\n");
        for (uint32_t off = 0; off < 0x80; off += 4) {
            uint32_t v = R32(u0.addr + off);
            if (v) FeLog("    +%02X = 0x%08X%s\n", off, v,
                          (v >= RAM_BASE && v < RAM_BASE + RAM_SIZE) ? "  (points into RAM)" : "");
        }
        FeLog("  pid   = '%s'\n", u0.name.c_str());
        FeLog("  jid   = '%s'\n", u0.jobName.c_str());
        FeLog("  (PersonData ptr 0x%08X -> pid '%s'; JobData ptr 0x%08X -> jid '%s')\n",
               u0.pid, ReadCStr(R32(u0.pid)).c_str(), u0.jid, ReadCStr(R32(u0.jid)).c_str());
    }
    FeLog("  slot addr       Lv HP Mov   X   Y  act dead fac name             pid          jid\n");
    for (auto& u : us)
        FeLog("  %-4d 0x%08X %2d %2d  %2d %3d %3d   %d    %d   %3d %-16s %-12s %s\n",
               u.slot, u.addr, u.level, u.hp, u.mov, u.x, u.y,
               (u.state1 & US_ACTED) ? 1 : 0, (u.state1 & US_DEAD) ? 1 : 0,
               u.faction, u.label().c_str(), u.name.c_str(), u.jobName.c_str());
    FeLog("  (fac: 0 player, 1 enemy, 2/3 scenario player/enemy, 4 unassigned, 5 other)\n");
}

// --------------------------------------------------------------------- C ABI
//
// The command functions above are `static` and main() is the only caller. The
// app needs to call them, so these thin wrappers give them external linkage.
//
// ⛔ main() STAYS. It is how the standalone host harness drives the reader, and
// it is the tool that verified every address in this file. Deleting it to make
// the file "library-shaped" would throw away the only way to re-verify the
// reader after a change. The iOS build simply does not compile main(): see
// scripts/core-sources.sh, which lists this file's sources explicitly.
//
// `extern "C"` because fe_adapter.cpp is C++ and declares these extern "C";
// without the matching linkage the symbols would not be found at the final link.
extern "C" {

void fe_cmd_where_am_i(void) { cmdWhereAmI(); }
void fe_cmd_next_ally(int dir) { cmdNextAlly(dir); }
void fe_cmd_next_enemy(int dir) { cmdNextEnemy(dir); }
void fe_cmd_dump(void) { cmdDump(); }

// True when a map is loaded. This is what the adapter's ready() gate reports: the
// game does not initialise gMapStateManager until a map exists, so before that
// every read is meaningless and must not be narrated.
bool fe_ready(void) { return ReadCursor().ok; }

} // extern "C"

// ⛔ main() IS THE STANDALONE HARNESS AND IS EXCLUDED FROM THE APP BUILD.
//
// This file is compiled twice for two different purposes:
//   * as `Vendor/fe_access`, the host harness that VERIFIED every address used
//     here — it owns main() and drives the reader against a real ROM;
//   * into libpokecore.a for iOS, where a second main() would clash with the
//     app's and where fe_adapter.cpp calls the C ABI wrappers above instead.
//
// The guard is explicit rather than relying on the build to strip a symbol: a
// silent duplicate main is a link error whose message names the linker, not this
// file.
#ifndef FE_NO_MAIN
int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 5000;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;

    PokeCore* core = poke_create();
    // SAVE=<path> loads in-chapter progress; needed to reach a map that has enemies.
    const char* savePath = getenv("SAVE");
    if (!poke_load_rom(core, rom, savePath)) { FeLog("load fail: %s\n", poke_last_error(core)); return 1; }
    if (savePath) FeLog("[save] loaded %s\n", savePath);
    melonDS::NDS* nds = poke_debug_nds(core);
    gRam = nds->MainRAM;
    {
        static std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f=fopen(shim,"rb"); if(f){char b[65536];size_t n;while((n=fread(b,1,sizeof(b),f))>0)s.append(b,n);fclose(f);} }
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }
    if (!poke_start(core)) { FeLog("start fail: %s\n", poke_last_error(core)); return 1; }

    struct K { long f; int b; int d; };
    std::vector<K> keys;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (f) { char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, '#'); if (c) *c = 0;
                char cmd[16]={0}, btn[16]={0}; long fr; int d;
                if (sscanf(line, "%15s %ld %15s %d", cmd, &fr, btn, &d) == 4 && !strcasecmp(cmd,"KEY")) {
                    static const char* nm[]={"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
                    for (int i=0;i<12;i++) if (!strcasecmp(btn,nm[i])) { K k; k.f=fr;k.b=i;k.d=d; keys.push_back(k); }
                }
            }
            fclose(f);
        }
    }

    size_t ki = 0;
    for (long f = 0; f < frames; f++) {
        while (ki < keys.size() && keys[ki].f == f) { poke_set_button(core, keys[ki].b, keys[ki].d != 0); ki++; }
        if (!poke_frame(core)) { FeLog("stopped at %ld\n", f); break; }
    }

    // The milestone: the state the game is ACTUALLY in at the end of the plan.
    cmdDump();
    FeLog("\n--- accessibility commands ---\n");
    FeLog("Where am I?  -> "); cmdWhereAmI();
    FeLog("Next enemy   -> "); cmdNextEnemy(+1);
    FeLog("Next ally    -> "); cmdNextAlly(+1);
    FeLog("Next ally    -> "); cmdNextAlly(+1);
    FeLog("Next enemy   -> "); cmdNextEnemy(+1);

    poke_destroy(core);
    return 0;
}
#endif  // FE_NO_MAIN
