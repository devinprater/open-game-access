/*
 * timingprobe.cpp — are the memory timings initialised?
 *
 * If ARM7MemTimings / ARM9MemTimings are zero, every instruction adds 0 cycles,
 * ARM7Timestamp never reaches its target, and NDS::RunFrame's inner
 * `while (ARM7Timestamp < target)` loops forever. That would look exactly like
 * what we see: real code executing at real PCs, but frame 0 never completing
 * and 0 cycles ever elapsing.
 */
#include "pokecore.h"
#include "NDS.h"
#include "ARM.h"
#include <stdio.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static void on_log(const char* t, void* u) { (void)t; (void)u; }

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    if (!nds) { printf("no nds\n"); return 1; }

    printf("=== ARM7MemTimings (should be nonzero) ===\n");
    for (int i = 0; i < 3; i++)
        printf("  [%d] = %d %d %d %d\n", i,
               nds->ARM7MemTimings[i][0], nds->ARM7MemTimings[i][1],
               nds->ARM7MemTimings[i][2], nds->ARM7MemTimings[i][3]);

    printf("=== ARM9MemTimings ===\n");
    for (int i = 0; i < 3; i++)
        printf("  [%d] = %d %d %d %d\n", i,
               nds->ARM9MemTimings[i][0], nds->ARM9MemTimings[i][1],
               nds->ARM9MemTimings[i][2], nds->ARM9MemTimings[i][3]);

    printf("\n=== ARM9 CP15 / regions ===\n");
    printf("  ARM9ClockShift = %d\n", nds->ARM9ClockShift);
    printf("  MainRAMMask    = %08X\n", nds->MainRAMMask);
    printf("  ARM9.Cycles    = %d\n", nds->ARM9.Cycles);
    printf("  ARM7.Cycles    = %d\n", nds->ARM7.Cycles);

    poke_destroy(core);
    return 0;
}
