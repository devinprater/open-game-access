// psp-capture-main.cpp — boot the game, dump raw RGBA screenshots and
// savestates every <interval> frames. Lets a sighted analyst see where the
// boot actually goes (logos? FMV? title?) without replaying blind.
#include <cstdint>
#include <cstring>
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
    if (argc != 6 && argc != 7 && argc != 10) {
        fprintf(stderr, "usage: %s <image> <savedir> <outdir> <frames> <interval> [script] | <image> <savedir> <outdir> <frames> <interval> <script-or--> <resume-state> <tap-btn> <tap-period>\n", argv[0]);
        return 2;
    }
    const char *image = argv[1], *savedir = argv[2], *outdir = argv[3];
    int cap = atoi(argv[4]), interval = atoi(argv[5]);
    // NDS/GBA cores need an accessibility script to start; capture runs a
    // bundled no-op (audio + frames flow without any reader logic).
    const char *script = (argc == 7) ? argv[6] : nullptr;
    // Input-probing mode: resume from a savestate, tap a button every
    // tap-period frames (held 60). argv[6] is script-or-placeholder.
    const char *resume = nullptr;
    int tapBtn = -1, tapPeriod = 0;
    if (argc == 10) {
        if (strcmp(argv[6], "--") != 0) script = argv[6];
        resume = argv[7];
        tapBtn = atoi(argv[8]);
        tapPeriod = atoi(argv[9]);
    }

    PokeCore *core = poke_create();
    if (!core) { fprintf(stderr, "CAP-FAIL: no core\n"); return 1; }
    // poke_set_script takes Lua SOURCE, not a path (NDS convention).
    std::string scriptSrc;
    if (script) {
        FILE *sf = fopen(script, "rb");
        if (!sf) { fprintf(stderr, "CAP-FAIL: no script file\n"); return 1; }
        char chunk[4096];
        size_t r;
        while ((r = fread(chunk, 1, sizeof(chunk), sf)) > 0)
            scriptSrc.append(chunk, r);
        fclose(sf);
        poke_set_script(core, scriptSrc.c_str());
    }
    if (!poke_load_rom(core, image, savedir)) {
        fprintf(stderr, "CAP-FAIL: load: %s\n", poke_last_error(core));
        return 1;
    }
    if (!poke_start(core)) {
        fprintf(stderr, "CAP-FAIL: start: %s\n", poke_last_error(core));
        return 1;
    }
    // Raw interleaved stereo s16 at the app's 32768 Hz (see poke_read_audio).
    // Matrix-surround analysis reads this, not the screenshots.
    std::string audioPath = std::string(outdir) + "/audio.s16";
    FILE *audio = fopen(audioPath.c_str(), "wb");
    int16_t abuf[3000 * 2];
    if (resume) {
        if (!poke_load_state(core, resume)) {
            fprintf(stderr, "CAP-FAIL: resume %s\n", resume);
            return 1;
        }
        fprintf(stderr, "CAP: resumed %s\n", resume);
    }
    int frames = 0;
    while (frames < cap && poke_frame(core)) {
        frames++;
        if (tapBtn >= 0 && tapPeriod > 0) {
            int ph = frames % tapPeriod;
            poke_set_button(core, tapBtn, ph < 60);
        }
        if (audio) {
            int got = poke_read_audio(core, abuf, 3000);
            if (got > 0) fwrite(abuf, sizeof(int16_t), (size_t)got * 2, audio);
        }
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
    if (audio) fclose(audio);
    printf("CAP-DONE: frames=%d/%d\n", frames, cap);
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
