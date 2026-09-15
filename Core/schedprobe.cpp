/*
 * schedprobe.cpp — WHERE is the frame loop stuck?
 *
 * Samples the scheduler state per frame. The candidate stalls are all
 * distinguishable:
 *   CPUStop_Sleep set forever   → sleep mode entered and never woken
 *   totalScanlines == 0 forever → the LCD event never advances a scanline
 *   GXStall                     → the 3D FIFO claims to be full forever
 *   SysTimestamp not moving     → no cycles are being consumed at all
 */
#include "pokecore.h"
#include "NDS.h"
#include "GPU.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); }

/* CPUStop flag names (NDS.h) */
static void flags(unsigned v, char* out, size_t n)
{
    out[0] = 0;
    if (v & 0x01) strncat(out, "Sleep ", n-strlen(out)-1);
    if (v & 0x02) strncat(out, "Wakeup ", n-strlen(out)-1);
    if (v & 0x04) strncat(out, "GXStall ", n-strlen(out)-1);
    if (v & 0x08) strncat(out, "DMA9 ", n-strlen(out)-1);
    if (v & 0x10) strncat(out, "DMA7 ", n-strlen(out)-1);
    if (v & 0x20) strncat(out, "Wifi ", n-strlen(out)-1);
    if (!out[0]) strncpy(out, "-", n);
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }

    static char script[200000];
    const char* sh = getenv("PA_SCRIPT");
    if (!sh) sh = getenv("PA_SHIM");
    size_t n = 0;
    if (sh) { FILE* f=fopen(sh,"rb"); if(f){n=fread(script,1,sizeof(script)-64,f);fclose(f);} }
    script[n]=0; strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    long frames = (argc > 2) ? atol(argv[2]) : 3000;
    char fl[64];

    printf("frame | CPUStop           | scanlines | VCount | SysTs        | ARM9.PC  | A9h A7h\n");
    printf("------------------------------------------------------------------------------\n");
    for (long f = 0; f < frames; f++)
    {
        if (f == 200) poke_set_button(core, POKE_BTN_A, true);
        if (f == 215) poke_set_button(core, POKE_BTN_A, false);
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }

        if (f % 500 == 0)
        {
            flags(nds->CPUStop, fl, sizeof fl);
            printf("%-5ld | %-16s | %-9d | %-6d | %-12s | %08X | %d %d\n",
                   f, fl, nds->GPU.TotalScanlines, nds->GPU.VCount,
                   "-",
                   nds->ARM9.R[15],
                   (int)!!(nds->ARM9.CPSR & 0x20), (int)!!(nds->ARM7.CPSR & 0x20));
            fflush(stdout);
        }
    }
    printf("== frames completed: %llu\n", poke_frames_completed(core));
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
