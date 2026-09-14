/*
 * fedump.cpp — Fire Emblem: Shadow Dragon tactical-state reader.
 *
 * This does NOT guess by diffing RAM. It follows the game's own data structures,
 * using symbols resolved from the fe11-us decompilation (config/YFEE01/arm9/
 * symbols.txt) and the class layouts in include/*.hpp:
 *
 *   0x021E3328  gMapStateManager      (bss)
 *   0x021974D8  gUnitList
 *   0x021974DC  gForces
 *
 *   MapStateManager { camera +0x000, unk_04 +0x004, unk_08 +0x008,
 *                     inputHandler +0x00C, cursor +0x010, unk_14 +0x014,
 *                     unk_18 +0x018 (DisposGroup*), unk_1c +0x01C,
 *                     unk_20 u16 +0x020, unk_22 u16 +0x022 }
 *   Cursor { unk_00[2] +0, unk_02[2] +2, xDisplay s16 +0x04, yDisplay s16 +0x06,
 *            xTile u8 +0x08, yTile u8 +0x09, isVisible +0x0A, unk_0b +0x0B,
 *            changed +0x0C, ... }
 *   Unit   { unk_00 u16, ..., level u8 +0x6A, exp u8 +0x6B, hp s8 +0x6C,
 *            mov s8 +0x6D, xPos s8 +0x6E, yPos s8 +0x6F, items[5] +0x70,
 *            ... state1 s32 +0x98, state2 s32 +0x9C, ... }
 *     Unit state bits: US_ACTED = 1<<0, US_DEAD = 1<<3, US_NOT_PRESENT = 1<<12
 *     Unit list linkage: prev +0x38, next +0x3C
 *
 * ⛔ EVERY READ IS BOUNDS-CHECKED AND EVERY POINTER IS VALIDATED. The game is
 * still loading/allocating these structures for the first part of the boot, so a
 * reader that trusts a pointer crashes the emulator instead of saying "not on a
 * map yet" — which is exactly the state we start in.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static const uint32_t RAM_BASE = 0x02000000;
static const uint32_t RAM_SIZE = 0x400000;
static uint8_t* gRam = nullptr;

static bool InRam(uint32_t a, uint32_t n = 1)
{
    return a >= RAM_BASE && (uint64_t) a + n <= (uint64_t) RAM_BASE + RAM_SIZE;
}
static uint8_t  R8 (uint32_t a) { return InRam(a)    ? gRam[a - RAM_BASE] : 0; }
static uint16_t R16(uint32_t a) { return InRam(a, 2) ? (uint16_t)(gRam[a-RAM_BASE] | (gRam[a-RAM_BASE+1] << 8)) : 0; }
static uint32_t R32(uint32_t a) { return InRam(a, 4) ? (uint32_t)(gRam[a-RAM_BASE] | (gRam[a-RAM_BASE+1]<<8)
                                     | (gRam[a-RAM_BASE+2]<<16) | ((uint32_t)gRam[a-RAM_BASE+3]<<24)) : 0; }
static int8_t   R8S(uint32_t a){ return (int8_t) R8(a); }
static int16_t  R16S(uint32_t a){ return (int16_t) R16(a); }

// ---- symbol addresses (fe11-us, YFEE01) ----
static const uint32_t A_gMapStateManager = 0x021E3328;
static const uint32_t A_gUnitList        = 0x021974D8;
static const uint32_t A_gForces          = 0x021974DC;

/* gUnitList is the BASE OF AN ARRAY, not a linked-list head:
 *     Unit * GetUnit(s32 unitId) { return gUnitList + unitId - 1; }
 * (decompilation, include/unit.hpp). The index is 1-based, and the pointer
 * arithmetic is on Unit*, so records are a fixed stride apart. The stride is
 * found by searching, not assumed — see findStride(). */
static uint32_t gUnitStride = 0xA4;   // sizeof(Unit) from the class layout; verified at runtime

// ---- MapStateManager / Cursor offsets ----
static const uint32_t MSM_CURSOR = 0x010;
static const uint32_t CUR_XTILE  = 0x008;
static const uint32_t CUR_YTILE  = 0x009;
static const uint32_t CUR_VIS    = 0x00A;

// ---- Unit offsets ----  (decompilation, include/unit.hpp, class Unit)
static const uint32_t U_LEVEL = 0x6A, U_HP = 0x6C, U_MOV = 0x6D, U_X = 0x6E, U_Y = 0x6F;
static const uint32_t U_ITEMS = 0x70;
static const uint32_t U_STATE1 = 0x98, U_PREV = 0x38, U_NEXT = 0x3C;
static const uint32_t U_FORCE = 0x4C;

static const uint32_t US_ACTED = 1u << 0, US_DEAD = 1u << 3, US_NOT_PRESENT = 1u << 12;

/* Look at gUnitList and the first records: print level/hp/x/y for a few
 * candidate strides and pick the one where consecutive records look like REAL
 * units (level 1..30, hp 0..80, x/y inside a map). This is the honest way to find
 * a stride when the class header might not reflect the runtime allocation. */
static void findStride()
{
    uint32_t base = R32(A_gUnitList);
    printf("\n[unit array] gUnitList = 0x%08X\n", base);
    if (!InRam(base, 0x40)) { printf("  (invalid base)\n"); return; }

    int bestScore = -1; uint32_t bestStride = gUnitStride;
    static const uint32_t strides[] = {0x9C, 0xA0, 0xA4, 0xA8, 0xB0};
    for (uint32_t st : strides) {
        int score = 0;
        for (int i = 0; i < 8; i++) {
            uint32_t ua = base + (uint32_t) i * st;
            if (!InRam(ua, 0xA4)) break;
            int lv = R8S(ua + U_LEVEL), hp = R8S(ua + U_HP);
            int x  = R8S(ua + U_X),     y  = R8S(ua + U_Y);
            if (lv >= 1 && lv <= 30) score++;
            if (hp >= 0 && hp <= 80) score++;
            if (x >= 0 && x < 32) score++;
            if (y >= 0 && y < 32) score++;
        }
        printf("  stride 0x%02X -> score %d/32\n", st, score);
        if (score > bestScore) { bestScore = score; bestStride = st; }
    }
    gUnitStride = bestStride;
    printf("  chosen stride: 0x%02X (score %d/32)\n", gUnitStride, bestScore);
}


struct UnitRec {
    uint32_t addr; int level, hp, mov, x, y; uint32_t state1, force; uint16_t item0;
};

static bool ReadUnit(uint32_t ua, UnitRec& o)
{
    if (!InRam(ua, 0xA4)) return false;
    o.addr = ua;
    o.level = R8S(ua + U_LEVEL);
    o.hp    = R8S(ua + U_HP);
    o.mov   = R8S(ua + U_MOV);
    o.x     = R8S(ua + U_X);
    o.y     = R8S(ua + U_Y);
    o.state1 = R32(ua + U_STATE1);
    o.force  = R32(ua + U_FORCE);
    o.item0  = R16(ua + U_ITEMS);
    return true;
}

/* Walk the unit ARRAY from gUnitList (1-based, fixed stride), with a cap and a
 * sanity filter. Unlike the linked list this does not need pointer chasing, and a
 * bogus record cannot loop forever. */
static int WalkUnits(UnitRec* out, int maxOut, uint32_t* outHead)
{
    uint32_t base = R32(A_gUnitList);
    if (outHead) *outHead = base;
    int n = 0;
    if (!InRam(base, 0x40)) return 0;
    for (int i = 0; i < maxOut; i++)
    {
        uint32_t ua = base + (uint32_t) i * gUnitStride;
        if (!InRam(ua, 0xA4)) break;
        UnitRec u;
        if (!ReadUnit(ua, u)) break;
        // A record that looks like nothing at all means we have run past the end
        // of the array (the game does not zero-fill the whole allocation).
        bool plausible = (u.level >= 1 && u.level <= 30) && (u.hp >= -1 && u.hp <= 80) &&
                         (u.x >= 0 && u.x < 32) && (u.y >= 0 && u.y < 32);
        if (!plausible) {
            // keep going a little in case of a gap, but stop at 4 in a row
            static int misses = 0;
            if (++misses > 4) break;
            continue;
        }
        out[n++] = u;
    }
    return n;
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 4000;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;
    long dumpAt = (argc > 4) ? atol(argv[4]) : -1;   // -1 = dump at end only

    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
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

    // Plan: KEY <frame> <BTN> <0|1>   and   SNAP <frame> <path>
    struct K { long f; int b; int d; std::string snap; bool isSnap = false; };
    std::vector<K> keys;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, '#'); if (c) *c = 0;
                char cmd[16] = {0}, btn[512] = {0}; long fr; int d;
                if (sscanf(line, "%15s %ld %511s %d", cmd, &fr, btn, &d) == 4 && !strcasecmp(cmd, "KEY")) {
                    static const char* names[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
                    for (int i = 0; i < 12; i++) if (!strcasecmp(btn, names[i])) { K k; k.f = fr; k.b = i; k.d = d; keys.push_back(k); }
                } else if (sscanf(line, "%15s %ld %511s", cmd, &fr, btn) == 3 && !strcasecmp(cmd, "SNAP")) {
                    K k; k.f = fr; k.isSnap = true; k.snap = btn; keys.push_back(k);
                }
            }
            fclose(f);
        }
    }

    auto dump = [&](const char* when) {
        printf("\n================ STATE (%s) frame=%ld ================\n", when, frames);
        findStride();
        uint32_t msm = R32(A_gMapStateManager);
        printf("gMapStateManager = 0x%08X  %s\n", msm, InRam(msm, 0x30) ? "(valid RAM ptr)" : "(NULL / invalid -> not on a map yet)");
        if (InRam(msm, 0x30)) {
            uint32_t cam = R32(msm + 0x000);
            uint32_t cur = R32(msm + MSM_CURSOR);
            printf("  camera = 0x%08X  %s\n", cam, InRam(cam, 0x20) ? "(valid)" : "(invalid)");
            printf("  cursor = 0x%08X  %s\n", cur, InRam(cur, 0x20) ? "(valid)" : "(invalid)");
            if (InRam(cur, 0x20)) {
                printf("  CURSOR xTile=%u yTile=%u visible=%u\n",
                       R8(cur + CUR_XTILE), R8(cur + CUR_YTILE), R8(cur + CUR_VIS));
                if (InRam(cam, 0x20))
                    printf("  CAMERA x=%d y=%d tileSize=%d\n",
                           (int) R32(cam + 0x00), (int) R32(cam + 0x04), (int) R16S(cam + 0x0C));
            }
            printf("  unk_20=%u unk_22=%u\n", R16(msm + 0x20), R16(msm + 0x22));
        }

        uint32_t head = 0;
        static UnitRec units[64];
        int n = WalkUnits(units, 24, &head);
        printf("\ngUnitList = 0x%08X  (%d plausible unit(s), stride 0x%02X)\n", head, n, gUnitStride);
        if (n > 0) {
            printf("  #  address     Lv HP Mov  X   Y  acted dead state1      force      item0\n");
            for (int i = 0; i < n; i++) {
                UnitRec& u = units[i];
                printf("  %-2d 0x%08X  %2d %2d  %2d %3d %3d   %d     %d   0x%08X  0x%08X  0x%04X\n",
                       i, u.addr, u.level, u.hp, u.mov, u.x, u.y,
                       (u.state1 & US_ACTED) ? 1 : 0,
                       (u.state1 & US_DEAD) ? 1 : 0,
                       u.state1, u.force, u.item0);
            }
        }
        uint32_t forces = R32(A_gForces);
        printf("\ngForces = 0x%08X %s\n", forces, InRam(forces, 0x40) ? "(valid)" : "(invalid)");
        if (InRam(forces, 0x40)) {
            for (int i = 0; i < 4; i++) {
                uint32_t f = R32(forces + i * 4);
                printf("  force[%d] = 0x%08X %s\n", i, f, InRam(f, 0x20) ? "(valid)" : "");
            }
        }
    };

    size_t ki = 0;
    for (long f = 0; f < frames; f++) {
        while (ki < keys.size() && keys[ki].f == f) {
            K& k = keys[ki];
            if (k.isSnap) {
                FILE* o = fopen(k.snap.c_str(), "wb");
                if (o) { fwrite(nds->MainRAM, 1, RAM_SIZE, o); fclose(o); }
                // Print the ONE line that matters for correlating an experiment:
                // the cursor coords, bracketed by the frame it was taken on.
                uint32_t msm = R32(A_gMapStateManager);
                uint32_t cur = InRam(msm, 0x30) ? R32(msm + MSM_CURSOR) : 0;
                int xt = -1, yt = -1, vis = -1;
                if (InRam(cur, 0x20)) { xt = R8(cur + CUR_XTILE); yt = R8(cur + CUR_YTILE); vis = R8(cur + CUR_VIS); }
                printf("[snap] f=%-5ld cursor=(%3d,%3d) vis=%d  %s\n", f, xt, yt, vis, k.snap.c_str());
                fflush(stdout);
            } else {
                poke_set_button(core, k.b, k.d != 0);
            }
            ki++;
        }
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
        if (dumpAt >= 0 && f == dumpAt) dump("mid-run");
    }
    dump("final");
    poke_destroy(core);
    return 0;
}
