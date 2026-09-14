/*
 * hosttest.c — drive the real PokeCore on a desktop host.
 *
 * Not part of the iOS app. It exercises exactly the code iOS runs: the same
 * pokecore.cpp, the same bundled main.lua + bizhawk_compat.lua, the same Lua
 * bindings. Only the platform timing layer differs. Speech that the player
 * would hear is printed to stdout as [SPEAK], which is the observable the
 * accessibility feature is judged on.
 *
 * The core's Platform::Log is chatty (it narrates every unmapped IO read), so
 * by default the log callback is muted and only lines matching an interest
 * filter are shown. Set PA_LOG=1 to see everything.
 */
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_verbose = 0;
static int g_speakCount = 0;

static void on_speech(const char* text, bool interrupt, void* userdata)
{
    (void) userdata;
    g_speakCount++;
    printf("[SPEAK%s] %s\n", interrupt ? "" : "-queue", text);
    fflush(stdout);
}

static void on_log(const char* text, void* userdata)
{
    (void) userdata;
    if (!g_verbose)
    {
        /* Keep only lines that say something about the accessibility layer or
           the console's own state, not per-IO noise. */
        if (!strstr(text, "booting") && !strstr(text, "Inserted cart") &&
            !strstr(text, "ctx") && !strstr(text, "Stop") && !strstr(text, "error"))
            return;
    }
    printf("[log] %s\n", text);
    fflush(stdout);
}

/* Count distinct colours on a screen — a cheap "is a real image here" probe. */
static int DistinctColours(const uint8_t* px)
{
    unsigned seen[128];
    int n = 0;
    for (int i = 0; i < 256 * 192; i += 53)
    {
        unsigned p = (unsigned) px[i*4] << 16 | px[i*4+1] << 8 | px[i*4+2];
        int found = 0;
        for (int k = 0; k < n; k++) if (seen[k] == p) { found = 1; break; }
        if (!found && n < 128) seen[n++] = p;
    }
    return n;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: hosttest <rom.nds> <script.lua> [frames] [start_frame]\n");
        return 2;
    }
    const char* romPath = argv[1];
    const char* scriptPath = argv[2];
    long maxFrames = (argc > 3) ? atol(argv[3]) : 3000;
    long startFrame = (argc > 4) ? atol(argv[4]) : 1200;
    g_verbose = getenv("PA_LOG") != NULL;

    FILE* sf = fopen(scriptPath, "rb");
    if (!sf) { fprintf(stderr, "cannot open script %s\n", scriptPath); return 2; }
    fseek(sf, 0, SEEK_END);
    long scriptLen = ftell(sf);
    fseek(sf, 0, SEEK_SET);
    char* script = (char*) malloc((size_t) scriptLen + 1);
    if (fread(script, 1, (size_t) scriptLen, sf) != (size_t) scriptLen) { /* best effort */ }
    script[scriptLen] = 0;
    fclose(sf);

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);

    printf("== version: %s\n", poke_version()); fflush(stdout);

    if (!poke_load_rom(core, romPath, NULL))
    {
        fprintf(stderr, "!! load_rom failed: %s\n", poke_last_error(core));
        return 1;
    }
    printf("== ROM loaded\n"); fflush(stdout);

    poke_set_script(core, script);
    if (!poke_start(core))
    {
        fprintf(stderr, "!! start failed: %s\n", poke_last_error(core));
        return 1;
    }
    printf("== started; script installed\n"); fflush(stdout);

    for (long f = 0; f < maxFrames; f++)
    {
        /* Press START around the title screen so the run reaches the menu, the
           way a player would; nothing else is injected. */
        if (f == startFrame || f == startFrame + 30)
            poke_set_button(core, POKE_BTN_START, true);
        if (f == startFrame + 8 || f == startFrame + 38)
            poke_set_button(core, POKE_BTN_START, false);

        if (!poke_frame(core)) { printf("== core stopped at frame %ld\n", f); fflush(stdout); break; }

        if (f % 300 == 0)
        {
            int topW = 0, topH = 0, botW = 0, botH = 0;
            poke_framebuffer(core, POKE_SCREEN_TOP, &topW, &topH);
            const uint8_t* top = poke_framebuffer_ptr(core, POKE_SCREEN_TOP);
            int dt = (top && topH) ? DistinctColours(top) : -1;

            poke_framebuffer(core, POKE_SCREEN_BOTTOM, &botW, &botH);
            const uint8_t* bot = poke_framebuffer_ptr(core, POKE_SCREEN_BOTTOM);
            int db = (bot && botH) ? DistinctColours(bot) : -1;

            printf("== frame %ld  top=%d bot=%d  speaks=%d\n", f, dt, db, g_speakCount);
            fflush(stdout);
        }
    }

    printf("== done: running=%d stopReason=%d speaks=%d\n",
           poke_running(core), poke_stop_reason(core), g_speakCount);
    fflush(stdout);
    poke_destroy(core);
    free(script);
    return 0;
}
