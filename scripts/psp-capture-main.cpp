// psp-capture-main.cpp — boot the game, dump raw RGBA screenshots and
// savestates every <interval> frames. Lets a sighted analyst see where the
// boot actually goes (logos? FMV? title?) without replaying blind.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "pokecore.h"

static bool DumpFb(PokeCore *core, const char *path, int *wOut, int *hOut) {
    int w = 0, h = 0;
    if (!poke_framebuffer(core, 0, &w, &h)) return false;
    const uint8_t *px = poke_framebuffer_ptr(core, 0);
    if (!px || w <= 0 || h <= 0) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    // raw header: "RGBA w h\n" then pixels, so the converter needs no args.
    fprintf(f, "RGBA %d %d\n", w, h);
    size_t n = fwrite(px, 1, (size_t)w * h * 4, f);
    fclose(f);
    if (wOut) *wOut = w;
    if (hOut) *hOut = h;
    return n == (size_t)w * h * 4;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s <image> <savedir> <outdir> <frames> <interval>\n", argv[0]);
        return 2;
    }
    const char *image = argv[1], *savedir = argv[2], *outdir = argv[3];
    int cap = atoi(argv[4]), interval = atoi(argv[5]);

    PokeCore *core = poke_create();
    if (!core) { fprintf(stderr, "CAP-FAIL: no core\n"); return 1; }
    if (!poke_load_rom(core, image, savedir)) {
        fprintf(stderr, "CAP-FAIL: load: %s\n", poke_last_error(core));
        return 1;
    }
    if (!poke_start(core)) {
        fprintf(stderr, "CAP-FAIL: start: %s\n", poke_last_error(core));
        return 1;
    }
    int frames = 0;
    while (frames < cap && poke_frame(core)) {
        frames++;
        if (frames % interval == 0) {
            char shot[512], st[512];
            snprintf(shot, sizeof(shot), "%s/fb-%06d.rgba", outdir, frames);
            snprintf(st, sizeof(st), "%s/state-%06d.ppz", outdir, frames);
            int w = 0, h = 0;
            bool ok = DumpFb(core, shot, &w, &h);
            bool sok = poke_save_state(core, st);
            printf("CAP: frame=%d shot=%d(%dx%d) state=%d\n",
                   frames, (int)ok, w, h, (int)sok);
            fflush(stdout);
        }
    }
    printf("CAP-DONE: frames=%d/%d\n", frames, cap);
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
