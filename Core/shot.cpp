/*
 * shot.cpp — write the framebuffer to a PPM, so the rendering can be LOOKED AT
 * instead of inferred from a colour count.
 */
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); fflush(stdout); }
static void on_log(const char* t, void* u) { (void)t;(void)u; }

static void writePPM(const char* path, const unsigned char* px)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 192\n255\n");
    for (int i = 0; i < 256 * 192; i++) fwrite(px + i * 4, 1, 3, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }

    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f = fopen(shim,"rb"); if (f) { n = fread(script,1,sizeof(script)-64,f); fclose(f);} }
    script[n] = 0;
    strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    long shots = (argc > 2) ? atol(argv[2]) : 6;
    long step  = (argc > 3) ? atol(argv[3]) : 3000;

    for (long s = 0; s < shots; s++)
    {
        for (long i = 0; i < step; i++)
        {
            /* press START around the 3rd batch so the title advances */
            long f = s * step + i;
            if (f == 3000 || f == 3030) poke_set_button(core, 3, true);
            if (f == 3008 || f == 3038) poke_set_button(core, 3, false);
            if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
        }
        int w=0,h=0;
        poke_framebuffer(core, 0, &w, &h);
        const unsigned char* top = poke_framebuffer_ptr(core, 0);
        poke_framebuffer(core, 1, &w, &h);
        const unsigned char* bot = poke_framebuffer_ptr(core, 1);
        char p1[256], p2[256];
        snprintf(p1, sizeof(p1), "/home/devin/shot_top_%ld.ppm", s);
        snprintf(p2, sizeof(p2), "/home/devin/shot_bot_%ld.ppm", s);
        if (top) writePPM(p1, top);
        if (bot) writePPM(p2, bot);
        printf("shot %ld: frame ~%ld  wrote %s / %s\n", s, (s+1)*step, p1, p2);
        fflush(stdout);
    }
    poke_destroy(core);
    return 0;
}
