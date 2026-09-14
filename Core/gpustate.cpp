/*
 * gpustate.cpp — why is the screen always white?
 *
 * A pure-white 256x192 framebuffer that never changes usually means the LCD is
 * powered down (PowerControl9 bits) so the GPU never renders a scanline. This
 * prints the power/display registers and the renderer identity.
 */
#include "pokecore.h"
#include "NDS.h"
#include "GPU.h"
#include <stdio.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); fflush(stdout); }
static void on_log(const char* t, void* u) { (void)t;(void)u; }

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    printf("After load:\n");
    printf("  PowerControl9 = %04X  (bit15=LCD on, low bits = engine A/B on; reset value 820F)\n", nds->PowerControl9);
    printf("  PowerControl7 = %04X\n", nds->PowerControl7);
    printf("  ARM9ClockShift = %d  MainRAMMask = %08X\n", nds->ARM9ClockShift, nds->MainRAMMask);
    printf("  DISPCNT_A = %08X\n", nds->GPU.DispCnt[0]);
    printf("  DISPCNT_B = %08X\n", nds->GPU.DispCnt[1]);
    printf("  TotalScanlines = %d\n", nds->GPU.TotalScanlines);

    int w=0,h=0;
    void* top = NULL; void* bot = NULL;
    bool okfb = nds->GPU.GetFramebuffers(&top, &bot);
    printf("  GetFramebuffers -> %d  top=%p bottom=%p\n", okfb, top, bot);

    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f = fopen(shim,"rb"); if (f) { n = fread(script,1,sizeof(script)-64,f); fclose(f);} }
    script[n] = 0;
    strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    printf("\nAfter start:\n");
    printf("  PowerControl9 = %04X\n", nds->PowerControl9);
    printf("  DISPCNT_A = %08X  DISPCNT_B = %08X\n", nds->GPU.DispCnt[0], nds->GPU.DispCnt[1]);

    for (int i = 0; i < 600; i++) poke_frame(core);

    printf("\nAfter 600 frames:\n");
    printf("  PowerControl9 = %04X\n", nds->PowerControl9);
    printf("  DISPCNT_A = %08X  DISPCNT_B = %08X\n", nds->GPU.DispCnt[0], nds->GPU.DispCnt[1]);
    printf("  TotalScanlines = %d\n", nds->GPU.TotalScanlines);
    (void)w; (void)h;
    poke_destroy(core);
    return 0;
}
