/* ps2-map-harness.cpp — a headless driver for the PS2 core wrapper
 * (Core/ps2_core.cpp), built by scripts/ps2-map-harness.sh. It boots a
 * disc on the wrapper — never on a core directly — runs a frame count
 * with scripted button presses, and writes the evidence a proof needs:
 *
 *   boot.txt   the core's own name, the disc serial the wrapper read,
 *              the reported frame rate, frames run, wall time, and the
 *              per-frame cost of ps2_frame (p50 / p95 / p99 / max — the
 *              honest-speed numbers: a JIT core's percentiles say more
 *              than an average ever will);
 *   frame.ppm  the last frame the wrapper's framebuffer held;
 *   ram.bin    the 32 MiB of EE RAM read back through ps2_read (only
 *              with --dump-ram) — a game that is executing fills it with
 *              its own data, which a logo screen never does.
 *
 * Usage:
 *   ps2-map-harness --rom DISC.iso --out DIR [--core CORE.so]
 *                   [--cores DIR] [--system DIR] [--frames N]
 *                   [--press BUTTON:AT:HOLD ...] [--dump-ram]
 *                   [--state-out FILE] [--state-in FILE]
 *
 * The disc and the BIOS are the player's own: read from where they are,
 * never copied into the repo. --system points at the BIOS staging folder
 * (pcsx2/bios under it), exactly as the app stages it.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <ctime>

#include "ps2_core.h"

namespace {

struct Press { int button; long at; long hold; };

const char *kButtonNames[PS2_BTN_COUNT] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2",
    "select", "start", "up", "down", "left", "right", "l3", "r3",
};

int ButtonFromName(const std::string &s)
{
    for (int i = 0; i < PS2_BTN_COUNT; i++)
        if (s == kButtonNames[i]) return i;
    return -1;
}

bool WritePpm(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(rgba + (size_t) i * 4, 1, 3, f);
    fclose(f);
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    std::string rom, outDir, corePath, coresDir, systemDir, stateOut, stateIn;
    long frames = 3600;
    bool dumpRam = false;
    std::vector<Press> presses;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", what); exit(2); }
            return argv[++i];
        };
        if (a == "--rom") rom = next("--rom");
        else if (a == "--out") outDir = next("--out");
        else if (a == "--core") corePath = next("--core");
        else if (a == "--cores") coresDir = next("--cores");
        else if (a == "--system") systemDir = next("--system");
        else if (a == "--frames") frames = atol(next("--frames").c_str());
        else if (a == "--state-out") stateOut = next("--state-out");
        else if (a == "--state-in") stateIn = next("--state-in");
        else if (a == "--dump-ram") dumpRam = true;
        else if (a == "--press") {
            std::string spec = next("--press");
            size_t c1 = spec.find(':'), c2 = spec.rfind(':');
            if (c1 == std::string::npos || c2 == c1) { fprintf(stderr, "bad --press %s\n", spec.c_str()); return 2; }
            int b = ButtonFromName(spec.substr(0, c1));
            if (b < 0) { fprintf(stderr, "unknown button in --press %s\n", spec.c_str()); return 2; }
            presses.push_back({ b, atol(spec.substr(c1 + 1, c2 - c1 - 1).c_str()),
                                   atol(spec.substr(c2 + 1).c_str()) });
        } else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (rom.empty() || outDir.empty()) {
        fprintf(stderr, "usage: ps2-map-harness --rom DISC --out DIR [--core SO] [--cores DIR] [--system DIR] [--frames N]\n");
        return 2;
    }
    std::string mk = "mkdir -p \"" + outDir + "\"";
    if (system(mk.c_str()) != 0) return 2;

    Ps2Core *core = ps2_create();
    if (!core) { fprintf(stderr, "ps2_create refused: %s\n", ps2_create_error()); return 1; }
    if (!corePath.empty()) ps2_set_core_path(core, corePath.c_str());
    if (!coresDir.empty()) ps2_set_core_dir(core, coresDir.c_str());
    if (!systemDir.empty()) ps2_set_bios_dir(core, systemDir.c_str());

    std::string saves = outDir + "/saves";
    char code[16];
    if (!ps2_load_disc(core, rom.c_str(), saves.c_str(), code)) {
        fprintf(stderr, "load failed: %s\n", ps2_last_error(core));
        ps2_destroy(core);
        return 1;
    }
    printf("CORE %s\n", ps2_core_name(core));
    printf("CODE %s\n", code[0] ? code : "(none)");
    printf("FPS %.3f\n", ps2_frame_rate(core));
    printf("HLE %s\n", ps2_using_hle_bios(core) ? "yes" : "no");
    if (!ps2_start(core)) { fprintf(stderr, "start failed: %s\n", ps2_last_error(core)); return 1; }

    if (!stateIn.empty()) {
        if (!ps2_load_state(core, stateIn.c_str()))
            fprintf(stderr, "state-in refused: %s\n", ps2_last_error(core));
    }

    std::vector<double> runMs;
    runMs.reserve((size_t) frames);
    std::vector<int16_t> audioBuf(32768);
    long long audioFrames = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long f = 0; f < frames; f++) {
        for (const Press &p : presses) {
            if (f == p.at) ps2_set_button(core, p.button, true);
            if (f == p.at + p.hold) ps2_set_button(core, p.button, false);
        }
        auto s = std::chrono::steady_clock::now();
        if (!ps2_frame(core)) { fprintf(stderr, "frame %ld failed\n", f); break; }
        auto e = std::chrono::steady_clock::now();
        runMs.push_back(std::chrono::duration<double, std::milli>(e - s).count());
        audioFrames += ps2_read_audio(core, audioBuf.data(), 8192);
        if (f % 600 == 599) {   /* keep a mid-run frame too: boot screens lie late */
            int w = 0, h = 0;
            if (ps2_framebuffer(core, &w, &h)) {
                char name[64];
                snprintf(name, sizeof(name), "/frame-%ld.ppm", f + 1);
                WritePpm((outDir + name).c_str(), ps2_framebuffer_ptr(core), w, h);
            }
        }
    }
    /* An async core (retro_run returns at once; frames present on the
     * core's own thread) may not have shown anything yet when the loop
     * above ends. Give it a paced wait for its first frame so the
     * evidence below is the game's, not the race's. */
    {
        int pw = 0, ph = 0;
        if (!ps2_framebuffer(core, &pw, &ph)) {
            for (int i = 0; i < 10000; i++) {
                ps2_frame(core);
                if (ps2_framebuffer(core, &pw, &ph)) break;
                struct timespec ts { 0, 1000000 };
                nanosleep(&ts, nullptr);
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(t1 - t0).count();

    int w = 0, h = 0;
    bool haveFb = ps2_framebuffer(core, &w, &h);
    if (haveFb) WritePpm((outDir + "/frame.ppm").c_str(), ps2_framebuffer_ptr(core), w, h);

    if (!stateOut.empty()) {
        if (!ps2_save_state(core, stateOut.c_str()))
            fprintf(stderr, "state-out refused: %s\n", ps2_last_error(core));
    }

    if (dumpRam) {
        FILE *f = fopen((outDir + "/ram.bin").c_str(), "wb");
        if (f) {
            std::vector<uint8_t> page(65536);
            for (uint32_t base = 0; base < PS2_RAM_SIZE; base += (uint32_t) page.size()) {
                bool ok = true;
                for (size_t i = 0; i < page.size(); i += 4) {
                    uint32_t v = 0;
                    if (!ps2_read(core, base + (uint32_t) i, 4, &v)) { ok = false; break; }
                    memcpy(page.data() + i, &v, 4);
                }
                if (!ok) { fprintf(stderr, "ram read failed at 0x%x\n", base); break; }
                fwrite(page.data(), 1, page.size(), f);
            }
            fclose(f);
        }
    }

    std::sort(runMs.begin(), runMs.end());
    auto pct = [&](double q) -> double {
        if (runMs.empty()) return 0.0;
        size_t i = (size_t) (q * (double) (runMs.size() - 1));
        return runMs[i];
    };
    printf("FRAMES %llu\n", ps2_frames_completed(core));
    printf("WALL %.2f s\n", wall);
    printf("RUNMS p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(0.50), pct(0.95), pct(0.99),
           runMs.empty() ? 0.0 : runMs.back());
    printf("AUDIO %lld frames drained at %d Hz\n", audioFrames, PS2_APP_AUDIO_RATE);
    printf("FB %s %dx%d\n", haveFb ? "yes" : "no", w, h);

    FILE *b = fopen((outDir + "/boot.txt").c_str(), "w");
    if (b) {
        fprintf(b, "core=%s\ncode=%s\nfps=%.3f\nhle=%s\nframes=%llu\nwall=%.2f\n"
                   "runms_p50=%.2f p95=%.2f p99=%.2f max=%.2f\naudio=%lld\nfb=%dx%d\n",
                ps2_core_name(core), code[0] ? code : "(none)", ps2_frame_rate(core),
                ps2_using_hle_bios(core) ? "yes" : "no", ps2_frames_completed(core), wall,
                pct(0.50), pct(0.95), pct(0.99), runMs.empty() ? 0.0 : runMs.back(),
                audioFrames, w, h);
        fclose(b);
    }
    ps2_stop(core);
    ps2_destroy(core);
    return 0;
}
