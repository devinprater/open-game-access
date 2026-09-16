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

        // ------------------------------------------------------- allocation trace
        //
        // ⛔ WHEN A STRUCTURE APPEARS MATTERS MORE THAN WHETHER IT IS NON-ZERO. A
        // single end-of-run reading answers "is it populated now", which cannot
        // distinguish "the addresses are wrong" from "we have not reached the state
        // that creates them". Watching the party window fill in OVER TIME shows the
        // exact frame the game allocates it — and once that frame is known, the
        // plan can be re-run to that point and the addresses checked against a game
        // that is demonstrably in the right state.
        //
        // Sampling every 2000 frames keeps it cheap on a 128 MB ROM while still
        // resolving the transition to within a couple of seconds.
        if (f % 2000 == 0) {
            uint32_t pn = 0, in2 = 0;
            for (uint32_t a = 0x020CD000; a < 0x020CE000; a++) if (ProbeRead(a, 1)) pn++;
            for (uint32_t a = 0x020CC700; a < 0x020CC900; a++) if (ProbeRead(a, 1)) in2++;
            printf("[trace] f=%-6ld party=%-5u/4096 items=%-4u/512  zennyUSA=%u zennyEUR=%u hp0=%u\n",
                   f, pn, in2,
                   ProbeRead(0x020CC770, 4), ProbeRead(0x020CC370, 4),
                   ProbeRead(0x020CD300, 2));
            fflush(stdout);
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

    // ------------------------------------------------------- byte layout
    //
    // ⛔ THE COUNT IS NOT DIAGNOSTIC; THE PATTERN IS. "157 / 4096 nonzero" reads
    // the same whether the party array exists at a different offset or whether a
    // handful of scattered flags happen to be set. Dumping the bytes answers the
    // question the count cannot: IS THERE A RUN OF DATA, AND WHERE DOES IT START?
    //
    // The window is dumped in full (not just its first 256 bytes) because the first
    // run of this measurement returned ZERO data in the first 256 bytes at the
    // published base — which is itself the finding: the code list's base address is
    // pointing at empty memory, so the data it describes must live at some offset
    // inside (or beyond) the window. The first/last non-zero addresses locate it.
    printf("\n=== byte layout of the party window (0x020CD000 .. 0x020CE000) ===\n");
    {
        uint32_t firstnz = 0, lastnz = 0, totalnz = 0;
        for (uint32_t row = 0; row < 0x1000; row += 16) {
            uint32_t a = 0x020CD000 + row;
            uint32_t nz = 0;
            char hex[64]; int hx = 0;
            for (int i = 0; i < 16; i++) {
                uint8_t v = ProbeRead(a + i, 1);
                if (v) { nz++; totalnz++; if (!firstnz) firstnz = a + i;
                         lastnz = a + i; }
                hx += snprintf(hex + hx, sizeof(hex) - hx, "%02X ", (unsigned) v);
            }
            // print every row that has any data, plus its occupancy — a run of rows
            // with similar occupancy is a structure; isolated rows are noise
            if (nz) printf("  +%04X  %s  (%u/16)\n", row, hex, nz);
        }
        printf("  ---\n");
        printf("  total non-zero : %u / 4096\n", totalnz);
        if (totalnz)
            printf("  first non-zero : 0x%08X   last: 0x%08X   span: %u bytes\n",
                   firstnz, lastnz, lastnz - firstnz + 1);
        else
            printf("  the window is entirely empty this run\n");
    }

    // ------------------------------------------------------- string scan
    //
    // ⛔ CHARACTER NAMES ARE THE BEST STRUCTURE ORACLE IN AN RPG, and this window
    // proves the point: the byte dump found ASCII "Krillin" at 0x020CDE50. Names are
    // long, self-identifying, and land at a FIXED OFFSET inside each character
    // record — so the distance between one name and the next IS the record stride,
    // measured rather than assumed. That inverts the whole approach: instead of
    // scanning 128 KB for a plausible integer (which returned 348 useless
    // candidates), scan for strings and derive the layout from them.
    //
    // Once the names are located, the party array base and stride are known exactly,
    // and every scalar field (HP, Ki, level) can be read relative to a name we can
    // actually identify — which is a hypothesis with a checkable answer.
    printf("\n=== string scan: character names locate the record layout ===\n");
    {
        uint32_t found = 0;
        for (uint32_t a = 0x020CD000; a < 0x020CE000; a++) {
            // start of an ASCII run: printable, at least 3 chars, NUL-terminated
            uint8_t first = ProbeRead(a, 1);
            if (first < 0x41 || first > 0x7A) continue;      // 'A'..'z'
            if (a > 0x020CD000 && ProbeRead(a - 1, 1) >= 0x41 &&
                ProbeRead(a - 1, 1) <= 0x7A) continue;        // not the run start
            char s[40]; int n = 0;
            for (int i = 0; i < 39; i++) {
                uint8_t v = ProbeRead(a + i, 1);
                if (v < 0x20 || v > 0x7E) break;
                s[n++] = (char) v;
            }
            s[n] = 0;
            if (n >= 3) {
                printf("  0x%08X  \"%s\"  (len %d)\n", a, s, n);
                found++;
            }
        }
        if (!found) printf("  no ASCII strings in this window\n");
        else printf("  -- %u string(s); distance between them is the record stride\n", found);
    }

    // ------------------------------------------------------- party record dump
    //
    // ⛔ THIS IS THE PAYOFF OF THE STRING SCAN, AND THE REASON THE EARLIER SCAN
    // FAILED. Scanning for a "plausible integer" returned 348 candidates because
    // plausibility is not evidence. Scanning for NAMES returned four hits whose
    // spacing IS the record stride — measured, not assumed:
    //
    //     Goku 0x020CD774  Gohan 0x020CD9C0  Piccolo 0x020CDC0C  Krillin 0x020CDE58
    //     differences: 0x24C, 0x24C, 0x24C   <- the published stride, CONFIRMED
    //
    // The names sit 0x20 into each record, so the array base is 0x020CD754 — NOT
    // the 0x020CD300 the code list claims. That single fact explains every zero
    // reading: the code list's base is off, so every offset taken from it lands in
    // the wrong place.
    //
    // Now that the real base and stride are known, dump each record and print it
    // three ways (byte, u16, u32) so the scalar fields can be read directly.
    printf("\n=== party record dump (base 0x020CD754, stride 0x24C) ===\n");
    {
        const uint32_t PBASE = 0x020CD754, PSTRIDE = 0x24C;
        const char* nm[4] = {"rec0", "rec1", "rec2", "rec3"};
        for (int r = 0; r < 4; r++) {
            uint32_t b = PBASE + (uint32_t) r * PSTRIDE;
            printf("\n  --- %s base 0x%08X ---\n", nm[r], b);
            for (uint32_t o = 0; o < PSTRIDE; o += 16) {
                char hex[64]; int hx = 0; bool any = false;
                for (int i = 0; i < 16; i++) {
                    uint8_t v = ProbeRead(b + o + i, 1);
                    if (v) any = true;
                    hx += snprintf(hex + hx, sizeof(hex) - hx, "%02X ", (unsigned) v);
                }
                if (!any) continue;
                printf("    +%03X  %s", o, hex);
                // label the name field explicitly when we are on it
                if (o == 0x20) {
                    char s[20]; int n = 0;
                    for (int i = 0; i < 16; i++) {
                        uint8_t v = ProbeRead(b + 0x20 + i, 1);
                        if (!v) break;
                        s[n++] = (char) v;
                    }
                    s[n] = 0;
                    printf("  name=\"%s\"", s);
                }
                printf("\n");
            }
            // the fields an RPG party member must expose
            printf("    decoded: hp16@+00=%u  @+02=%u  @+04=%u  @+06=%u\n",
                   ProbeRead(b + 0x00, 2), ProbeRead(b + 0x02, 2),
                   ProbeRead(b + 0x04, 2), ProbeRead(b + 0x06, 2));
            printf("    decoded: u16@+10=%u @+20=%u @+30=%u @+40=%u @+50=%u @+60=%u\n",
                   ProbeRead(b + 0x10, 2), ProbeRead(b + 0x20 - 0x20, 2),
                   ProbeRead(b + 0x30, 2), ProbeRead(b + 0x40, 2),
                   ProbeRead(b + 0x50, 2), ProbeRead(b + 0x60, 2));
        }
    }

    // ------------------------------------------------------- per-character table
    //
    // ⛔ ANCHOR EVERY FIELD TO A NAME, NOT TO A GUESSED BASE. The 0x020CD754
    // "base" is derived from an ASSUMED name offset of 0x20; the name addresses
    // themselves are measured facts. Reading fields relative to each name keeps the
    // claim honest even if the assumed base is wrong — and the stride (0x24C,
    // confirmed across four consecutive names) is what proves these four records
    // are one array rather than four coincidences.
    //
    // The candidate stat block sits at name+0x1D8..name+0x1F0 as three consecutive
    // u32 triples. Three copies of each value is the normal shape for a DS RPG
    // (current / max / display copy), and the numbers here differ per character in
    // exactly the way party stats should.
    printf("\n=== per-character fields, anchored on the verified name addresses ===\n");
    {
        const uint32_t NAMES[4] = {0x020CD774, 0x020CD9C0, 0x020CDC0C, 0x020CDE58};
        printf("  %-9s %-11s %-9s %-9s %-9s %-9s %-9s %-9s\n",
               "name", "name addr", "+1D8", "+1DC", "+1E0", "+1E8", "+1EC", "+1F0");
        for (int r = 0; r < 4; r++) {
            uint32_t n = NAMES[r];
            char s[16]; int k = 0;
            for (int i = 0; i < 15; i++) {
                uint8_t v = ProbeRead(n + i, 1);
                if (!v) break;
                s[k++] = (char) v;
            }
            s[k] = 0;
            printf("  %-9s 0x%08X  %-9u %-9u %-9u %-9u %-9u %-9u\n",
                   s, n,
                   ProbeRead(n + 0x1D8, 4), ProbeRead(n + 0x1DC, 4),
                   ProbeRead(n + 0x1E0, 4), ProbeRead(n + 0x1E8, 4),
                   ProbeRead(n + 0x1EC, 4), ProbeRead(n + 0x1F0, 4));
        }
        printf("\n  ⛔ These are CANDIDATE stat fields, not confirmed HP/Ki. What is\n");
        printf("     confirmed is the layout: the four names are 0x24C apart, so this\n");
        printf("     IS a character array with the published stride, at a base the code\n");
        printf("     list gets wrong. Naming the fields needs one value changed in-game\n");
        printf("     (take damage, re-read) — never a plausible-looking number.\n");
    }

    // ------------------------------------------------------- roster vs party
    //
    // ⛔ ALL FOUR CHARACTERS HAVING STATS IS A RED FLAG, NOT A RESULT. In this game
    // Goku starts ALONE — Gohan, Piccolo and Krillin join later. If all four records
    // carry populated stats from frame 0, this table may be the character DATABASE
    // (a template roster) rather than the ACTIVE PARTY. Those are different things
    // and the adapter needs the party.
    //
    // Two cheap tests separate them, and neither needs a battle:
    //
    //   1. A per-record flag. A party-membership field would be set for Goku and
    //      clear for the rest at this point in the game. Dump the record head.
    //   2. Duplicate hunt. If these are base/template values, the LIVE value of
    //      Goku's HP should ALSO exist somewhere else (the real party copy). Search
    //      all of main RAM for the exact u32 and see where else it appears.
    //
    // ⛔ Do not call this "the party array" until one of these distinguishes them.
    printf("\n=== roster vs party: record heads + duplicate hunt ===\n");
    {
        const uint32_t NAMES[4] = {0x020CD774, 0x020CD9C0, 0x020CDC0C, 0x020CDE58};
        const char*   NMS[4]    = {"Goku", "Gohan", "Piccolo", "Krillin"};
        for (int r = 0; r < 4; r++) {
            uint32_t n = NAMES[r];
            printf("  %-8s head ", NMS[r]);
            for (uint32_t o = 0; o < 0x30; o++) printf("%02X", ProbeRead(n + o, 1));
            printf("\n");
        }
        // duplicate hunt for record 0's candidate stat value across main RAM
        uint32_t target = ProbeRead(NAMES[0] + 0x1D8, 4);
        printf("\n  hunting 0x%08X (value %u, Goku +1D8) across 0x02000000..0x02400000\n",
               target, target);
        if (target) {
            uint32_t hits = 0;
            for (uint32_t a = 0x02000000; a < 0x02400000; a += 4) {
                if (ProbeRead(a, 4) == target) {
                    if (hits < 24) printf("      0x%08X\n", a);
                    hits++;
                }
            }
            printf("      %u occurrence(s)\n", hits);
        } else {
            printf("      value is 0 — nothing to hunt\n");
        }
    }

    // ------------------------------------------------------- roster vs party (wide)
    //
    // ⛔ HOW MANY RECORDS ARE THERE? That question settles "party array vs character
    // database" far more cheaply than a battle. The 4 KB scan found exactly four
    // names, but it stopped at 0x020CE000 — and the four records starting at
    // 0x020CD754 already run to 0x020CE084, i.e. PAST that boundary. So the earlier
    // scan could have been truncated exactly where the next name would be.
    //
    // Scan a much wider window for ASCII names:
    //   * a full character DATABASE would hold every playable character in the game
    //     (Yamcha, Tien, Chiaotzu, ...) — many records;
    //   * an active PARTY array holds only the current party (Goku alone early on).
    //
    // ⛔ This is the question the adapter actually depends on. Reading template stats
    // as if they were live party HP would narrate numbers the player is not using.
    printf("\n=== wide string scan: how many character records exist? ===\n");
    {
        uint32_t found = 0, first = 0, last = 0;
        for (uint32_t a = 0x020C0000; a < 0x020D0000; a++) {
            uint8_t f = ProbeRead(a, 1);
            if (f < 0x41 || f > 0x5A) continue;                 // must START uppercase
            uint8_t p = (a > 0x020C0000) ? ProbeRead(a - 1, 1) : 0;
            if (p >= 0x41 && p <= 0x7A) continue;               // must be the run start
            char s[40]; int n = 0; bool allname = true;
            for (int i = 0; i < 39; i++) {
                uint8_t v = ProbeRead(a + i, 1);
                if (v >= 0x61 && v <= 0x7A) { s[n++] = (char) v; continue; }  // a-z
                if (v == 0x20 && n > 0)     { s[n++] = ' ';      continue; }  // space
                if (v >= 0x41 && v <= 0x5A && i == 0) { s[n++] = (char) v; continue; }
                if (v == 0) break;
                allname = false; break;
            }
            s[n] = 0;
            if (n >= 3 && n <= 20 && allname) {
                printf("  0x%08X  \"%s\"\n", a, s);
                if (!first) first = a;
                last = a;
                found++;
            }
        }
        printf("  -- %u name-like string(s)\n", found);
        if (found) {
            printf("  first 0x%08X  last 0x%08X  span %u bytes\n",
                   first, last, last - first + 1);
            // ⛔ DO NOT LET THE COUNT DECIDE. An earlier version of this probe
            // reasoned "found <= 8 ⇒ ACTIVE PARTY", and that was WRONG: the eight
            // records include Bubbles and Gregory, who are never playable, so the
            // table is a ROSTER. A count cannot tell a roster from a party — only
            // WHO is in it can. Print the membership question instead of an answer.
            printf("  ⛔ membership decides this, NOT the count: a real party holds only\n");
            printf("     characters the player can actually field. Non-playable names\n");
            printf("     (pets, NPCs, later-game characters) ⇒ this is a ROSTER.\n");
        }
    }

    // ------------------------------------------------------- live party hunt
    //
    // ⛔ THE ROSTER IS STATIC; THE PARTY IS THE THING THAT CHANGES. Confirming the
    // roster found the character DATA, but the adapter needs the ACTIVE PARTY —
    // who is in it right now and with what current HP. Two independent hypotheses
    // are cheap to test here, and both are checkable rather than plausible:
    //
    //   (1) POINTER LIST. An RPG party is very often an array of POINTERS to
    //       character records. If so, the roster base (0x020CD754) or the name
    //       addresses appear as 4-byte values somewhere in main RAM. That is a
    //       precise, falsifiable search — unlike "find a plausible integer".
    //   (2) LIVE COPY. A duplicate of a live value in a structurally DIFFERENT
    //       place (not on the 0x24C lattice) would be the current-stat block. The
    //       earlier duplicate hunt found 0x02054A10 as exactly such an outlier.
    //
    // ⛔ Only (1) is strong evidence. A value match proves nothing on its own — a
    // pointer to a known record does, because the odds of a random 4-byte word
    // equalling a specific RAM address are negligible.
    printf("\n=== live party hunt ===\n");
    {
        const uint32_t ROSTER_BASE = 0x020CD754;
        const uint32_t NAME0       = 0x020CD774;   // Goku
        printf("  (1) hunting POINTERS to the roster:\n");
        uint32_t hits = 0;
        for (uint32_t a = 0x02000000; a < 0x02400000; a += 4) {
            uint32_t v = ProbeRead(a, 4);
            if (v == ROSTER_BASE || v == NAME0 ||
                (v >= NAME0 && v <= 0x020CE800 &&
                 ((v - ROSTER_BASE) % 0x24C) == 0)) {
                if (hits < 20) printf("        0x%08X -> 0x%08X\n", a, v);
                hits++;
            }
        }
        printf("        %u pointer-shaped word(s)\n", hits);
        if (hits) printf("        ⇒ a pointer INTO a character record is strong evidence\n"
                         "          of a party/index list, because a random word matching\n"
                         "          a specific RAM address is vanishingly unlikely\n");
        else      printf("        ⇒ no pointers to the roster; the party is likely a\n"
                         "          separate copy rather than a list of pointers\n");

        printf("\n  (2) the 0x02054A10 outlier (a live 290 outside the roster):\n");
        for (uint32_t base = 0x02054A00; base < 0x02054A40; base += 16) {
            printf("        0x%08X  ", base);
            for (int i = 0; i < 16; i++) printf("%02X ", ProbeRead(base + i, 1));
            printf("\n");
        }
        printf("        as u32: ");
        for (int i = 0; i < 8; i++) printf("%u ", ProbeRead(0x02054A00 + i * 4, 4));
        printf("\n");
    }

    // ------------------------------------------------------- structure scan
    //
    // ⛔ WHEN THE PUBLISHED ADDRESSES READ ZERO BUT THE GAME CLEARLY HAS THE STATE
    // ON SCREEN, THE ADDRESSES ARE WRONG — so stop asking about them and go find
    // the structure instead. The screenshot is what settles this: an HP bar is
    // visible on the bottom screen, so party HP demonstrably EXISTS in RAM
    // somewhere, whether or not it lives at the code list's offset.
    //
    // The search uses the one structural fact the code lists DO give us: a
    // character record array with a fixed stride of 0x24C (from the Europe list's
    // `DC000000 0000024C`). A real party array shows the SAME field shape at
    // base, base+stride and base+2*stride — so look for an address where a
    // plausible value repeats across all three. That repetition is what makes this
    // a discovery rather than a match on noise.
    //
    // Plausibility for a DBZ RPG party at the start of the game: HP/Ki in the
    // low hundreds, level in 1..99, and NOT zero (an empty slot reads zero).
    printf("\n=== structure scan: looking for the party array ===\n");
    printf("  stride 0x%X, scanning 0x020C0000..0x020E0000\n", STRIDE);
    {
        struct Hit { uint32_t addr; uint32_t v0, v1, v2; };
        std::vector<Hit> hits;
        const uint32_t LO = 0x020C0000, HI = 0x020E0000;
        for (uint32_t a = LO; a < HI; a += 2) {
            uint32_t v0 = ProbeRead(a, 2);
            uint32_t v1 = ProbeRead(a + STRIDE, 2);
            uint32_t v2 = ProbeRead(a + 2 * STRIDE, 2);
            // All three must be non-zero, plausibly small, and DIFFERENT from each
            // other — three identical values is a repeated constant or a tile, not
            // three characters' HP.
            auto plausible = [](uint32_t v) { return v >= 1 && v <= 9999; };
            if (plausible(v0) && plausible(v1) && plausible(v2)
                && !(v0 == v1 && v1 == v2)) {
                hits.push_back({a, v0, v1, v2});
            }
        }
        printf("  candidates: %zu\n", hits.size());
        for (size_t i = 0; i < hits.size() && i < 15; i++)
            printf("    0x%08X  %u / %u / %u\n",
                   hits[i].addr, hits[i].v0, hits[i].v1, hits[i].v2);
        if (hits.empty())
            printf("    (none — the stride hypothesis did not hold in this window)\n");
        printf("  A candidate that repeats across all three records is the party array;\n");
        printf("  verify it by changing a value in-game and re-reading, never by trusting\n");
        printf("  the first plausible hit.\n");
    }

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
