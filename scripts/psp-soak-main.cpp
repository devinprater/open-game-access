// psp-soak-main.cpp — long-run slowdown/leak probe for the PSP path.
//
// Drives the production PokeCore exactly as the iOS app does each frame
// (poke_frame, poke_framebuffer, poke_adapter_ready) from a save state, with
// a slow D-pad/Cross pattern so the game and the Dissidia reader stay busy,
// and prints one line per window: wall ms/frame (mean and worst), resident
// memory, and speech/log counts. A leak shows as RSS climbing window over
// window; a slowdown as ms/frame climbing with it (or without it).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pokecore.h"

static long g_say = 0, g_log = 0;
static void OnSpeak(const char *t, bool, void *) { if (t && *t) g_say++; }
static void OnLog(const char *t, void *) { if (t && *t) g_log++; }

static double RssMB() {
    long pages = 0, rss = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = -1;
    fclose(f);
    return rss * 4096.0 / (1024.0 * 1024.0);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <image> <savedir> <frames> <state>\n", argv[0]);
        return 2;
    }
    const int frames = atoi(argv[3]);
    const int window = 600;  // 10 game-seconds

    PokeCore *core = poke_create();
    poke_set_speech_callback(core, OnSpeak, nullptr);
    poke_set_log_callback(core, OnLog, nullptr);
    if (!poke_load_rom(core, argv[1], argv[2]) || !poke_start(core) ||
        !poke_load_state(core, argv[4])) {
        fprintf(stderr, "SOAK-FAIL: %s\n", poke_last_error(core));
        // Shut the emulator down even on failure: PPSSPP's audio, IO and
        // worker threads otherwise keep the process alive after main returns.
        poke_stop(core);
        poke_destroy(core);
        return 1;
    }
    printf("SOAK: adapter=%s frames=%d window=%d\n",
           poke_adapter_id(core) ? poke_adapter_id(core) : "(none)", frames, window);
    printf("SOAK: %8s %9s %9s %8s %7s %7s\n", "frame", "ms/frame", "worst", "rss_mb", "say", "log");

    static const int kPad[] = {POKE_BTN_RIGHT, POKE_BTN_DOWN, POKE_BTN_LEFT, POKE_BTN_UP, POKE_BTN_A};
    using clock = std::chrono::steady_clock;
    double sum = 0, worst = 0;
    for (int i = 1; i <= frames; i++) {
        int phase = i % 90;
        if (phase == 0) poke_set_button(core, kPad[(i / 90) % 5], true);
        if (phase == 8) poke_set_button(core, kPad[(i / 90) % 5], false);

        auto t0 = clock::now();
        if (!poke_frame(core)) { fprintf(stderr, "SOAK-FAIL: frame %d: %s\n", i, poke_last_error(core)); return 1; }
        int w = 0, h = 0;
        poke_framebuffer(core, 0, &w, &h);
        poke_adapter_ready(core);
        double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();

        sum += ms;
        if (ms > worst) worst = ms;
        if (i % window == 0) {
            printf("SOAK: %8d %9.2f %9.2f %8.1f %7ld %7ld\n", i, sum / window, worst, RssMB(), g_say, g_log);
            fflush(stdout);
            sum = 0; worst = 0;
        }
    }
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
