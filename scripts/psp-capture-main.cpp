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

static FILE *g_speakLog = nullptr;
static int g_capFrame = 0;

static void CapSpeak(const char *text, bool interrupt, void *ud) {
    (void)ud;
    if (g_speakLog && text) {
        fprintf(g_speakLog, "frame=%d intr=%d %s\n", g_capFrame,
                (int)interrupt, text);
        fflush(g_speakLog);
    }
}

int main(int argc, char **argv) {
    // Battle-probe mode appends: <cmd> <cmd-period> — issue adapter command
    // <cmd> every <cmd-period> frames and log every SPEAK line with its frame.
    if (argc != 6 && argc != 7 && argc != 10 && argc != 12 && argc != 13 && argc != 15) {
        fprintf(stderr, "usage: %s <image> <savedir> <outdir> <frames> <interval> [script] | <image> <savedir> <outdir> <frames> <interval> <script-or--> <resume-state> <tap-btn> <tap-period> [<cmd> <cmd-period>]\n", argv[0]);
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
    int tapCycle[8]; int tapCycleN = 0;
    int probeCmd = -1, probePeriod = 0;
    int tapHold = 60;
    if (argc == 10 || argc == 12 || argc == 13 || argc == 15) {
        if (strcmp(argv[6], "--") != 0) script = argv[6];
        resume = argv[7];
        // tap button may be a comma cycle ("10,11,0") for scripted mash.
        {
            const char *p8 = argv[8];
            while (*p8 && tapCycleN < 8) {
                tapCycle[tapCycleN++] = atoi(p8);
                while (*p8 && *p8 != ',') p8++;
                if (*p8 == ',') p8++;
            }
            if (tapCycleN > 0) tapBtn = tapCycle[0];
        }
        tapPeriod = atoi(argv[9]);
    }
    if (argc == 12) {
        probeCmd = atoi(argv[10]);
        probePeriod = atoi(argv[11]);
    }
    if (argc == 13 || argc == 15) {
        probeCmd = atoi(argv[10]);
        probePeriod = atoi(argv[11]);
        tapHold = atoi(argv[argc - 1]);
    }
    PokeCore *core = poke_create();
    if (!core) { fprintf(stderr, "CAP-FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, CapSpeak, nullptr);
    static FILE *g_hostLog = nullptr;
    {
        std::string hlog = std::string(argv[3]) + "/host.log";
        g_hostLog = fopen(hlog.c_str(), "w");
    }
    struct HostLogCap { static void cb(const char *t, void *u) {
        FILE *f = (FILE *)u;
        if (f && t) { fprintf(f, "%s\n", t); fflush(f); }
    } };
    poke_set_log_callback(core, HostLogCap::cb, g_hostLog);
    if (probeCmd >= 0) {
        std::string slog = std::string(argv[3]) + "/speak.log";
        g_speakLog = fopen(slog.c_str(), "w");
    }

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
        g_capFrame = frames;
        if (probeCmd >= 0 && probePeriod > 0 && (frames % probePeriod) == 0)
            poke_command(core, probeCmd);
        if (tapBtn >= 0 && tapPeriod > 0) {
            int ph = frames % tapPeriod;
            int cyc = tapCycleN > 0 ? tapCycle[(frames / tapPeriod) % tapCycleN] : tapBtn;
            bool want = ph < tapHold;
            static int lastCyc = -999;
            if (cyc != lastCyc) {
                if (lastCyc >= 0) poke_set_button(core, lastCyc, false);
                lastCyc = cyc;
            }
            tapBtn = cyc;
            static bool had = false;
            static int lastLoggedCyc = -999;
            if (want != had || cyc != lastLoggedCyc) {
                fprintf(stderr, "CAP-INPUT: frame=%d btn=%d %s\n",
                        frames, cyc, want ? "DOWN" : "UP");
                had = want; lastLoggedCyc = cyc;
            }
            poke_set_button(core, cyc, want);
        }
        if (audio) {
            int got = poke_read_audio(core, abuf, 3000);
            if (got > 0) fwrite(abuf, sizeof(int16_t), (size_t)got * 2, audio);
        }
        if (frames % interval == 0) {
            // Release every button before the savestate: PPSSPP persists the
            // latched pad, so a state saved mid-hold resumes with key-repeat
            // running and the next run's inputs land on the wrong rows.
            for (unsigned b = 0; b < 16; b++) poke_set_button(core, b, false);
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
    if (g_speakLog) fclose(g_speakLog);
    if (g_hostLog) fclose(g_hostLog);
    printf("CAP-DONE: frames=%d/%d\n", frames, cap);
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
