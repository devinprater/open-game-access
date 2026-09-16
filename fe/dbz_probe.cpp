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
#include <algorithm>
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
    if (argc < 2) { printf("usage: dbz_probe <rom> [frames] [plan]\n"); return 2; }
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 6000;
    const char* planPath = (argc > 3) ? argv[3] : nullptr;

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

    // ------------------------------------------------------------------- input
    //
    // ⛔ WITHOUT INPUT THE ADDRESSES READ 0, AND THAT IS EXPECTED, NOT A FAILURE.
    // The first run drove no input at all, so the game stayed on its title screen
    // and the party structure was never allocated — every published address read
    // exactly 0 while a control address returned real data. That is the documented
    // "region entirely zero" signal. Measuring the addresses only means something
    // once the game is actually in a battle.
    //
    // The plan format is the one the Fire Emblem plans already use:
    //     KEY <frame> <BUTTON> <0|1>
    struct K { long f; int b; int d; };
    std::vector<K> keys;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (!f) {
            printf("warning: plan %s not readable — no input will be sent\n", planPath);
        } else {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* c = strchr(line, '#'); if (c) *c = 0;
                char cmd[16] = {0}, btn[16] = {0}; long fr; int d;
                if (sscanf(line, "%15s %ld %15s %d", cmd, &fr, btn, &d) == 4
                    && !strcasecmp(cmd, "KEY")) {
                    static const char* nm[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
                    for (int i = 0; i < 12; i++)
                        if (!strcasecmp(btn, nm[i])) { K k; k.f = fr; k.b = i; k.d = d; keys.push_back(k); }
                }
            }
            fclose(f);
            printf("input plan : %s (%zu key events)\n\n", planPath, keys.size());
        }
    } else {
        printf("input plan : NONE — the game will sit on its title screen and every\n");
        printf("             structure will read 0. Pass a plan to reach a battle.\n\n");
    }

    // Order the events by frame. A plan that is not already frame-ordered silently
    // drops events, because the driver only fires when an event's frame equals the
    // current one.
    std::sort(keys.begin(), keys.end(), [](const K& a, const K& b) { return a.f < b.f; });

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
        // Fire any key events scheduled for this frame.
        static size_t ki = 0;
        while (ki < keys.size() && keys[ki].f == f) {
            poke_set_button(core, keys[ki].b, keys[ki].d != 0);
            ki++;
        }
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

    // ------------------------------------------------------- window population
    //
    // ⛔ "READS ZERO FOR THE WHOLE RUN" IS NOT PROOF AN ADDRESS IS WRONG — only that
    // nothing wrote that byte. The way to tell the two apart is to measure how
    // POPULATED the surrounding region is:
    //
    //   * an entirely zero window  -> the structure was never allocated (the game
    //     never reached the state that creates it);
    //   * a busy window with different values in it -> the structure EXISTS and the
    //     claimed address is simply wrong (or the code list is for another REGION).
    //
    // This single measurement is what separates "wrong address" from "not there
    // yet", and the two demand completely different next actions.
    printf("\n=== window population (is the region allocated at all?) ===\n");
    struct Win { const char* what; uint32_t base; uint32_t len; };
    Win wins[] = {
        {"party block (USA 0x020CD000-0x020CE000)", 0x020CD000, 0x1000},
        {"item block  (USA 0x020CC700-0x020CC900)", 0x020CC700, 0x0200},
        {"item block  (EUR 0x020CC300-0x020CC500)", 0x020CC300, 0x0200},
        {"control     (0x02000000-0x02001000)",     0x02000000, 0x1000},
    };
    for (Win& wn : wins) {
        uint32_t nonzero = 0, distinct_sample = 0, first_vals[8] = {0};
        for (uint32_t a = wn.base; a < wn.base + wn.len; a++) {
            uint8_t v = ProbeRead(a, 1);
            if (v) {
                if (nonzero < 8) first_vals[nonzero] = v;
                nonzero++;
            }
        }
        printf("  %-40s nonzero %5u / %5u bytes", wn.what, nonzero, wn.len);
        if (nonzero && nonzero < 8) {
            printf("   first: ");
            for (uint32_t i = 0; i < nonzero; i++) printf("%02X ", (unsigned) first_vals[i]);
        }
        printf("\n");
    }
    printf("\n  An all-zero window means the structure was never created by this run.\n");
    printf("  A busy window with real values means the addresses to check are wrong.\n");

    // ------------------------------------------------------------- screenshot
    //
    // ⛔ A MEMORY READ CANNOT TELL YOU WHERE THE GAME IS. The project's own rule:
    // bracket every RAM claim with a picture, because "memory says what was stored,
    // only the picture says what the game was DOING". A reader can be perfectly
    // correct while the game sits on a title screen — and the party block reading
    // sparse is exactly what that looks like.
    //
    // This is why the population numbers above are not self-explanatory: without
    // knowing which screen the game is on, "157/4096 nonzero" could mean "the
    // structure is half-built" or "we never left the title".
    if (const char* shot = getenv("DBZ_SHOT")) {
        int w = 0, h = 0;
        // ⛔ ORDER MATTERS AND IT IS NOT OBVIOUS. `poke_framebuffer_ptr` is
        // STATEFUL: it returns nullptr unless the core's `frameScreen` already
        // equals the screen asked for, and that field is set by
        // `poke_framebuffer()`. Asking for the pointer FIRST invalidates nothing
        // visibly — you get a valid width/height with a null pointer, which reads
        // as "the framebuffer is broken" rather than "you called them backwards".
        // So: select the screen, THEN take its pointer.
        if (poke_framebuffer(core, 0, &w, &h) && w > 0 && h > 0) {
            const uint8_t* px = poke_framebuffer_ptr(core, 0);
            if (!px) {
                printf("\nscreenshot: framebuffer selected (%dx%d) but pointer is null\n", w, h);
            } else {
                FILE* f = fopen(shot, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", w, h);
                    for (int i = 0; i < w * h; i++) {
                        // The core hands back RGBA8888; PPM wants RGB.
                        fwrite(px + i * 4, 1, 3, f);
                    }
                    fclose(f);
                    printf("\nscreenshot: %s (%dx%d)\n", shot, w, h);
                } else {
                    printf("\nscreenshot: could not write %s\n", shot);
                }
            }
        } else {
            printf("\nscreenshot: framebuffer unavailable (w=%d h=%d)\n", w, h);
        }
    }

    poke_stop(core);
    poke_destroy(core);
    return 0;
}
