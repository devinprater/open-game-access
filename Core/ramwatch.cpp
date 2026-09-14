/*
 * ramwatch.cpp — verify published cheat addresses against a LIVE game.
 *
 * An Action Replay code is a claim about an absolute RAM address. Those claims
 * are exactly what an accessibility reader needs (they tell you where the game
 * keeps its state), but internet lists are unverified, version-specific, and
 * often stale or mistyped. Reading them in a running console is what turns a
 * claim into a mapping:
 *
 *   * a WRONG address holds 0 forever or holds noise that never correlates
 *   * a RIGHT address holds a plausible value for its meaning (money in a sane
 *     range, HP <= max HP, a pointer into RAM)
 *
 * Usage: ramwatch <rom> <frames> <out.csv> addr:width[:label] ...
 *   width is the byte count (1,2,4); label is free text.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

struct Watch {
    uint32_t addr;
    int width;
    std::string label;
    // observations
    uint64_t first = 0, last = 0;
    int changed = 0;
    uint64_t distinct[16] = {0};
    int nDistinct = 0;
};

static uint64_t ReadAt(uint8_t* ram, uint32_t a, int w)
{
    uint64_t v = 0;
    for (int i = 0; i < w; i++) v |= (uint64_t) ram[a + i] << (8 * i);
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s <rom> <frames> <out.csv> addr:width[:label]...\n", argv[0]); return 2; }
    const char* rom = argv[1];
    long frames = atol(argv[2]);
    const char* csv = argv[3];

    std::vector<Watch> ws;
    for (int i = 4; i < argc; i++) {
        Watch w;
        char buf[256]; snprintf(buf, sizeof(buf), "%s", argv[i]);
        char* c1 = strchr(buf, ':'); if (!c1) continue;
        *c1 = 0; w.addr = (uint32_t) strtoul(buf, nullptr, 16);
        char* c2 = strchr(c1 + 1, ':');
        if (c2) { *c2 = 0; w.label = c2 + 1; }
        w.width = atoi(c1 + 1);
        ws.push_back(w);
    }

    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);
    // Load the shim + a tiny yielder: emu/joypad come from the shim, not the core.
    {
        static std::string script;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f = fopen(shim, "rb"); if (f) { char b[65536]; size_t n; while ((n = fread(b,1,sizeof(b),f)) > 0) script.append(b,n); fclose(f); } }
        script += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, script.c_str());
    }
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    FILE* out = fopen(csv, "w");
    fprintf(out, "frame");
    for (auto& w : ws) fprintf(out, ",%s(%06X/%d)", w.label.c_str(), w.addr, w.width);
    fprintf(out, "\n");

    const uint32_t RAM_BASE = 0x02000000;
    const uint32_t RAM_SIZE = 0x400000;

    for (long f = 0; f < frames; f++)
    {
        if (f == 300)  poke_set_button(core, POKE_BTN_A, true);
        if (f == 318)  poke_set_button(core, POKE_BTN_A, false);
        if (f == 900)  poke_set_button(core, POKE_BTN_START, true);
        if (f == 918)  poke_set_button(core, POKE_BTN_START, false);
        for (long t = 1500; t <= 3500; t += 250) {
            if (f == t)     poke_set_button(core, POKE_BTN_A, true);
            if (f == t + 18) poke_set_button(core, POKE_BTN_A, false);
        }
        if (!poke_frame(core)) break;

        if (f % 60 != 0 && f != frames - 1) continue;

        uint8_t* ram = nds->MainRAM;
        fprintf(out, "%ld", f);
        for (auto& w : ws)
        {
            uint64_t v = 0;
            if (w.addr >= RAM_BASE && w.addr - RAM_BASE + w.width <= RAM_SIZE)
                v = ReadAt(ram, w.addr - RAM_BASE, w.width);
            fprintf(out, ",%llu", (unsigned long long) v);
            if (f == 0) w.first = v;
            if (v != w.last && w.last != 0) w.changed++;
            w.last = v;
            bool seen = false;
            for (int k = 0; k < w.nDistinct; k++) if (w.distinct[k] == v) { seen = true; break; }
            if (!seen && w.nDistinct < 16) w.distinct[w.nDistinct++] = v;
        }
        fprintf(out, "\n");
    }
    fclose(out);

    printf("frames=%llu\n\n", poke_frames_completed(core));
    printf("%-22s %-12s %-4s %-20s %-20s %s\n", "label", "address", "w", "first", "last", "distinct/changed");
    printf("%-22s %-12s %-4s %-20s %-20s %s\n", "-----", "-------", "-", "-----", "----", "----------------");
    for (auto& w : ws)
    {
        printf("%-22s 0x%08X   %-4d %-20llu %-20llu %d/%d\n",
               w.label.c_str(), w.addr, w.width,
               (unsigned long long) w.first, (unsigned long long) w.last,
               w.nDistinct, w.changed);
    }
    poke_destroy(core);
    return 0;
}
