/*
 * scriptrun.cpp — the real accessibility script against a game that is actually
 * in-game. Loads shim + main.lua (2.1 MB) and captures every spoken line.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore*);
static void on_log(const char* t, void* u)
{
    (void)u;
    if (strstr(t, "ctx") || strstr(t, "SPEAK") || strstr(t, "error") ||
        strstr(t, "Error") || strstr(t, "boot"))
        printf("[log] %s\n", t), fflush(stdout);
}
static int speeches = 0;
static void on_speech(const char* t, bool interrupt, void* u)
{
    (void)interrupt; (void)u;
    speeches++;
    printf("[SPEAK #%d] %s\n", speeches, t); fflush(stdout);
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    const char* scriptPath = argv[2];
    long total = (argc > 3) ? atol(argv[3]) : 60000;

    PokeCore* c = poke_create();
    poke_set_speech_callback(c, on_speech, NULL);
    poke_set_log_callback(c, on_log, NULL);
    poke_set_firmware(c, "/home/devin/ds-bios/bios9.bin",
                         "/home/devin/ds-bios/bios7.bin",
                         "/home/devin/ds-bios/firmware.bin");
    if (!poke_load_rom(c, rom, NULL)) { printf("load fail: %s\n", poke_last_error(c)); return 1; }

    /* shim + main.lua, concatenated, exactly as the app does it */
    static char script[4000000];
    FILE* f = fopen(scriptPath, "rb");
    if (!f) { printf("cannot open script\n"); return 1; }
    size_t n = fread(script, 1, sizeof(script) - 64, f);
    fclose(f);
    script[n] = 0;
    printf("script loaded: %zu bytes from %s\n", n, scriptPath);

    poke_set_script(c, script);
    if (!poke_start(c)) { printf("start fail: %s\n", poke_last_error(c)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(c);
    for (long fr = 0; fr < total; fr++)
    {
        poke_set_button(c, POKE_BTN_A, (fr % 120) < 5);
        poke_set_button(c, POKE_BTN_START, (fr % 900) < 5);
        if (!poke_frame(c)) { printf("stopped at %ld\n", fr); break; }
        if (fr % 15000 == 0 && fr)
        {
            printf("-- frame %ld: PC=%08X VRAMCNT_A=%02X speeches=%d --\n",
                   fr, nds->ARM9.R[15], nds->GPU.VRAMCNT[0], speeches);
            fflush(stdout);
        }
    }
    printf("\n==== total spoken lines: %d ====\n", speeches);
    fflush(stdout);
    poke_destroy(c);
    return 0;
}
