/*
 * fwtest.cpp — boot with REAL bios9/bios7/firmware and see if the game
 * progresses past the point direct boot got stuck at.
 *
 * Compares the two paths side by side:
 *   - with firmware: no direct boot, so the BIOS runs the firmware, the firmware
 *     boots the cart, and the game gets the handshake it waits for.
 *   - reported at the end as distinct colours per screen, plus frames completed.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

static int g_speak = 0;
static void on_speech(const char* t, bool i, void* u) {
    (void)i; (void)u; g_speak++;
    printf("[SPEAK] %s\n", t); fflush(stdout);
}
static void on_log(const char* t, void* u) {
    (void)u;
    /* keep only the informative lines */
    if (strstr(t, "boot") || strstr(t, "ctx") || strstr(t, "Firmware") ||
        strstr(t, "BIOS") || strstr(t, "Inserted") || strstr(t, "Halt"))
        printf("[log] %s\n", t), fflush(stdout);
}

static int distinct(unsigned* seen, int cap, const unsigned char* px)
{
    int n = 0;
    if (!px) return -1;
    for (int i = 0; i < 256 * 192; i += 29)
    {
        unsigned v = (px[i*4] << 16) | (px[i*4+1] << 8) | px[i*4+2];
        int f = 0;
        for (int k = 0; k < n; k++) if (seen[k] == v) { f = 1; break; }
        if (!f && n < cap) seen[n++] = v;
    }
    return n;
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    const char* b9  = (argc > 2) ? argv[2] : NULL;
    const char* b7  = (argc > 3) ? argv[3] : NULL;
    const char* fw  = (argc > 4) ? argv[4] : NULL;
    long frames     = (argc > 5) ? atol(argv[5]) : 20000;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);

    if (b9 || b7 || fw) poke_set_firmware(core, b9, b7, fw);

    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    printf("== firmware installed: %s\n", poke_has_firmware(core) ? "YES (bootable)" : "no (free/generated)");

    melonDS::NDS* nds = poke_debug_nds(core);
    printf("== NeedsDirectBoot: %s\n", nds->NeedsDirectBoot() ? "true (forced)" : "false (firmware boot)");

    static char script[4000000];
    const char* shim = getenv("PA_SCRIPT");
    if (!shim) shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f=fopen(shim,"rb"); if(f){n=fread(script,1,sizeof(script)-64,f);fclose(f);} }
    script[n]=0; strcat(script, "\nwhile true do emu.frameadvance() end\n");
    printf("== script source: %s (%zu bytes)\n", shim ? shim : "(none)", n);
    fflush(stdout);
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    for (long f = 0; f < frames; f++)
    {
        /* The DS firmware menu needs a tap; press A/START now and then so a
           firmware-booted run can move past the menu into the game. */
        if (f == 200) poke_set_button(core, POKE_BTN_A, true);
        if (f == 215) poke_set_button(core, POKE_BTN_A, false);
        if (f == 400) poke_set_button(core, POKE_BTN_START, true);
        if (f == 415) poke_set_button(core, POKE_BTN_START, false);

        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }

        if (f % 5000 == 0 && f)
        {
            unsigned s1[512], s2[512];
            int w=0,h=0;
            poke_framebuffer(core, 0, &w, &h);
            const unsigned char* t1 = poke_framebuffer_ptr(core, 0);
            poke_framebuffer(core, 1, &w, &h);
            const unsigned char* t2 = poke_framebuffer_ptr(core, 1);
            printf("f=%-6ld top_colours=%-4d bot_colours=%-4d speaks=%d ARM9=%08X\n",
                   f, distinct(s1, 512, t1), distinct(s2, 512, t2), g_speak, nds->ARM9.R[15]);
            fflush(stdout);
        }
    }
    printf("== done: frames=%llu speaks=%d\n", poke_frames_completed(core), g_speak);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
