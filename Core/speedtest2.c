/*
 * speedtest2.c — isolate what makes the interpreter crawl.
 *
 * Delta's melonDS core excludes the JIT and still plays at roughly realtime,
 * so a 500x slowdown here is a build problem, not an emulation truth. Two
 * suspects, both testable:
 *   (a) Platform::Log vsnprintf()s EVERY unmapped-IO access before the callback
 *       runs — millions of calls per frame.
 *   (b) optimisation level (-O1 here vs -Ofast in Delta's podspec).
 *
 * PA_NOLOG=1 skips installing the log callback, so Platform::Log returns before
 * formatting anything.
 */
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_speak = 0;
static void on_speech(const char* t, bool i, void* u) { (void)i; (void)u; g_speak++; }
static int g_logCalls = 0;
static void on_log(const char* t, void* u) { (void)t; (void)u; g_logCalls++; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

int main(int argc, char** argv)
{
    const char* romPath = argv[1];
    const char* scriptPath = (argc > 2) ? argv[2] : NULL;
    long frames = (argc > 3) ? atol(argv[3]) : 5;
    if (scriptPath && strcmp(scriptPath, "-") == 0) scriptPath = NULL;

    int useLog = getenv("PA_NOLOG") == NULL;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    if (useLog) poke_set_log_callback(core, on_log, NULL);
    printf("== log callback: %s\n", useLog ? "INSTALLED" : "disabled (PA_NOLOG)");

    if (!poke_load_rom(core, romPath, NULL)) { fprintf(stderr, "load: %s\n", poke_last_error(core)); return 1; }

    char* script = NULL;
    if (scriptPath)
    {
        FILE* f = fopen(scriptPath, "rb");
        if (!f) { fprintf(stderr, "no script\n"); return 1; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        script = (char*) malloc((size_t) n + 1);
        size_t rd = fread(script, 1, (size_t) n, f); (void) rd;
        script[n] = 0; fclose(f);
        poke_set_script(core, script);
    }

    if (!poke_start(core)) { fprintf(stderr, "start: %s\n", poke_last_error(core)); return 1; }

    double t0 = now_s();
    long done = 0;
    for (long i = 0; i < frames; i++)
    {
        if (!poke_frame(core)) break;
        done++;
        if (i % 5 == 0)
        {
            printf("== %ld frames in %.1fs (%.2f fps) logCalls=%d speaks=%d\n",
                   done, now_s() - t0, done / (now_s() - t0), g_logCalls, g_speak);
            fflush(stdout);
        }
    }
    double total = now_s() - t0;
    printf("== RESULT: %ld frames in %.2fs = %.3f fps  logCalls=%d\n", done, total, done / total, g_logCalls);
    fflush(stdout);
    poke_destroy(core);
    free(script);
    return 0;
}
