/*
 * timingtest.c — measure how long a single poke_frame() takes, so a stall can
 * be attributed to the emulator or to the accessibility script.
 *
 * The host run reached "started" then sat at 100% CPU with no completed frame
 * batch, so the question is which of the two halves of poke_frame() is the
 * expensive one:
 *     ApplyInput + NDS::RunFrame     (the emulator)
 *     lua_resume  -> main.lua        (the script)
 * This builds the core WITHOUT the script and times pure emulation; if that is
 * fast, the script is the cost.
 */
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_speak = 0;
static void on_speech(const char* t, bool i, void* u) { (void)i; (void)u; g_speak++; printf("[SPEAK] %s\n", t); }
static void on_log(const char* t, void* u) { (void)t; (void)u; }

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
    /* "-" means "no script": pure emulation, used to tell an emulator stall
       from a script stall. */
    if (scriptPath && strcmp(scriptPath, "-") == 0) scriptPath = NULL;
    long frames = (argc > 3) ? atol(argv[3]) : 600;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, romPath, NULL)) { fprintf(stderr, "load: %s\n", poke_last_error(core)); return 1; }

    if (scriptPath)
    {
        FILE* f = fopen(scriptPath, "rb");
        if (!f) { fprintf(stderr, "no script\n"); return 1; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        char* s = (char*) malloc((size_t) n + 1);
        size_t rd = fread(s, 1, (size_t) n, f); (void) rd;
        s[n] = 0; fclose(f);
        poke_set_script(core, s);
    }

    printf("== starting %s\n", scriptPath ? "WITH script" : "WITHOUT script");
    fflush(stdout);
    if (!poke_start(core)) { fprintf(stderr, "start: %s\n", poke_last_error(core)); return 1; }

    double t0 = now_s();
    for (long i = 0; i < frames; i++)
    {
        double a = now_s();
        if (!poke_frame(core)) { printf("== stopped at %ld\n", i); break; }
        double d = now_s() - a;
        if (d > 0.5) { printf("!! frame %ld took %.2fs\n", i, d); fflush(stdout); }
        if (i % 100 == 0) { printf("== frame %ld  elapsed=%.1fs  speaks=%d\n", i, now_s() - t0, g_speak); fflush(stdout); }
    }
    double total = now_s() - t0;
    printf("== %ld frames in %.2fs = %.1f fps  speaks=%d\n", frames, total, frames / total, g_speak);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
