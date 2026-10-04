/*
 * gba_host_probe.cpp — boot a real GBA/GB ROM through the REAL app path and
 * narrate what the core says.
 *
 * ⛔ WHAT THIS PROVES THAT gba-adapter-test.sh CANNOT. The adapter test builds a
 * STUB host and only exercises selection, readiness and the refusal paths. This
 * one drives Core/pokecore.cpp exactly as the app does — poke_load_rom ->
 * poke_set_script_dir -> poke_start -> poke_frame — with the real mGBA core and
 * the real Pokémon Access reader set, and prints every line the reader speaks.
 *
 * That is the difference between "the registry says GBA is READY" and "a .gba
 * boots and the reader talks".
 *
 * Usage: gba-probe <rom> <script-dir> [max-frames]
 * ios-debug: host-only diagnostic, not part of the app.
 */
#include "pokecore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_spoken = 0;

static void on_speech(const char* text, bool interrupt, void* userdata)
{
    (void) interrupt; (void) userdata;
    if (text)
    {
        g_spoken++;
        printf("[SPEAK] %s\n", text);
        fflush(stdout);
    }
}

static void on_log(const char* text, void* userdata)
{
    (void) userdata;
    if (text) printf("[log]   %s\n", text);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <rom> <script-dir> [max-frames]\n", argv[0]);
        return 2;
    }
    const char* rom = argv[1];
    const char* scriptDir = argv[2];
    long maxFrames = (argc > 3) ? atol(argv[3]) : 20000;

    PokeCore* core = poke_create();
    if (!core) { fprintf(stderr, "!! poke_create failed\n"); return 1; }

    poke_set_speech_callback(core, on_speech, nullptr);
    poke_set_log_callback(core, on_log, nullptr);

    if (!poke_load_rom(core, rom, nullptr))
    {
        printf("LOAD FAIL: %s\n", poke_last_error(core));
        return 1;
    }
    printf("loaded %s  game code=\"%s\"  adapter=%s\n",
           rom, poke_game_code(core),
           poke_adapter_id(core) ? poke_adapter_id(core) : "(none)");

    /* The GBA reader loads its own ~174 files with loadfile, so it needs a
     * directory rather than the concatenated NDS script string. */
    poke_set_script_dir(core, scriptDir);

    if (!poke_start(core))
    {
        printf("START FAIL: %s\n", poke_last_error(core));
        return 1;
    }
    printf("started; running up to %ld frames\n", maxFrames);

    long f = 0;
    for (; f < maxFrames; f++)
    {
        if (!poke_frame(core))
        {
            printf("frame returned false at %ld: %s\n", f, poke_last_error(core));
            break;
        }
        /* ⛔ A REAL BUTTON SCRIPT, AND LONG ENOUGH TO LEAVE THE INTRO.
         *
         * FRLG's intro (copyright, title, "press start", Oak's speech, naming)
         * runs for MINUTES of emulated time. An earlier version of this probe
         * ran 900 frames — 15 seconds — and pressed A twice, so the reader was
         * asked to describe a game that had not started. That is not a reader
         * bug and it was not being distinguished from one.
         *
         * So: mash A on a steady cadence from the start, which walks the intro,
         * Oak's speech and the naming prompts, and press Start early to clear
         * the title. A player does exactly this. */
        {
            const long period = 45;            /* ~0.75 s: faster than any prompt */
            if (f >= 120 && (f % period) == 0)
                poke_set_button(core, POKE_BTN_A, true);
            if (f >= 120 && (f % period) == 20)
                poke_set_button(core, POKE_BTN_A, false);

            /* ⛔ START AS WELL AS A, AND ON A SLOWER CADENCE.
             *
             * A alone walks the intro but STALLS ON THE NAMING SCREEN, where A
             * types a letter and never confirms — the probe sat there for 14000
             * frames reporting a working reader as a stalled one. START is the
             * confirm button on that screen.
             *
             * Interleaving them at different rates (A every 45, START every 150)
             * means the odd A press never blocks a confirm for long, so the
             * script gets through the intro, Oak's speech, naming and into the
             * world without needing to know which screen it is on. */
            const long slow = 150;
            if (f >= 300 && (f % slow) == 0)
                poke_set_button(core, POKE_BTN_START, true);
            if (f >= 300 && (f % slow) == 24)
                poke_set_button(core, POKE_BTN_START, false);
        }
    }

    int w = 0, h = 0;
    bool fb = poke_framebuffer(core, POKE_SCREEN_TOP, &w, &h);
    const uint8_t* px = fb ? poke_framebuffer_ptr(core, POKE_SCREEN_TOP) : nullptr;
    int distinct = 0;
    if (px)
    {
        unsigned seen[64]; int n = 0;
        for (int i = 0; i < w * h; i += 7)
        {
            unsigned v = (px[i*4] << 16) | (px[i*4+1] << 8) | px[i*4+2];
            int found = 0;
            for (int k = 0; k < n; k++) if (seen[k] == v) { found = 1; break; }
            if (!found && n < 64) seen[n++] = v;
        }
        distinct = n;
    }
    printf("frames=%ld  framebuffer=%s %dx%d  distinct colours=%d\n",
           f, fb ? "yes" : "NO", w, h, distinct);
    printf("spoken lines=%d\n", g_spoken);

    /* A reader that never spoke and a black screen is the honest failure. */
    if (g_spoken == 0) { printf("RESULT: NO SPEECH\n"); return 1; }
    if (!fb || distinct < 2) { printf("RESULT: NO PICTURE\n"); return 1; }
    printf("RESULT: OK\n");
    return 0;
}
