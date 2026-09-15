/*
 * purecore.cpp — absolutely no Lua, no shim, no accessibility script.
 *
 * Everything so far has had the compat shim loaded. The shim implements
 * BizHawk's memory API, and some of those calls WRITE to emulated memory. If a
 * shim write is corrupting the running game, every previous test would show
 * exactly what we have seen. This run removes Lua entirely (a script that ends
 * immediately, so there is no coroutine to resume per frame).
 *
 * If the game renders here  → the Lua layer is corrupting the game.
 * If it is still blank      → the bug is in my core wiring, and Lua is innocent.
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

static int distinct(const unsigned char* px)
{
    unsigned seen[64]; int n = 0;
    if (!px) return -1;
    for (int i = 0; i < 256 * 192; i += 13)
    {
        unsigned v = (px[i*4] << 16) | (px[i*4+1] << 8) | px[i*4+2];
        int f = 0;
        for (int k = 0; k < n; k++) if (seen[k] == v) { f = 1; break; }
        if (!f && n < 64) seen[n++] = v;
    }
    return n;
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }

    /* A script that finishes immediately: nothing to resume, so no Lua code ever
       runs during the frame loop. */
    poke_set_script(core, "local a = 1\n");
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    long frames = (argc > 2) ? atol(argv[2]) : 6000;

    for (long f = 0; f < frames; f++)
    {
        if (f == 200) poke_set_button(core, POKE_BTN_A, true);
        if (f == 215) poke_set_button(core, POKE_BTN_A, false);
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
    }

    int w = 0, h = 0;
    poke_framebuffer(core, 0, &w, &h);
    const unsigned char* t = poke_framebuffer_ptr(core, 0);
    poke_framebuffer(core, 1, &w, &h);
    const unsigned char* b = poke_framebuffer_ptr(core, 1);

    printf("\n==== PURE CORE (no Lua at all), %ld frames ====\n", frames);
    printf("top colours=%d  bottom colours=%d\n", distinct(t), distinct(b));
    printf("VRAM_A nonzero: %s   VRAM_C nonzero: %s\n",
           ({int nz=0; for(int i=0;i<4096;i++) if(nds->GPU.VRAM_A[i]){nz=1;break;} nz;}) ? "YES":"no",
           ({int nz=0; for(int i=0;i<4096;i++) if(nds->GPU.VRAM_C[i]){nz=1;break;} nz;}) ? "YES":"no");
    printf("total scanlines last frame: %d   VCount: %d\n", nds->GPU.TotalScanlines, nds->GPU.VCount);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
