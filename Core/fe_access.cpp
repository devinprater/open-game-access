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

melonDS::NDS* poke_debug_nds(PokeCore* core);

static const uint32_t RAM_BASE = 0x02000000, RAM_SIZE = 0x400000;
static uint8_t* gRam = nullptr;

static bool InRam(uint32_t a, uint32_t n = 1)
{ return a >= RAM_BASE && (uint64_t) a + n <= (uint64_t) RAM_BASE + RAM_SIZE; }
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

static void cmdWhereAmI()
{
    Cursor c = ReadCursor();
    if (!c.ok) { printf("Not on a map yet.\n"); return; }
    printf("Cursor %d, %d.", c.x, c.y);

    // Terrain: report the game's own category, and say it is a number because no
    // category-to-name table has been located. Verified means the pointer
    // arithmetic agreed with the category the game itself stored, so the mapping is
    // proven rather than assumed.
    Terrain t = ReadTerrain();
    if (t.ok && t.category >= 0)
        printf(" Terrain category %d (tile %u%s).", t.category, t.tile,
               t.verified ? ", verified" : "");
    else
        printf(" Terrain: unavailable.");

    bool any = false;
    for (auto& u : AllUnits())
        if (u.x == c.x && u.y == c.y) {
            printf(" Unit here: %s, %d HP%s.", u.label().c_str(), u.hp,
                   u.acted() ? ", acted" : ", unacted");
            any = true;
        }
    if (!any) printf(" No unit here.");
    printf("\n");
}

static void cmdNextAlly(int dir)
{
    static int idx = -1;
    Cursor c = ReadCursor();
    if (!c.ok) { printf("Not on a map yet.\n"); return; }
    auto us = AllUnits();
    if (us.empty()) { printf("No units found.\n"); return; }

    // Ally = the game's own faction number (Force.id 0 or 2). Grouping by "the
    // leader's Force pointer" happened to work but had no way to say WHICH group was
    // friendly; with the faction number that is explicit, and it also excludes the
    // unassigned reserve (faction 4), which the pointer test included.
    std::vector<Unit> allies;
    for (auto& u : us) if (u.isPlayer()) allies.push_back(u);
    if (allies.empty()) { printf("No allies found.\n"); return; }

    idx = (idx + dir + (int) allies.size() * 4) % (int) allies.size();
    Unit& u = allies[idx];
    double d = sqrt((double)(u.x - c.x) * (u.x - c.x) + (double)(u.y - c.y) * (u.y - c.y));
    printf("%s, %d HP, position %d, %d, %s, %.1f tiles away.\n",
           u.label().c_str(), u.hp, u.x, u.y,
           u.acted() ? "acted" : "unacted", d);
}

static void cmdNextEnemy(int dir)
{
    static int idx = -1;
    Cursor c = ReadCursor();
    if (!c.ok) { printf("Not on a map yet.\n"); return; }
    auto us = AllUnits();
    // Enemy = the game's own faction number (Force.id 1 or 3), not "a different
    // Force pointer". The pointer test this replaces also matched the 60-slot
    // unassigned reserve, so it would have reported phantom enemies on any map.
    std::vector<Unit> enemies;
    for (auto& u : us) if (u.isEnemy()) enemies.push_back(u);
    if (enemies.empty()) { printf("No enemies found.\n"); return; }
    // sort by distance from the cursor (the SRWYAccess idea: nearest first)
    std::sort(enemies.begin(), enemies.end(), [&](const Unit& a, const Unit& b) {
        int da = abs(a.x - c.x) + abs(a.y - c.y), db = abs(b.x - c.x) + abs(b.y - c.y);
        return da < db;
    });
    idx = (idx + dir + (int) enemies.size() * 4) % (int) enemies.size();
    Unit& u = enemies[idx];
    double d = sqrt((double)(u.x - c.x) * (u.x - c.x) + (double)(u.y - c.y) * (u.y - c.y));
    printf("%s, %d HP, position %d, %d, %.1f tiles away.\n",
           u.label().c_str(), u.hp, u.x, u.y, d);
}

static void cmdDump()
{
    Cursor c = ReadCursor();
    printf("=== Fire Emblem: Shadow Dragon — tactical state dump ===\n");
    printf("map state     : %s\n", c.ok ? "on a map" : "NOT on a map (gMapStateManager invalid)");
    printf("gMapStateManager = 0x%08X\n", R32(A_gMapStateManager));
    if (c.ok) printf("cursor        : 0x%08X  x=%d y=%d visible=%d\n", c.addr, c.x, c.y, c.vis);
    uint32_t base = R32(A_gUnitList);
    printf("gUnitList     : 0x%08X  stride=0x%02X  slots=%d\n", base, UNIT_STRIDE, UNIT_SLOTS);
    auto us = AllUnits();
    printf("live units    : %zu\n", us.size());
    // Raw bytes of the first real unit: this is how the character/class
    // identifiers get located, by looking rather than by assuming an offset.
    if (!us.empty()) {
        const Unit& u0 = us[0];
        printf("raw dump of slot %d at 0x%08X:\n", u0.slot, u0.addr);
        for (uint32_t off = 0; off < 0x80; off += 16) {
            printf("  +%02X:", off);
            for (uint32_t i = 0; i < 16; i++) printf(" %02X", R8(u0.addr + off + i));
            printf("   ");
            for (uint32_t i = 0; i < 16; i++) {
                uint8_t b = R8(u0.addr + off + i);
                printf("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
            }
            printf("\n");
        }
        printf("  as u32 words:\n");
        for (uint32_t off = 0; off < 0x80; off += 4) {
            uint32_t v = R32(u0.addr + off);
            if (v) printf("    +%02X = 0x%08X%s\n", off, v,
                          (v >= RAM_BASE && v < RAM_BASE + RAM_SIZE) ? "  (points into RAM)" : "");
        }
        printf("  pid   = '%s'\n", u0.name.c_str());
        printf("  jid   = '%s'\n", u0.jobName.c_str());
        printf("  (PersonData ptr 0x%08X -> pid '%s'; JobData ptr 0x%08X -> jid '%s')\n",
               u0.pid, ReadCStr(R32(u0.pid)).c_str(), u0.jid, ReadCStr(R32(u0.jid)).c_str());
    }
    printf("  slot addr       Lv HP Mov   X   Y  act dead fac name             pid          jid\n");
    for (auto& u : us)
        printf("  %-4d 0x%08X %2d %2d  %2d %3d %3d   %d    %d   %3d %-16s %-12s %s\n",
               u.slot, u.addr, u.level, u.hp, u.mov, u.x, u.y,
               (u.state1 & US_ACTED) ? 1 : 0, (u.state1 & US_DEAD) ? 1 : 0,
               u.faction, u.label().c_str(), u.name.c_str(), u.jobName.c_str());
    printf("  (fac: 0 player, 1 enemy, 2/3 scenario player/enemy, 4 unassigned, 5 other)\n");
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 5000;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;

    PokeCore* core = poke_create();
    // SAVE=<path> loads in-chapter progress; needed to reach a map that has enemies.
    const char* savePath = getenv("SAVE");
    if (!poke_load_rom(core, rom, savePath)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    if (savePath) printf("[save] loaded %s\n", savePath);
    melonDS::NDS* nds = poke_debug_nds(core);
    gRam = nds->MainRAM;
    {
        static std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f=fopen(shim,"rb"); if(f){char b[65536];size_t n;while((n=fread(b,1,sizeof(b),f))>0)s.append(b,n);fclose(f);} }
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

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
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
    }

    // The milestone: the state the game is ACTUALLY in at the end of the plan.
    cmdDump();
    printf("\n--- accessibility commands ---\n");
    printf("Where am I?  -> "); cmdWhereAmI();
    printf("Next ally    -> "); cmdNextAlly(+1);
    printf("Next ally    -> "); cmdNextAlly(+1);
    printf("Next enemy   -> "); cmdNextEnemy(+1);

    poke_destroy(core);
    return 0;
}
