/*
 * renderprobe.cpp — does the game render now that the keypad is fixed?
 *
 * No environment variables, no Lua: a script that ends immediately, so the frame
 * loop is pure core. Reports VRAM bank mapping, whether VRAM actually holds
 * graphics data, and dumps both screens to PPM so they can be LOOKED at.
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

static int nonzero(const unsigned char* p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return 1;
    return 0;
}
static int distinct(const unsigned char* px)
{
    unsigned seen[128]; int n = 0;
    if (!px) return -1;
    for (int i = 0; i < 256*192; i += 5)
    {
        unsigned v = (px[i*4]<<16)|(px[i*4+1]<<8)|px[i*4+2];
        int f = 0; for (int k = 0; k < n; k++) if (seen[k]==v) { f=1; break; }
        if (!f && n < 128) seen[n++] = v;
    }
    return n;
}
static void dump(const char* path, const unsigned char* px)
{
    if (!px) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 192\n255\n");
    for (int i = 0; i < 256*192; i++) fwrite(px + i*4, 1, 3, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }
    poke_set_script(core, "local a = 1\n");
    if (!poke_start(core)) { printf("start fail\n"); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    long frames = (argc > 2) ? atol(argv[2]) : 12000;
    const char* tag = (argc > 3) ? argv[3] : "shot";

    for (long f = 0; f < frames; f++)
    {
        /* Tap through any boot prompts: press A in short pulses. */
        long phase = f % 360;
        poke_set_button(core, POKE_BTN_A, phase < 6);
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
    }

    printf("\n================ after %ld frames ================\n", frames);
    auto& gpu = nds->GPU;
    for (int i = 0; i < 9; i++) printf("VRAMCNT_%c=%02X ", 'A'+i, gpu.VRAMCNT[i]);
    printf("\nVRAM_A nonzero=%d  VRAM_B nonzero=%d  VRAM_C nonzero=%d  VRAM_D nonzero=%d\n",
           nonzero(gpu.VRAM_A, 1<<16), nonzero(gpu.VRAM_B, 1<<16),
           nonzero(gpu.VRAM_C, 1<<16), nonzero(gpu.VRAM_D, 1<<16));

    int w=0,h=0; char p[256];
    poke_framebuffer(core, 0, &w, &h);
    const unsigned char* t = poke_framebuffer_ptr(core, 0);
    poke_framebuffer(core, 1, &w, &h);
    const unsigned char* b = poke_framebuffer_ptr(core, 1);
    printf("top: %dx%d colours=%d\n", w, h, distinct(t));
    printf("bot: colours=%d\n", distinct(b));
    snprintf(p, sizeof p, "/home/devin/%s_top.ppm", tag); dump(p, t);
    snprintf(p, sizeof p, "/home/devin/%s_bot.ppm", tag); dump(p, b);
    printf("dumped to /home/devin/%s_{top,bot}.ppm\n", tag);
    printf("ARM9.PC=%08X  VCount=%d\n", nds->ARM9.R[15], gpu.VCount);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
