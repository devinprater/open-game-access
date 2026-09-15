/*
 * fechapter.cpp — FE11 chapter identity, movement range, and dialogue text.
 *
 * ── THREE THINGS, from the decompilation ─────────────────────────────────────
 *
 * 1. CHAPTER / MAP IDENTITY  (src/database.cpp)
 *      MapData {
 *          char* unk_00;   // "bmap_###" or "arena_###"  <- the map identifier
 *          char* unk_04;   // "MCT_###"                  <- chapter text id
 *      }
 *      func_0203812c(pMap) returns HashTable::Get1(pMap->unk_04) with pMap->unk_00
 *      as fallback. Reading the identifier straight out of the game means the
 *      chapter name is the game's own string, not a hand-made table.
 *      The map table is gFE11Database->unk_18; GetMapDBIndex(pMap) =
 *      (pMap - unk_18) / sizeof(MapData).
 *
 * 2. MOVEMENT RANGE  (src/ov000/map_sequence.cpp:2741)
 *      After the blue overlay is computed, MapStateManager.unk_d30 is a 32x32
 *      reachability BITMAP: bit (x | y<<5) set == that tile is reachable. This is
 *      the authoritative "where can this unit go" answer — the same bits the game
 *      draws — so we read it rather than reimplementing the movement rules.
 *      Per-tile impassability lives in unk_82c[tile] & 0x80.
 *
 * 3. TEXT  (src/...; dialogue is held in the message tables)
 *      Rather than chase the message-table format, dump the strings the game
 *      itself holds around the chapter data and the message system's own pointer
 *      table. FE11 strings are ASCII ("PID_MARS", "bmap_001", "MCT_001"), so a
 *      printable-string scan of the databases finds the real text.
 */
#include "pokecore.h"

// fechapter dereferences nds->MainRAM, so the real melonDS NDS.h must be on the
// include path (-I$MELONDS_SRC/src, set in scripts/fe-chapter.sh). A bare forward
// declaration is not enough: the type has to be complete at the dereference.
#include "NDS.h"
melonDS::NDS* poke_debug_nds(PokeCore* core);

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// ---- FE11 (USA, YFEE01) addresses ----
static const uint32_t A_gMapStateManager = 0x021E3328;
static const uint32_t A_gUnitList       = 0x021974D8;
static const uint32_t A_gFE11Database   = 0x02197254;

static uint8_t* gRam = nullptr;
static const uint32_t RAM_BYTES = 0x400000;

static inline bool InRam(uint32_t a, uint32_t n) {
    return a >= 0x02000000u && a + n <= 0x02400000u && (a - 0x02000000u) + n <= RAM_BYTES;
}
static inline uint8_t  R8 (uint32_t a) { return InRam(a, 1) ? gRam[a - 0x02000000u] : 0; }
static inline int8_t   R8S(uint32_t a) { return (int8_t) R8(a); }
static inline uint16_t R16(uint32_t a) { return InRam(a, 2) ? *(uint16_t*) &gRam[a - 0x02000000u] : 0; }
static inline uint32_t R32(uint32_t a) { return InRam(a, 4) ? *(uint32_t*) &gRam[a - 0x02000000u] : 0; }
static inline bool Plausible(uint32_t p) { return InRam(p, 4); }

static std::string PrintableAt(uint32_t addr, int maxLen) {
    std::string s;
    for (int i = 0; i < maxLen; i++) {
        uint8_t c = R8(addr + i);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) { s.clear(); break; }
        s.push_back((char) c);
    }
    return s;
}

// Is this a plausible ASCII string of length >= minLen starting here?
static bool IsStr(uint32_t a, int minLen, int maxLen) {
    if (!InRam(a, minLen)) return false;
    int n = 0;
    for (int i = 0; i < maxLen; i++) {
        uint8_t c = R8(a + i);
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;
        n++;
    }
    return n >= minLen;
}

struct Key { long f; int b; int d; };

int main(int argc, char** argv)
{
    const char* rom = (argc > 1) ? argv[1] : nullptr;
    int frames = (argc > 2) ? atoi(argv[2]) : 7000;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;
    if (!rom) { fprintf(stderr, "usage: fechapter <rom> [frames] [plan]\n"); return 2; }

    PokeCore* core = poke_create();

    std::vector<Key> keys;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, '#'); if (c) *c = 0;
                char cmd[16] = {0}, btn[16] = {0}; long fr; int d;
                if (sscanf(line, "%15s %ld %15s %d", cmd, &fr, btn, &d) == 4 && !strcasecmp(cmd, "KEY")) {
                    static const char* nm[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
                    for (int i = 0; i < 12; i++) if (!strcasecmp(btn, nm[i])) { Key k{fr, i, d}; keys.push_back(k); }
                }
            }
            fclose(f);
        }
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.f < b.f; });

    if (!poke_load_rom(core, rom, nullptr)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    { melonDS::NDS* nds = poke_debug_nds(core); if (!nds) return 1; gRam = (uint8_t*) nds->MainRAM; }
    {
        static std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f = fopen(shim, "rb"); if (f) { char b[65536]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n); fclose(f); } }
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }
    if (!poke_start(core)) { printf("start: %s\n", poke_last_error(core)); return 1; }

    size_t ki = 0;
    for (int i = 0; i < frames; i++) {
        while (ki < keys.size() && keys[ki].f == i) {
            // ⛔ poke_set_button, not poke_set_hotkey — see feterrain2.cpp.
            poke_set_button(core, keys[ki].b, keys[ki].d != 0);
            ki++;
        }
        if (!poke_frame(core)) break;
    }

    printf("=============== FE11 CHAPTER + MOVEMENT + TEXT (frame %d) ===============\n", frames);

    uint32_t msm = R32(A_gMapStateManager);
    printf("\n---- map state ----\n");
    printf("  gMapStateManager = %08X  %s\n", msm, Plausible(msm) ? "(valid)" : "(INVALID)");
    if (!Plausible(msm)) { poke_destroy(core); return 0; }

    uint32_t st = R32(msm + 0x08);              // the struct holding unk_0854
    printf("  msm+0x08 (state obj) = %08X  %s\n", st, Plausible(st) ? "(valid)" : "");

    // ---- 1. chapter / map identity ----
    printf("\n---- chapter / map identity ----\n");
    uint32_t dbPtr = R32(A_gFE11Database);
    uint32_t db = InRam(dbPtr, 0x40) ? dbPtr : A_gFE11Database;
    printf("  db = %08X\n", db);
    uint32_t mapTable = R32(db + 0x18);
    printf("  db->unk_18 (map table) = %08X  %s\n", mapTable, Plausible(mapTable) ? "(valid)" : "");
    if (Plausible(mapTable)) {
        for (int i = 0; i < 8; i++) {
            uint32_t m = mapTable + (uint32_t) i * 0x1C;
            uint32_t unk00 = R32(m + 0x00);
            uint32_t unk04 = R32(m + 0x04);
            std::string s0 = InRam(unk00, 8) ? PrintableAt(unk00, 24) : "";
            std::string s4 = InRam(unk04, 8) ? PrintableAt(unk04, 24) : "";
            printf("  map[%d] @%08X  unk_00=%-14s unk_04=%-14s\n", i, m,
                   s0.empty() ? "?" : s0.c_str(), s4.empty() ? "?" : s4.c_str());
        }
    }
    // The map the state manager is currently playing. Rather than hunt for a chapter
    // global, find which of the state manager's pointer fields points INTO the map
    // table — that IS the current map entry, and GetMapDBIndex() is exactly
    // (pMap - mapTable) / sizeof(MapData).
    printf("  map bounds: x %d..%d  y %d..%d\n",
           R8(msm + 0x24), R8(msm + 0x26), R8(msm + 0x25), R8(msm + 0x27));
    for (uint32_t off = 0; off < 0x400; off += 4) {
        uint32_t v = R32(msm + off);
        if (!Plausible(v)) continue;
        if (mapTable && v >= mapTable && v < mapTable + 0x1C * 512) {
            int idx = (int) ((v - mapTable) / 0x1C);
            printf("  ★ msm+0x%03X = %08X  -> map[%d]  unk_00=\"%s\" unk_04=\"%s\"\n",
                   off, v, idx,
                   PrintableAt(R32(v + 0x00), 20).c_str(),
                   PrintableAt(R32(v + 0x04), 20).c_str());
        }
    }
    for (uint32_t off = 0x0C; off <= 0x40; off += 4) {
        uint32_t v = R32(msm + off);
        if (Plausible(v) && IsStr(v, 4, 24))
            printf("  msm+0x%02X -> \"%s\"\n", off, PrintableAt(v, 24).c_str());
    }

    // ---- 2. movement range ----
    // MapStateManager holds FIVE 0x80-byte buffers (map.hpp): unk_c30, unk_cb0,
    // unk_d30, unk_db0, unk_e30. Each is 0x80 bytes = 1024 bits = 32x32, i.e. one bit
    // per tile — so any of them could be the blue movement overlay. unk_d30 is the one
    // the decomp's range loop reads, but it reads all-ones outside a move preview, so
    // sample ALL of them across frames and report the one that actually varies into a
    // plausible range. Guessing which buffer is "the" bitmap is how an earlier attempt
    // concluded the overlay does not exist.
    printf("\n---- movement range: all candidate bitmaps ----\n");
    struct Buf { const char* name; uint32_t off; };
    const Buf bufs[] = {
        {"unk_c30", 0xC30}, {"unk_cb0", 0xCB0}, {"unk_d30", 0xD30},
        {"unk_db0", 0xDB0}, {"unk_e30", 0xE30},
    };
    const int NBUF = 5;

    // Sample every 40 frames and keep the most "range-like" reading per buffer:
    // nonzero and not all-1024.
    int bestCount[NBUF], bestFrame[NBUF];
    for (int i = 0; i < NBUF; i++) { bestCount[i] = -1; bestFrame[i] = -1; }

    for (int probe = 0; probe < 1500; probe++) {
        for (int b = 0; b < NBUF; b++) {
            uint32_t base = msm + bufs[b].off;
            int on = 0;
            for (int i = 0; i < 0x400; i++)
                if ((R8(base + (i >> 3)) >> (i & 7)) & 1) on++;
            if (on > 0 && on < 1024) {
                if (bestCount[b] < 0) { bestCount[b] = on; bestFrame[b] = frames + probe; }
            }
        }
        if (!poke_frame(core)) break;
    }
    for (int b = 0; b < NBUF; b++) {
        if (bestCount[b] >= 0)
            printf("  %s: RANGE-LIKE  %d tiles reachable at frame %d\n",
                   bufs[b].name, bestCount[b], bestFrame[b]);
        else
            printf("  %s: no range-like reading in 1500 frames (always 0 or all-1024)\n",
                   bufs[b].name);
    }

    // Print the most promising buffer as a picture around the cursor.
    for (int b = 0; b < NBUF; b++) {
        if (bestCount[b] < 0) continue;
        uint32_t base = msm + bufs[b].off;
        int cx = R8(msm + 0x10 + 0x08), cy = R8(msm + 0x10 + 0x09);  // cursor tile
        printf("\n  %s map (rows %d..%d, cursor at %d,%d):\n",
               bufs[b].name, cy > 2 ? cy - 2 : 0, cy + 2, cx, cy);
        for (int y = (cy > 2 ? cy - 2 : 0); y <= cy + 2 && y < 32; y++) {
            char row[40]; int q = 0;
            for (int x = 0; x < 32; x++) {
                int idx = x | (y << 5);
                bool on = (R8(base + (idx >> 3)) >> (idx & 7)) & 1;
                row[q++] = (x == cx && y == cy) ? '@' : (on ? '#' : '.');
            }
            row[q] = 0;
            printf("    y=%-2d %s\n", y, row);
        }
        break;   // just the first range-like buffer is enough to confirm
    }

    // Per-tile impassability + occupancy, which the range algorithm consults.
    uint32_t p82c = R32(msm + 0x828);   // tile ids
    uint32_t p82cimp = msm + 0x82C;     // impassable flags
    uint32_t p028 = msm + 0x028;        // occupying unit id per tile
    if (InRam(p82c, 0x400)) {
        int imp = 0, occ = 0;
        for (int i = 0; i < 0x400; i++) {
            if (R8(p82cimp + i) & 0x80) imp++;
            if (R8(p028 + i) != 0) occ++;
        }
        printf("  impassable tiles (unk_82c & 0x80) = %d\n", imp);
        printf("  occupied tiles  (unk_028 != 0)     = %d\n", occ);
    }

    // ---- 3. text ----
    printf("\n---- dialogue / message text ----\n");
    // Scan the main message region for runs of printable text. FE11 stores ASCII,
    // so a string table shows up as dense printable data.
    struct Hit { uint32_t addr; int len; std::string s; };
    std::vector<Hit> hits;
    for (uint32_t a = 0x02240000; a < 0x02400000 && hits.size() < 400; a += 1) {
        if (!IsStr(a, 6, 80)) continue;
        // only start a run at its beginning
        if (IsStr(a - 1, 6, 80)) continue;
        int len = 0; while (len < 80 && R8(a + len)) len++;
        if (len < 8) continue;
        Hit h{a, len, PrintableAt(a, 80)};
        hits.push_back(h);
        a += len;
    }
    printf("  printable strings found: %zu\n", hits.size());
    for (size_t i = 0; i < hits.size() && i < 25; i++)
        printf("    %08X (%3d) \"%s\"\n", hits[i].addr, hits[i].len, hits[i].s.c_str());

    // The message-table pointer array: look for a run of plausible pointers whose
    // targets all begin with printable text.
    printf("\n---- candidate message tables ----\n");
    for (uint32_t a = 0x02200000; a < 0x02300000; a += 4) {
        if (!Plausible(R32(a))) continue;
        int good = 0;
        for (int i = 0; i < 16; i++)
            if (IsStr(R32(a + (uint32_t) i * 4), 4, 80)) good++;
        if (good >= 14) {
            printf("  table @%08X: %d/16 entries are text\n", a, good);
            for (int i = 0; i < 6; i++) {
                uint32_t t = R32(a + (uint32_t) i * 4);
                printf("      [%2d] %08X \"%s\"\n", i, t, PrintableAt(t, 60).c_str());
            }
            a += 0x40;   // skip past this table
        }
    }

    poke_destroy(core);
    return 0;
}
