/*
 * vramprobe.cpp — does the game write ANY graphics data?
 *
 * Decisive split:
 *   VRAM empty  → the game is stuck in early boot code; the white screen is a
 *                 symptom of that, not a rendering bug.
 *   VRAM has data but the framebuffer stays flat → display/renderer config.
 *
 * Also reports the renderer pointer (a null renderer means the GPU has nothing
 * to draw with and would explain a permanently blank framebuffer).
 */
#include "pokecore.h"
#include "NDS.h"
#include "GPU.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i; (void)u; printf("[SPEAK] %s\n", t); }

static bool nonzero(const unsigned char* p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return true;
    return false;
}

int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (argc > 2 && argv[2][0]) poke_set_firmware(core, argv[2], argv[3], argv[4]);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }

    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim && !getenv("PA_NOSCRIPT")) { FILE* f=fopen(shim,"rb"); if(f){n=fread(script,1,sizeof(script)-64,f);fclose(f);} }
    script[n]=0; strcat(script, "\nwhile true do emu.frameadvance() end\n");
    printf("== script: %s (%zu bytes)\n", n ? "shim+main.lua" : "MINIMAL (no shim/main.lua)", n);
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    long frames = (argc > 5) ? atol(argv[5]) : 6000;

    for (long f = 0; f < frames; f++)
    {
        if (f == 200) poke_set_button(core, POKE_BTN_A, true);
        if (f == 215) poke_set_button(core, POKE_BTN_A, false);
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
    }

    auto& gpu = nds->GPU;
    printf("\n================ GPU / VRAM STATE after %ld frames ================\n", frames);
    printf("VRAM_A[0:16]         =");
    for (int i = 0; i < 16; i++) printf(" %02X", gpu.VRAM_A[i]);
    printf("\n\n--- graphics data present? ---\n");
    printf("VRAM_A   nonzero: %s\n", nonzero(gpu.VRAM_A, 4096) ? "YES" : "no");
    printf("VRAM_C   nonzero: %s\n", nonzero(gpu.VRAM_C, 4096) ? "YES" : "no");
    printf("VRAMFlat_ABG nonzero: %s\n", nonzero(gpu.VRAMFlat_ABG, 65536) ? "YES" : "no");
    printf("VRAMFlat_Texture nonzero: %s\n", nonzero(gpu.VRAMFlat_Texture, 65536) ? "YES" : "no");
    printf("\n--- first 32 bytes of VRAM_A ---\n");
    for (int i = 0; i < 32; i++) printf("%02X ", gpu.VRAM_A[i]);
    printf("\n");

    printf("\n--- framebuffer pointers ---\n");
    void *t = NULL, *b = NULL;
    if (gpu.GetFramebuffers(&t, &b))
        printf("top=%p bottom=%p  first4(top)=%02X %02X %02X %02X\n",
               t, b, t?((unsigned char*)t)[0]:0, t?((unsigned char*)t)[1]:0,
               t?((unsigned char*)t)[2]:0, t?((unsigned char*)t)[3]:0);
    else
        printf("GetFramebuffers returned false!\n");
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
