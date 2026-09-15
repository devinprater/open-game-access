/*
 * powerprobe.cpp — force the LCD power on and see whether the game renders.
 *
 * ScreensEnabled = !!(PowerControl9 & 1), set only by GPU::SetPowerCnt.
 * Reset() zeroes PowerControl9; SetupDirectBoot() sets 0x820F. If the game's own
 * power-on write never lands, nothing can ever draw.
 *
 * This harness (a) reports ScreensEnabled, then (b) forces 0x820F partway
 * through and reports again. Rendering appearing only after the forced write
 * means the game's write is not reaching the GPU.
 */
#include "pokecore.h"
#include "NDS.h"
#include "GPU.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; }

static int distinct(const unsigned char* px)
{
    unsigned seen[64]; int n = 0;
    if (!px) return -1;
    for (int i = 0; i < 256 * 192; i += 7)
    {
        unsigned v = (px[i*4] << 16) | (px[i*4+1] << 8) | px[i*4+2];
        int f = 0; for (int k = 0; k < n; k++) if (seen[k] == v) { f = 1; break; }
        if (!f && n < 64) seen[n++] = v;
    }
    return n;
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }
    poke_set_script(core, "local a=1\n");
    if (!poke_start(core)) { printf("start fail\n"); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    int w=0,h=0;

    for (long f = 0; f < 4000; f++)
    {
        if (!poke_frame(core)) break;
        if (f == 300)
        {
            printf("BEFORE force: ScreensEnabled=%d  top_colours=%d\n",
                   (int) nds->GPU.ScreensEnabled,
                   distinct(poke_framebuffer_ptr(core, 0)));
            printf(">>> forcing GPU.SetPowerCnt(0x820F)\n");
            nds->GPU.SetPowerCnt(0x820F);
            printf("AFTER force:  ScreensEnabled=%d\n", (int) nds->GPU.ScreensEnabled);
        }
    }

    poke_framebuffer(core, 0, &w, &h);
    printf("\nfinal: top_colours=%d bot_colours=%d  ScreensEnabled=%d  VCount=%d\n",
           distinct(poke_framebuffer_ptr(core, 0)),
           distinct(poke_framebuffer_ptr(core, 1)),
           (int) nds->GPU.ScreensEnabled, nds->GPU.VCount);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
