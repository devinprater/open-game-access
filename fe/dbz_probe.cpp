/*
 * dbz_probe.cpp — verify the publicly documented Action Replay addresses for
 * Dragon Ball Z: Attack of the Saiyans (BRPE, USA).
 *
 * WHY THIS IS THE FIRST STEP, NOT THE LAST: published AR codes are ABSOLUTE RAM
 * addresses, which makes them the only public memory documentation this game has.
 * But a code list is a CLAIM. This project has already been burned by one tagged
 * for the wrong REGION (a whole Europe code list reading zero against a USA ROM),
 * and by "documented" state bits that were simply wrong. So every address here is
 * measured, not trusted.
 *
 * WHAT IT MEASURES, per candidate address:
 *   - first / last value over the run
 *   - how many DISTINCT values it held
 *   - how many times it CHANGED
 * A right address holds a plausible value for its meaning AND moves when the game
 * moves. An address that never changes across a whole run is not proof it is
 * wrong — only that nothing touched it in that window, which is why the frame
 * count is reported alongside.
 *
 * ⛔ THE PARTY-STAT BLOCK IS THE INTERESTING ONE. The AR codes show a clear
 * structure: seven character records at a FIXED STRIDE (0x24C), with HP/Ki/stats/
 * level/AP at consistent offsets inside each. If that reproduces here, the game's
 * party layout is known well enough to read party HP and Ki for a reader.
 */
#include "pokecore.h"

// The probe dereferences the console's Main RAM directly, so the real melonDS
// header must be on the include path — the same arrangement the other host probes
// (fechapter, feterrain2) use. Reading raw MainRAM is correct HERE because this is
// a discovery tool measuring arbitrary candidate addresses; the app's own adapters
// go through the bounds-checked host reads instead.
#include "NDS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static uint8_t* gRam = nullptr;

static bool InRam(uint32_t a, uint32_t n)
{
    const uint32_t BASE = 0x02000000u, SIZE = 0x400000u;
    if (a < BASE) return false;
    const uint32_t off = a - BASE;
    return (uint64_t) off + n <= SIZE;
}

static uint32_t ProbeRead(uint32_t a, int width)
{
    if (width == 1) return InRam(a, 1) ? gRam[a - 0x02000000u] : 0;
    if (width == 2) return InRam(a, 2) ? (uint32_t) *(uint16_t*) &gRam[a - 0x02000000u] : 0;
    return InRam(a, 4) ? *(uint32_t*) &gRam[a - 0x02000000u] : 0;
}

// ------------------------------------------------------------------ candidates
struct Watch {
    const char* what;
    uint32_t    addr;
    int         width;      // bytes
    uint32_t    first;
    uint32_t    last;
    uint32_t    changes;
    std::vector<uint32_t> seen;
    Watch(const char* w, uint32_t a, int wd)
        : what(w), addr(a), width(wd), first(0), last(0), changes(0) {}
};

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: dbz_probe <rom> [frames]\n"); return 2; }
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 6000;

    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, nullptr)) {
        printf("load failed: %s\n", poke_last_error(core));
        return 1;
    }

    // MainRAM must be latched BEFORE the first frame: melonDS can reallocate it on
    // reset, so holding a stale pointer would read freed memory and report
    // plausible nonsense.
    gRam = poke_debug_nds(core)->MainRAM;

    // poke_start REFUSES to run without a bundled script ("No accessibility script
    // was bundled"), and the check happens before any frame runs — so a purely
    // native probe cannot start the console at all.
    //
    // This is not a workaround for a limitation: the script engine IS how frames
    // are advanced (emu.frameadvance() yields the coroutine, and the frame loop
    // resumes it). An idle looping script is therefore the correct minimum, not a
    // stub. The `emu` global comes from the compat shim, so the shim is loaded
    // first when PA_SHIM is set — the same arrangement fe_access.cpp uses, and the
    // same trap that produced an all-zeros table once before: a "minimal" inline
    // script dies on `attempt to index a nil value (global 'emu')`.
    {
        std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) {
            FILE* f = fopen(shim, "rb");
            if (f) { char b[65536]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n); fclose(f); }
            else printf("warning: PA_SHIM set but unreadable (%s) — the script may fail\n", shim);
        }
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }

    if (!poke_start(core)) {
        printf("start failed: %s\n", poke_last_error(core));
        return 1;
    }

    printf("=== Dragon Ball Z: Attack of the Saiyans — AR address verification ===\n");
    printf("rom    : %s\n", rom);
    printf("frames : %ld\n\n", frames);

    // The addresses the published code lists give, grouped as the code lists group
    // them. The USA and Europe lists DISAGREE by a constant offset, which is itself
    // worth measuring: a uniform shift means one list is a port of the other.
    const uint32_t PARTY = 0x020CD300;
    const uint32_t STRIDE = 0x24C;      // from the Europe list's DC000000 0000024C

    std::vector<Watch> w;
    // --- USA list ---
    w.push_back(Watch("zenny (USA)",        0x020CC770, 4));
    w.push_back(Watch("consumables (USA)",  0x020CC798, 4));
    w.push_back(Watch("capsules (USA)",     0x020CC898, 4));
    w.push_back(Watch("party rec0 HP",      PARTY + 0x00, 2));
    w.push_back(Watch("party rec0 Ki",      PARTY + 0x10, 2));
    w.push_back(Watch("party rec0 lvl",     PARTY + 0x68, 1));
    w.push_back(Watch("party rec0 AP",      PARTY + 0x1A4, 2));
    // --- Europe list, for the region comparison ---
    w.push_back(Watch("zenny (EUR)",        0x020CC370, 4));
    // --- the STRIDE hypothesis: does rec1 sit exactly one record later? ---
    w.push_back(Watch("party rec1 HP",      PARTY + STRIDE, 2));
    w.push_back(Watch("party rec2 HP",      PARTY + 2 * STRIDE, 2));
    // --- a control: an address that SHOULD be untouched, to prove the probe can
    // report "nothing here" rather than always finding something.
    w.push_back(Watch("control (zero page)", 0x02000010, 4));

    for (long f = 0; f < frames; f++) {
        if (!poke_frame(core)) { printf("stopped at frame %ld\n", f); break; }
        if (f % 60 == 0) {   // once per second: enough to see motion, cheap enough
            for (size_t i = 0; i < w.size(); i++) {
                Watch& it = w[i];
                uint32_t v = ProbeRead(it.addr, it.width);
                if (f == 0) { it.first = v; it.last = v; }
                if (v != it.last) { it.changes++; it.last = v; }
                bool dup = false;
                for (size_t k = 0; k < it.seen.size(); k++)
                    if (it.seen[k] == v) { dup = true; break; }
                if (!dup && it.seen.size() < 64) it.seen.push_back(v);
            }
        }
    }

    printf("%-22s %-12s %-12s %-12s %-9s %s\n",
           "watch", "address", "first", "last", "changes", "distinct");
    printf("--------------------------------------------------------------------------------------\n");
    for (size_t i = 0; i < w.size(); i++) {
        Watch& it = w[i];
        printf("%-22s 0x%08X %-12u %-12u %-9u %zu\n",
               it.what, it.addr, it.first, it.last, it.changes, it.seen.size());
    }

    printf("\n=== reading the table ===\n");
    printf("  changes > 0   -> the game WRITES this address: it is live state.\n");
    printf("  changes == 0  -> nothing touched it in this window (NOT proof of a wrong\n");
    printf("                   address; say it that way).\n");
    printf("  distinct > 1  -> it varies, so it is plausible game state rather than a\n");
    printf("                   constant or an uninitialised zero page.\n");

    poke_stop(core);
    poke_destroy(core);
    return 0;
}
