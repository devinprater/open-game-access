/*
 * ramscan.cpp — is a claimed address REGION populated at all?
 *
 * When a published cheat address reads zero, there are two very different
 * explanations and they call for different next steps:
 *   (a) the address belongs to a DIFFERENT ROM revision/region, or
 *   (b) the game simply had not allocated that structure yet at the point we
 *       sampled (still in the title/intro).
 *
 * Scanning a window around the claim distinguishes them: a region that is
 * entirely zero is "nothing here yet"; a region that is busy while the specific
 * offsets stay zero is a much stronger hint of a wrong address or wrong version.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = atol(argv[2]);
    uint32_t base = (uint32_t) strtoul(argv[3], nullptr, 16);
    uint32_t span = (uint32_t) strtoul(argv[4], nullptr, 16);   // window size
    int dumpRows = (argc > 5) ? atoi(argv[5]) : 0;

    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);
    {
        static std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f = fopen(shim,"rb"); if (f) { char b[65536]; size_t n; while ((n=fread(b,1,sizeof(b),f))>0) s.append(b,n); fclose(f);} }
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }
    if (!poke_start(core)) { printf("start fail\n"); return 1; }

    for (long f = 0; f < frames; f++) {
        if (f == 300)  poke_set_button(core, POKE_BTN_A, true);
        if (f == 318)  poke_set_button(core, POKE_BTN_A, false);
        if (f == 900)  poke_set_button(core, POKE_BTN_START, true);
        if (f == 918)  poke_set_button(core, POKE_BTN_START, false);
        for (long t = 1500; t <= 4000; t += 250) {
            if (f == t)      poke_set_button(core, POKE_BTN_A, true);
            if (f == t + 18) poke_set_button(core, POKE_BTN_A, false);
        }
        if (!poke_frame(core)) break;
    }

    const uint32_t RAM_BASE = 0x02000000, RAM_SIZE = 0x400000;
    uint8_t* ram = nds->MainRAM;

    long nonzero = 0, total = 0;
    for (uint32_t i = 0; i < span; i++) {
        uint32_t a = base + i;
        if (a < RAM_BASE || a - RAM_BASE >= RAM_SIZE) continue;
        total++;
        if (ram[a - RAM_BASE]) nonzero++;
    }
    printf("frames=%llu  window=%08X..%08X  nonzero=%ld/%ld (%.1f%%)\n",
           poke_frames_completed(core), base, base + span - 1, nonzero, total,
           total ? (100.0 * nonzero / total) : 0.0);

    if (dumpRows > 0) {
        printf("\n  nonzero bytes in the window (first %d):\n", dumpRows);
        int shown = 0;
        for (uint32_t i = 0; i < span && shown < dumpRows; i++) {
            uint32_t a = base + i;
            if (a - RAM_BASE >= RAM_SIZE) break;
            if (ram[a - RAM_BASE]) { printf("    %08X = %02X\n", a, ram[a - RAM_BASE]); shown++; }
        }
    }
    poke_destroy(core);
    return 0;
}
