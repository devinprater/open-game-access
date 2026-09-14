/*
 * simrun.cpp — the iOS core, exercised the way the app exercises it, with an
 * actual iPhone-style test flow: boot the ROM, drive it into the game, then
 * press the accessibility script's own hotkeys and capture everything it says.
 *
 * Why this exists: an iOS Simulator cannot run on Windows/Linux (it is a
 * macOS-only runtime), so this harness is the highest-fidelity substitute —
 * byte-identical core sources, byte-identical script, the app's own C ABI and
 * frame pacing. The one thing it cannot prove is UIKit/VoiceOver presentation.
 *
 * Observable: [SPEAK] lines. An accessibility feature that does not speak has
 * not been tested, no matter what else passes.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

static int g_verbose = 0;
static int g_speak = 0;
static int g_ctx = 0;

static void on_speech(const char* t, bool i, void* u)
{
    (void) u; (void) i;
    g_speak++;
    printf("[SPEAK] %s\n", t);
    fflush(stdout);
}

static void on_log(const char* t, void* u)
{
    (void) u;
    if (g_verbose) { printf("[log] %s\n", t); fflush(stdout); return; }
    if (strstr(t, "[ctx]") || strstr(t, "booting") || strstr(t, "Inserted cart") ||
        strstr(t, "Firmware") || strstr(t, "BIOS"))
    {
        g_ctx++;
        printf("[log] %s\n", t);
        fflush(stdout);
    }
}

static int distinct(const unsigned char* px)
{
    static unsigned seen[1024];
    int n = 0;
    if (!px) return -1;
    for (int i = 0; i < 256 * 192; i += 23)
    {
        unsigned v = (unsigned) px[i * 4] << 16 | (unsigned) px[i * 4 + 1] << 8 | px[i * 4 + 2];
        int f = 0;
        for (int k = 0; k < n; k++) if (seen[k] == v) { f = 1; break; }
        if (!f && n < 1024) seen[n++] = v;
    }
    return n;
}

/* A press the script can actually observe: main.lua samples input.get() once
 * per emulated frame, so down and up MUST be separated by real frames. A
 * same-instant tap is invisible to it. */
static void pressHot(PokeCore* core, const char* key, long* at)
{
    poke_set_hotkey(core, key, true);
    *at = 4;
    printf("[test] press %s\n", key);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    const char* b9  = (argc > 2 && strcmp(argv[2], "-")) ? argv[2] : NULL;
    const char* b7  = (argc > 3 && strcmp(argv[3], "-")) ? argv[3] : NULL;
    const char* fw  = (argc > 4 && strcmp(argv[4], "-")) ? argv[4] : NULL;
    long frames = (argc > 5) ? atol(argv[5]) : 25000;
    long bootFrames = (argc > 6) ? atol(argv[6]) : 20000;

    g_verbose = getenv("PA_LOG") != NULL;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (b9 || b7 || fw) poke_set_firmware(core, b9, b7, fw);

    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    printf("== firmware: %s\n", poke_has_firmware(core) ? "real bootable" : "free/generated");
    melonDS::NDS* nds = poke_debug_nds(core);
    printf("== NeedsDirectBoot: %s\n", nds->NeedsDirectBoot() ? "true" : "false");

    static char* script = (char*) malloc(5000000);
    size_t n = 0;
    const char* path = getenv("PA_SCRIPT");
    if (path) { FILE* f = fopen(path, "rb"); if (f) { n = fread(script, 1, 4900000, f); fclose(f); } }
    script[n] = 0;
    printf("== script: %s (%zu bytes)\n", path ? path : "(none)", n);
    fflush(stdout);

    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }
    printf("== script loaded: %s\n", poke_running(core) ? "yes" : "no");
    fflush(stdout);

    /* The hotkeys to exercise, in order, after boot. These are the keys the
     * script's own poll_keys() edge-detects; driving them is what turns a
     * silent, correct core into proof that narration works. */
    static const char* KEYS[] = { "C", "J", "L", "K", "U", "R", "C", "P", "R" };
    int keyIdx = 0;
    long keyCountdown = 0;

    for (long f = 0; f < frames; f++)
    {
        /* Boot input, exactly like a player: title menu needs START/A. */
        if (f == 300)  poke_set_button(core, POKE_BTN_A, true);
        if (f == 320)  poke_set_button(core, POKE_BTN_A, false);
        if (f == 700)  poke_set_button(core, POKE_BTN_START, true);
        if (f == 720)  poke_set_button(core, POKE_BTN_START, false);
        if (f == 1100) poke_set_button(core, POKE_BTN_A, true);
        if (f == 1120) poke_set_button(core, POKE_BTN_A, false);

        /* After the game is up, poke hotkeys a few seconds apart. */
        if (f >= bootFrames)
        {
            if (keyCountdown == 0 && keyIdx < (int) (sizeof(KEYS) / sizeof(KEYS[0])))
            {
                if (keyIdx > 0) poke_set_hotkey(core, KEYS[keyIdx - 1], false);
                pressHot(core, KEYS[keyIdx], &keyCountdown);
                keyIdx++;
                if (keyIdx < (int) (sizeof(KEYS) / sizeof(KEYS[0]))) keyCountdown = 240;
            }
            if (keyCountdown > 0) keyCountdown--;
        }

        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }

        if (f % 5000 == 0 && f)
        {
            int w = 0, h = 0;
            poke_framebuffer(core, 0, &w, &h);
            const unsigned char* t1 = poke_framebuffer_ptr(core, 0);
            int c1 = t1 ? distinct(t1) : -1;
            poke_framebuffer(core, 1, &w, &h);
            const unsigned char* t2 = poke_framebuffer_ptr(core, 1);
            int c2 = t2 ? distinct(t2) : -1;
            printf("f=%-6ld top_colours=%-4d bot_colours=%-4d speaks=%-3d log=%d ARM9=%08X\n",
                   f, c1, c2, g_speak, g_ctx, nds->ARM9.R[15]);
            fflush(stdout);
        }
    }

    printf("== done: frames=%llu speaks=%d ctxlog=%d\n",
           poke_frames_completed(core), g_speak, g_ctx);
    poke_destroy(core);
    return 0;
}
