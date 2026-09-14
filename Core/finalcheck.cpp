/*
 * finalcheck.cpp — after the Reset() fix, what is the real register/display state?
 *
 * Reads only PUBLIC members (the earlier probe tripped over protected fields,
 * which is itself informative: NDS keeps power/most GPU state private).
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
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    printf("== after load_rom ==\n");
    printf("   ARM9ClockShift=%d MainRAMMask=%08X\n", nds->ARM9ClockShift, nds->MainRAMMask);
    printf("   ARM9MemTimings[0] = %d %d %d %d\n",
           nds->ARM9MemTimings[0][0], nds->ARM9MemTimings[0][1],
           nds->ARM9MemTimings[0][2], nds->ARM9MemTimings[0][3]);

    void *t1=NULL,*b1=NULL;
    bool ok = nds->GPU.GetFramebuffers(&t1, &b1);
    printf("   GetFramebuffers=%d top=%p bottom=%p\n", ok, t1, b1);
    if (t1)
    {
        unsigned* p = (unsigned*) t1;
        printf("   top fb first 4 px: %08X %08X %08X %08X\n", p[0], p[1], p[2], p[3]);
    }
    printf("   GPU3D.TotalScanlines? ... running frames\n");

    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f = fopen(shim,"rb"); if (f) { n = fread(script,1,sizeof(script)-64,f); fclose(f);} }
    script[n] = 0;
    strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    for (int i = 0; i < 3000; i++) poke_frame(core);

    int w=0,h=0;
    poke_framebuffer(core, 0, &w, &h);
    const unsigned char* top = poke_framebuffer_ptr(core, 0);
    int distinct = 0;
    unsigned seen[256]; int ns = 0;
    for (int i = 0; i < 256*192; i += 17)
    {
        unsigned v = (top[i*4]<<16)|(top[i*4+1]<<8)|top[i*4+2];
        int f=0; for (int k=0;k<ns;k++) if (seen[k]==v) { f=1; break; }
        if (!f && ns < 256) seen[ns++] = v;
    }
    distinct = ns;
    printf("== after 3000 frames: top-screen distinct colours = %d\n", distinct);
    for (int i = 0; i < ns && i < 6; i++) printf("   %06X\n", seen[i]);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
