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
 * ⛔ AND IT SAVES/LOADS STATE, because walking a Pokémon intro costs minutes of
 * emulated time PER RUN and that makes trial-and-error impossible. One successful
 * walk into the world is captured with --state-out; every later run resumes from
 * it with --state-in and reaches the world in seconds.
 *
 * Usage: gba-probe <rom> <script-dir> [max-frames] [--state-out P] [--state-in P]
 * ios-debug: host-only diagnostic, not part of the app.
 */
#include "pokecore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_spoken = 0;
static long g_first_world_line = -1;

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

/* One input step, for frame `f`.
 *
 * ⛔ A-ONLY MASHING CANNOT CLEAR A MENU. That was the bug in the first version:
 * on FRLG's naming screen A types a letter forever and never confirms, because
 * confirming needs the cursor MOVED to "OK" first. The probe sat there for 14000
 * frames and reported a working reader as a stalled one.
 *
 * So this presses A, a CYCLING DIRECTION, and START on three different cadences.
 * Whatever screen it is on — copyright, title, speech, naming, a dialogue box —
 * some combination of those three advances it. It is deliberately not a script
 * that knows the game, because the point is to reach the world without one. */
static void DriveInput(PokeCore* core, long f, int profile)
{
    static const int kDir[4] = { POKE_BTN_RIGHT, POKE_BTN_DOWN, POKE_BTN_LEFT, POKE_BTN_UP };

    if (f < 90) return;      /* let the boot settle before touching anything */

    if (profile == 1)
    {
        /* ---- naming-screen walk to "OK" -------------------------------------
         * A press is a tap: 1-frame down is enough and a long hold repeats.
         * The grid's confirm key is at the bottom-right, so go there and press it.
         * Order matters and there are no undos, which is the whole difference
         * from the random walk. */
        const long tap = 12;                 /* ~0.2 s per step: brisk but clean */
        long n = f - 90;
        long step = n / tap;                 /* which key of the sequence we are on */

        int btn = -1;
        if (step < 16)       btn = POKE_BTN_DOWN;   /* 1..16  -> bottom row */
        else if (step < 34)  btn = POKE_BTN_RIGHT;  /* 17..34 -> rightmost = OK */
        else                 btn = POKE_BTN_A;      /* 35..   -> confirm, then advance */

        if (btn >= 0)
        {
            if ((n % tap) == 0) poke_set_button(core, btn, true);
            if ((n % tap) == 3) poke_set_button(core, btn, false);
        }

        /* START every 2 s as a backstop: on this screen it confirms the current
         * name outright, and on a dialogue box it is inert. */
        if ((n % 120) == 0)  poke_set_button(core, POKE_BTN_START, true);
        if ((n % 120) == 8)  poke_set_button(core, POKE_BTN_START, false);
        return;
    }

    const long fast = 40;    /* A:            ~1.5 presses/second */
    const long dirp = 40;    /* direction:    ~1.5 presses/second, 4-way cycle */
    const long slow = 200;   /* START:        ~0.5 presses/second */

    /* A — the confirm key on almost every screen. */
    if ((f % fast) == 0)  poke_set_button(core, POKE_BTN_A, true);
    if ((f % fast) == 18) poke_set_button(core, POKE_BTN_A, false);

    /* A direction, cycling R/D/L/U. This is what actually gets the cursor to
     * "OK" on a naming screen, and it also walks past any "press a direction"
     * prompt and moves the player once in the world. */
    const long slot = (f / dirp) % 4;
    const int dir = kDir[slot];
    if ((f % dirp) == 20) poke_set_button(core, dir, true);
    if ((f % dirp) == 34) poke_set_button(core, dir, false);

    /* START — the confirm button on the naming screen, and the menu key. */
    if ((f % slow) == 0)  poke_set_button(core, POKE_BTN_START, true);
    if ((f % slow) == 15) poke_set_button(core, POKE_BTN_START, false);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <rom> <script-dir> [max-frames] "
                        "[--state-out PATH] [--state-in PATH] [--profile 0|1]\n", argv[0]);
        return 2;
    }
    const char* rom = argv[1];
    const char* scriptDir = argv[2];
    long maxFrames = 20000;
    const char* stateOut = nullptr;
    const char* stateIn = nullptr;
    int profile = 0;   /* 0 = generic walk, 1 = naming-screen walk to OK */

    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--state-out") == 0 && i + 1 < argc) stateOut = argv[++i];
        else if (strcmp(argv[i], "--state-in") == 0 && i + 1 < argc) stateIn = argv[++i];
        else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) profile = atoi(argv[++i]);
        else if (argv[i][0] != '-') maxFrames = atol(argv[i]);
    }

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

    long f = 0;
    if (stateIn)
    {
        if (!poke_load_state(core, stateIn))
        {
            printf("STATE LOAD FAIL: %s\n", poke_last_error(core));
            return 1;
        }
        printf("resumed from %s\n", stateIn);
    }
    printf("running up to %ld frames%s\n", maxFrames, stateIn ? " (from a savestate)" : "");

    for (; f < maxFrames; f++)
    {
        DriveInput(core, f, profile);
        if (!poke_frame(core))
        {
            printf("frame returned false at %ld: %s\n", f, poke_last_error(core));
            break;
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

    if (stateOut)
    {
        if (poke_save_state(core, stateOut)) printf("saved state -> %s\n", stateOut);
        else                                printf("STATE SAVE FAIL: %s\n", poke_last_error(core));
    }

    /* A reader that never spoke and a black screen is the honest failure.
     * Raw numbers are NOT a failure here: see the note in gba-host-proof.sh —
     * a cold-boot run is expected to describe a game that has not started. */
    if (g_spoken == 0) { printf("RESULT: NO SPEECH\n"); return 1; }
    if (!fb || distinct < 2) { printf("RESULT: NO PICTURE\n"); return 1; }
    printf("RESULT: OK\n");
    return 0;
}
