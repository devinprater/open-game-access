// psp-enemy-probe.cpp — resume a Dissidia board savestate, tap the D-pad
// through a scripted list, and after each step save a screenshot (PPM) plus
// a RAM dump of the catalog region. Built for ONE job: the enemy-name task
// (docs TODO "Dissidia enemy pop-up text"): capture tooltip screenshots of
// TWO different enemies alongside the FP catalog bytes behind them, so the
// O+10 -> f1750 name deref can be correlated without guessing.
//
// Usage:
//   psp-enemy-probe <cso> <savedir> <assetsdir> <state.ppst> <outdir> <taps>
//   <taps> = comma list of U/D/L/R (dpad). Each tap: hold 30 frames, run 120.
//   Step 0 (no tap) is captured first, so outputs are step0..stepN.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "psp_core.h"

static int BTN(char c)
{
    switch (c) {
        case 'U': return PSP_BTN_UP;
        case 'D': return PSP_BTN_DOWN;
        case 'L': return PSP_BTN_LEFT;
        case 'R': return PSP_BTN_RIGHT;
        case 'X': return PSP_BTN_CROSS;
        case 'O': return PSP_BTN_CIRCLE;
        case 'Q': return PSP_BTN_SQUARE;
        case 'T': return PSP_BTN_TRIANGLE;
        case 's': return PSP_BTN_START;
        case 'e': return PSP_BTN_SELECT;
        case 'l': return PSP_BTN_L;
        case 'r': return PSP_BTN_R;
        default: return -1;
    }
}

// Analog-stick taps (numpad layout: 8 up, 2 down, 4 left, 6 right, 5 center).
// Some Dissidia menus read the stick where the D-pad does nothing.
static bool ANALOG(char c, float* x, float* y)
{
    switch (c) {
        case '8': *x = 0; *y = -1; return true;
        case '2': *x = 0; *y = 1; return true;
        case '4': *x = -1; *y = 0; return true;
        case '6': *x = 1; *y = 0; return true;
        case '5': *x = 0; *y = 0; return true;
        default: return false;
    }
}

// Catalog window: the FP catalog the Sept notes put at 0x09C168F0, with room
// on both sides in case this board's catalog sits nearby. 256 KiB.
static const uint32_t DUMP_BASE = 0x09C10000u;
static const uint32_t DUMP_LEN = 0x40000u;

static bool Shot(PspCore* c, const char* path)
{
    int w = 0, h = 0;
    if (!psp_framebuffer(c, &w, &h)) return false;
    const uint8_t* px = psp_framebuffer_ptr(c);
    if (!px || w <= 0 || h <= 0) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(px[i * 4], f);
        fputc(px[i * 4 + 1], f);
        fputc(px[i * 4 + 2], f);
    }
    fclose(f);
    return true;
}

static bool Dump(PspCore* c, const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    for (uint32_t a = DUMP_BASE; a < DUMP_BASE + DUMP_LEN; a += 4) {
        uint32_t v = psp_debug_read(c, a, 4);
        uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
        if (fwrite(b, 1, 4, f) != 4) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

static void Run(PspCore* c, int n)
{
    for (int i = 0; i < n; i++)
        if (!psp_frame(c)) break;
}

// PPSSPP's default log callback floods stdout (module imports per thread);
// the probe only cares about its own step lines.
static void QuietLog(const char* msg, void* ud)
{
    (void)msg;
    (void)ud;
}

int main(int argc, char** argv)
{
    if (argc != 7 && argc != 8 && argc != 9) {
        fprintf(stderr, "usage: %s <cso> <savedir> <assets> <state|-> <out> <taps> [shots-every] [lead]\n", argv[0]);
        return 2;
    }
    PspCore* c = psp_create();
    psp_set_log_callback(c, QuietLog, nullptr);
    psp_set_asset_dir(c, argv[3]);
    char code[16] = {0};
    // Every exit path stops and destroys the core: PPSSPP's threads hang
    // process exit if they are still running when main returns.
    int rc = 0;
    if (!psp_load_rom(c, argv[1], argv[2], code)) {
        fprintf(stderr, "load failed: %s\n", psp_last_error(c));
        rc = 1;
    } else if (!psp_start(c)) {
        fprintf(stderr, "start failed: %s\n", psp_last_error(c));
        rc = 1;
    } else if (strcmp(argv[4], "-") != 0 && !psp_load_state(c, argv[4])) {
        fprintf(stderr, "state failed: %s\n", psp_last_error(c));
        rc = 1;
    } else {
        // Survey mode: shots-every N (>0) screenshots every N frames with no
        // taps, for mapping boot/menu timelines blind. Steps are numbered
        // sequentially after step0.
        int every = (argc == 8) ? atoi(argv[7]) : 0;
        if (every > 0) {
            char sp[512];
            snprintf(sp, sizeof(sp), "%s/step0.ppm", argv[5]);
            printf("step0 shot=%d\n", (int)Shot(c, sp));
            for (int f = every, s = 1; f <= 12000; f += every, s++) {
                Run(c, every);
                snprintf(sp, sizeof(sp), "%s/step%d.ppm", argv[5], s);
                printf("step%d shot=%d\n", s, (int)Shot(c, sp));
            }
        } else {
        const char* taps = argv[6];
        int n = (int)strlen(taps);
        // Lead-in: wait this many frames before the first tap (e.g. logos
        // and FMV between a milestone state and the menu being driven).
        int lead = (argc == 9) ? atoi(argv[8]) : 60;
        Run(c, lead);
        for (int s = 0; s <= n; s++) {
            char sp[512], dp[512];
            snprintf(sp, sizeof(sp), "%s/step%d.ppm", argv[5], s);
            snprintf(dp, sizeof(dp), "%s/step%d.bin", argv[5], s);
            printf("step%d shot=%d dump=%d\n", s, (int)Shot(c, sp), (int)Dump(c, dp));
            if (s < n) {
                float ax = 0, ay = 0;
                if (ANALOG(taps[s], &ax, &ay)) {
                    psp_set_analog(c, ax, ay);
                    Run(c, 8);
                    psp_set_analog(c, 0, 0);
                } else {
                    int b = BTN(taps[s]);
                    if (b < 0) { fprintf(stderr, "bad tap %c\n", taps[s]); rc = 1; break; }
                    psp_set_button(c, b, true);
                    Run(c, 2);
                    psp_set_button(c, b, false);
                }
                Run(c, 150);
            }
        }
        }
        if (rc == 0) {
            // Milestone: a fresh state from THIS build, loadable going forward.
            char fp[512];
            snprintf(fp, sizeof(fp), "%s/final.ppst", argv[5]);
            printf("final state=%d\n", (int)psp_save_state(c, fp));
        }
    }
    psp_stop(c);
    psp_destroy(c);
    return rc;
}
