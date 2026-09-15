/*
 * feterrain2.cpp — FE11 terrain categories and the movement-cost matrix.
 *
 * ── THE ALGORITHM, recovered verbatim from the decompilation ──────────────────
 *
 *   src/ov000/map_sequence.cpp:2741 (func_ov000_021abbc8, "build movement range")
 *     if (func_0203826c(
 *             gFE11Database->pTerrain[unk_828[ix | iy<<5]].unk_08,   // terrain category
 *             pUnitA->pJobData->unk_28)                             // movement type
 *         < 0) continue;                                            // impassable
 *     ... unk_08->unk_0854[ix | iy<<5] = 0;                         // mark reachable
 *   plus, before that:
 *     unk_82c[ix | iy<<5] & 0x80  != 0  -> skip   (impassable-by-terrain flag)
 *     unk_d30[(ix|iy<<5)>>3] & (1<<(ix&7)) == 0 -> skip  (reachability bitmap)
 *     GetUnit(unk_028[ix | iy<<5]) != NULL -> skip (tile occupied)
 *
 *   src/database.cpp:419
 *     GetTerrainCategoryDBIndex(p) = (p - gFE11Database->unk_24) / 4
 *   src/database.cpp:424
 *     stride = ((TerrainCostData*)unk_28->size + 3) & ~3
 *     cost   = ((TerrainCostData*)unk_28)->unk_04[b * stride + category]
 *
 * So: the category is an INDEX into a 4-byte-record table based at unk_24, and the
 * cost is looked up in a [movementType][category] matrix.
 *
 * ⛔ THE ONE THING TO GET RIGHT. `TerrainCostData { s32 size; s8 * unk_04; }` in the
 * header claims unk_04 is a POINTER. Reading it as one yields 0x010101FF, which is not
 * a valid RAM address — but as four signed bytes `01 01 01 FF` it reads 1,1,1,-1, i.e.
 * plausible terrain costs with -1 meaning impassable. So a flexible array member
 * (`s8 unk_04[];` at +4) is the likely truth and the header is a guess. This probe
 * tests BOTH interpretations and reports which one produces a coherent matrix, rather
 * than assuming either.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

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

static const uint32_t A_gMapStateManager = 0x021E3328;
static const uint32_t A_gFE11Database    = 0x02197254;
static const uint32_t A_gUnitList        = 0x021974D8;

static bool Plausible(uint32_t p) { return p >= RAM_BASE && p < RAM_BASE + RAM_SIZE && (p & 3) == 0; }

static std::string PrintableAt(uint32_t a, int maxLen = 40)
{
    std::string out;
    for (int i = 0; i < maxLen; i++) {
        uint8_t c = R8(a + i);
        if (c == 0) break;
        out.push_back((c >= 32 && c < 127) ? (char) c : '.');
    }
    return out;
}

static void HexDump(const char* label, uint32_t a, int n)
{
    printf("%s @%08X\n", label, a);
    if (!InRam(a, (uint32_t) n)) { printf("  (out of RAM)\n"); return; }
    for (int off = 0; off < n; off += 16) {
        printf("  +%03X  ", off);
        int last = (off + 16 <= n) ? 16 : (n - off);
        for (int i = 0; i < last; i++) printf("%02X ", R8(a + off + i));
        for (int i = last; i < 16; i++) printf("   ");
        printf(" ");
        for (int i = 0; i < last; i++) {
            uint8_t c = R8(a + off + i);
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

int main(int argc, char** argv)
{
    const char* rom = (argc > 1) ? argv[1] : nullptr;
    int frames = (argc > 2) ? atoi(argv[2]) : 8500;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;
    if (!rom) { fprintf(stderr, "usage: feterrain2 <rom.nds> [frames] [plan]\n"); return 2; }

    PokeCore* core = poke_create();
    if (!core) return 1;
    if (!poke_load_rom(core, rom, NULL)) { fprintf(stderr, "load: %s\n", poke_last_error(core)); return 1; }
    { melonDS::NDS* nds = poke_debug_nds(core); if (!nds) return 1; gRam = (uint8_t*) nds->MainRAM; }
    {
        static std::string script;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f = fopen(shim, "rb"); if (f) { char b[65536]; size_t n;
            while ((n = fread(b, 1, sizeof(b), f)) > 0) script.append(b, n); fclose(f); } }
        script += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, script.c_str());
    }
    if (!poke_start(core)) { fprintf(stderr, "start: %s\n", poke_last_error(core)); return 1; }

    struct K { long f; int b; int d; };
    std::vector<K> keys;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (f) { char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, '#'); if (c) *c = 0;
                char cmd[16] = {0}, btn[64] = {0}; long fr; int d;
                if (sscanf(line, "%15s %ld %63s %d", cmd, &fr, btn, &d) == 4 && !strcasecmp(cmd, "KEY")) {
                    static const char* names[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
                    for (int i = 0; i < 12; i++)
                        if (!strcasecmp(btn, names[i])) { K k; k.f = fr; k.b = i; k.d = d; keys.push_back(k); }
                }
            }
            fclose(f);
        }
    }
    size_t ki = 0;
    printf("[plan] %s -> %zu key events\n", planPath ? planPath : "(none)", keys.size());
    if (!keys.empty())
        printf("[plan] frames %ld..%ld\n", keys.front().f, keys.back().f);
    fflush(stdout);
    for (int i = 0; i < frames; i++) {
        while (ki < keys.size() && keys[ki].f == i) {
            // ⛔ poke_set_button, NOT poke_set_hotkey. poke_set_hotkey sends the
            // accessibility script's LETTER keys (J/K/L/I/O/P/C/E/N/B/R/U) which main.lua
            // acts on; it does not press a DS button. Using it for a plan meant no
            // directional/A input ever reached the game: the run stayed on the title
            // screen, gMapStateManager read 00000000 for the whole run, and it looked
            // like a memory-map problem rather than "the buttons were never pressed".
            // fedump and fe_access both use poke_set_button; match them.
            poke_set_button(core, keys[ki].b, keys[ki].d != 0);
            ki++;
        }
        if (!poke_frame(core)) break;
        // Trace gMapStateManager across the run: it is the one value the working
        // harness and this probe disagree on, so print when it changes rather than
        // sampling once and guessing.
        if ((i % 250) == 0) {
            uint32_t v = R32(A_gMapStateManager);
            printf("[msm] f=%-5d gMapStateManager=%08X %s\n", i, v,
                   Plausible(v) ? "(valid)" : "");
            fflush(stdout);
        }
    }
    printf("[plan] consumed %zu of %zu events by frame %d\n", ki, keys.size(), frames);

    printf("=============== FE11 TERRAIN + MOVEMENT (frame %d) ===============\n", frames);

    uint32_t dbPtr = R32(A_gFE11Database);
    uint32_t db = Plausible(dbPtr) ? dbPtr : A_gFE11Database;

    printf("\n---- FE11Database pointer block (raw) ----\n");
    printf("  gFE11Database [%08X] = %08X  -> using %08X\n", A_gFE11Database, dbPtr, db);
    HexDump("  db+0x00", db, 0x50);

    uint32_t pTerrain = R32(db + 0x20);
    uint32_t unk24    = R32(db + 0x24);
    uint32_t unk28    = R32(db + 0x28);

    // ---- category table ----
    printf("\n---- unk_24 = terrain CATEGORY table base (%08X) ----\n", unk24);
    if (Plausible(unk24)) {
        HexDump("  raw", unk24, 0x40);
        printf("  as 4-byte records (the index unit used by GetTerrainCategoryDBIndex):\n");
        for (int i = 0; i < 16; i++) {
            uint32_t rec = unk24 + (uint32_t) i * 4;
            uint8_t b0 = R8(rec), b1 = R8(rec+1), b2 = R8(rec+2), b3 = R8(rec+3);
            printf("    [%2d] %02X %02X %02X %02X   as s32=%d\n",
                   i, b0, b1, b2, b3, (int32_t) R32(rec));
        }
    }

    // ---- cost matrix, BOTH interpretations ----
    printf("\n---- unk_28 = TerrainCostData (%08X) ----\n", unk28);
    if (Plausible(unk28)) {
        HexDump("  raw", unk28, 0x60);
        int32_t size  = (int32_t) R32(unk28);
        uint32_t maybePtr = R32(unk28 + 4);
        printf("  size (at +0)              = %d\n", size);
        printf("  +4 read as pointer        = %08X  %s\n", maybePtr,
               Plausible(maybePtr) ? "(valid RAM ptr)" : "(NOT a pointer)");
        int stride = (size + 3) & ~3;
        printf("  stride = (size+3)&~3      = %d\n", stride);

        if (Plausible(maybePtr) && size > 0 && size < 64) {
            printf("\n  INTERPRETATION A: unk_04 is a POINTER\n");
            printf("  %-5s", "type");
            for (int c = 0; c < 16; c++) printf("%5d", c);
            printf("\n");
            for (int t = 0; t < size; t++) {
                printf("  %-5d", t);
                for (int c = 0; c < 16; c++)
                    printf("%5d", (int) R8S(maybePtr + (uint32_t)(t * stride + c)));
                printf("\n");
            }
        }

        // Flexible-array interpretation: records start at +4.
        if (size > 0 && size < 64) {
            printf("\n  INTERPRETATION B: unk_04 is a FLEXIBLE ARRAY at +4\n");
            printf("  (matches that +4 reads 01 01 01 FF = costs 1,1,1,-1)\n");
            printf("  %-5s", "type");
            for (int c = 0; c < 16; c++) printf("%5d", c);
            printf("\n");
            for (int t = 0; t < size; t++) {
                printf("  %-5d", t);
                for (int c = 0; c < 16; c++)
                    printf("%5d", (int) R8S(unk28 + 4 + (uint32_t)(t * stride + c)));
                printf("\n");
            }
        }
    }

    // ---- what is on the map ----
    // ⛔ SCAN FORWARD FOR A VALID MAP rather than trusting the caller's frame count.
    // gMapStateManager is in OVERLAY bss and is only valid while ov000 is mapped, so a
    // single sample at an arbitrary frame often lands on a menu/cutscene and reports
    // "INVALID" — which says nothing about the terrain data.
    uint32_t msm = 0;
    int validAt = -1;
    for (int extra = 0; extra < 900 && validAt < 0; extra++) {
        uint32_t v = R32(A_gMapStateManager);
        if (Plausible(v)) { msm = v; validAt = frames + extra; break; }
        if (!poke_frame(core)) break;
    }
    printf("\n---- map state (%s) ----\n",
           validAt >= 0 ? "valid" : "INVALID after scanning 900 extra frames");
    if (validAt >= 0) printf("  first valid frame: %d\n", validAt);
    if (Plausible(msm)) {
        int hist[256] = {0};
        for (int i = 0; i < 0x400; i++) hist[R8(msm + 0x830 + i)]++;
        printf("  terrain category histogram:\n");
        for (int i = 0; i < 64; i++) if (hist[i]) printf("    category %2d : %d tiles\n", i, hist[i]);

        // The movement bitmap the game fills in.
        printf("\n  unk_d30 movement bitmap (non-zero bytes = reachable):\n");
        int nz = 0;
        for (int i = 0; i < 0x80; i++) if (R8(msm + 0xD30 + i)) nz++;
        printf("    %d of 128 bytes non-zero\n", nz);
        if (nz) {
            printf("    reachable tiles (x,y) for the first 24 set bits:\n");
            int shown = 0;
            for (int y = 0; y < 32 && shown < 24; y++)
                for (int x = 0; x < 32 && shown < 24; x++) {
                    uint32_t i = (uint32_t)(x | (y << 5));
                    if (R8(msm + 0xD30 + (i >> 3)) & (1 << (x & 7))) {
                        printf("      (%2d,%2d)\n", x, y); shown++;
                    }
                }
        }
    }

    // ---- units, to correlate a movement type ----
    printf("\n---- units ----\n");
    uint32_t base = R32(A_gUnitList);
    if (Plausible(base)) {
        for (int slot = 1; slot <= 8; slot++) {
            uint32_t a = base + (uint32_t) slot * 0xA8;
            if (!InRam(a, 0xA8)) break;
            int hp = R8S(a + 0x6C), x = R8S(a + 0x6E), y = R8S(a + 0x6F);
            if (hp <= 0 || hp > 80 || x < 0 || y < 0 || x > 31 || y > 31) continue;
            uint32_t force = R32(a + 0x4C);
            int fac = Plausible(force) ? (int) R32(force + 0x08) : -1;
            uint32_t jp = R32(a + 0x44);
            uint32_t pj = Plausible(jp) ? jp : 0;
            uint32_t jid = pj ? R32(pj) : 0;
            // JobData (include/unit.hpp):
            //   /* 28 */ u8 unk_28;   <- movement TYPE (cost-matrix row index)
            //   /* 29 */ u8 mov;      <- the class's movement stat
            // ⛔ unk_28 is a BYTE. Reading it with R32 gave 50464512 (0x03025C00),
            // which looks like an address and is really one byte plus 3 neighbours.
            uint8_t movType = pj ? R8(pj + 0x28) : 0xFF;
            uint8_t jobMov  = pj ? R8(pj + 0x29) : 0;
            printf("  slot %d unitMov=%2d jobMov=%2d movType=%3u HP=%2d (%2d,%2d) fac=%d jid=%s\n",
                   slot, R8S(a + 0x6D), jobMov, movType, hp, x, y, fac,
                   InRam(jid, 8) ? PrintableAt(jid, 20).c_str() : "?");
            if (pj && movType < 32 && Plausible(unk28)) {
                int str2 = ((int32_t) R32(unk28) + 3) & ~3;
                printf("      cost row %u:", movType);
                for (int c = 0; c < 16; c++)
                    printf(" %4d", (int) R8S(unk28 + 4 + (uint32_t)(movType * str2 + c)));
                printf("\n");
            }
        }
    }

    poke_destroy(core);
    return 0;
}
