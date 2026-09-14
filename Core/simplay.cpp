/*
 * simplay.cpp — drive the iOS core from the title screen into the overworld the
 * way a player does, then exercise the accessibility readers there. The title
 * screen only proves the script loaded; the overworld is where the readers that
 * read the live map, coordinates and nav list actually run.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

static int g_speak = 0;
static void on_speech(const char* t, bool i, void* u)
{
    (void) u; (void) i;
    if (!t) { printf("[SPEAK] (stop)\n"); fflush(stdout); return; }
    g_speak++;
    printf("[SPEAK] %s\n", t);
    fflush(stdout);
}
static void on_log(const char* t, void* u)
{
    (void) u;
    if (strstr(t, "[ctx]") || strstr(t, "Inserted cart"))
        { printf("[log] %s\n", t); fflush(stdout); }
}

static void btn(PokeCore* c, int b, bool down) { poke_set_button(c, b, down); }

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 40000;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);

    static char* script = (char*) malloc(5000000);
    size_t n = 0;
    const char* path = getenv("PA_SCRIPT");
    if (path) { FILE* f = fopen(path, "rb"); if (f) { n = fread(script, 1, 4900000, f); fclose(f); } }
    script[n] = 0;
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }
    printf("== needsDirectBoot=%s script=%s\n",
           nds->NeedsDirectBoot() ? "true" : "false", poke_running(core) ? "running" : "NOT running");
    fflush(stdout);

    /* A scripted player. Each entry: frame, button, pressed?  Presses last ~12
     * frames, which is long enough for the game to sample on a real frame. */
    struct Ev { long f; int b; int d; const char* what; };
    static const struct Ev EV[] = {
        {  300, POKE_BTN_A, 1, "title: A (enter menu)" },
        {  312, POKE_BTN_A, 0, "" },
        {  900, POKE_BTN_START, 1, "menu: START (New Game)" },
        {  912, POKE_BTN_START, 0, "" },
        { 1400, POKE_BTN_A, 1, "intro: A x many" },
        { 1412, POKE_BTN_A, 0, "" },
        { 1500, POKE_BTN_A, 1, "" },
        { 1512, POKE_BTN_A, 0, "" },
        { 1650, POKE_BTN_A, 1, "" },
        { 1662, POKE_BTN_A, 0, "" },
        { 1800, POKE_BTN_A, 1, "" },
        { 1812, POKE_BTN_A, 0, "" },
        { 1950, POKE_BTN_A, 1, "" },
        { 1962, POKE_BTN_A, 0, "" },
        { 2100, POKE_BTN_A, 1, "" },
        { 2112, POKE_BTN_A, 0, "" },
        { 2300, POKE_BTN_A, 1, "" },
        { 2312, POKE_BTN_A, 0, "" },
        { 2500, POKE_BTN_A, 1, "" },
        { 2512, POKE_BTN_A, 0, "" },
        { 2700, POKE_BTN_A, 1, "" },
        { 2712, POKE_BTN_A, 0, "" },
        { 2900, POKE_BTN_A, 1, "" },
        { 2912, POKE_BTN_A, 0, "" },
        { 3100, POKE_BTN_A, 1, "" },
        { 3112, POKE_BTN_A, 0, "" },
        { 3300, POKE_BTN_A, 1, "" },
        { 3312, POKE_BTN_A, 0, "" },
        { 3500, POKE_BTN_A, 1, "" },
        { 3512, POKE_BTN_A, 0, "" },
        { 3700, POKE_BTN_A, 1, "" },
        { 3712, POKE_BTN_A, 0, "" },
        { 3900, POKE_BTN_A, 1, "" },
        { 3912, POKE_BTN_A, 0, "" },
        { 4300, POKE_BTN_DOWN, 1, "walk down" },
        { 4360, POKE_BTN_DOWN, 0, "" },
        { 4500, POKE_BTN_DOWN, 1, "walk down" },
        { 4560, POKE_BTN_DOWN, 0, "" },
        { 4700, POKE_BTN_RIGHT, 1, "walk right" },
        { 4760, POKE_BTN_RIGHT, 0, "" },
    };
    static const char* KEYS[] = { "C", "U", "J", "L", "K", "C", "P", "R" };
    int evi = 0, keyi = 0;
    long keyHold = 0, keyGap = 0;

    for (long f = 0; f < frames; f++)
    {
        while (evi < (int) (sizeof(EV) / sizeof(EV[0])) && EV[evi].f == f)
        {
            btn(core, EV[evi].b, EV[evi].d != 0);
            if (EV[evi].what[0]) { printf("[pad] f=%ld %s\n", f, EV[evi].what); fflush(stdout); }
            evi++;
        }
        /* From f=5000 on, poke the reading hotkeys, three seconds apart. */
        if (f >= 5000 && f % 180 == 0 && keyi < (int) (sizeof(KEYS) / sizeof(KEYS[0])))
        {
            printf("[key] f=%ld press %s\n", f, KEYS[keyi]);
            fflush(stdout);
            poke_set_hotkey(core, KEYS[keyi], true);
            keyHold = 6;
            keyi++;
        }
        if (keyHold > 0) { if (--keyHold == 0 && keyi > 0) poke_set_hotkey(core, KEYS[keyi - 1], false); }

        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
        if (f % 4000 == 0 && f)
            printf("--- f=%-6ld speaks=%-3d ARM9=%08X VRAMCNT_A=%02X\n",
                   f, g_speak, nds->ARM9.R[15], nds->GPU.VRAMCNT[0]);
    }
    printf("== done frames=%llu speaks=%d\n", poke_frames_completed(core), g_speak);
    poke_destroy(core);
    return 0;
}
