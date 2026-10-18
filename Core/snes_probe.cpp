// snes_probe.cpp — the host harness for the SNES console.
//
// One mode per task in docs/plans/alttp-snes-console.md. Every mode prints its
// claims as "<name>=<value>" lines and exits non-zero when a claim is false, so
// the shell script around it can be the only test runner.
//
// ⛔ No ROM is read by this file except through the core. Nothing here knows a
// game address; the modes that check addresses get them from the plan's
// measured table, and they print what they read.
#include "snes_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Usage(const char* argv0)
{
    std::fprintf(stderr,
                 "usage: %s --identity <rom> <savedir>\n"
                 "       %s --run <rom> <savedir> <frames>\n"
                 "       %s --input <rom> <savedir>\n"
                 "       %s --reset <rom> <savedir>\n"
                 "       %s --reads <rom> <savedir>\n",
                 argv0, argv0, argv0, argv0, argv0);
}

static uint32_t Fnv1a(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Task 1: the dump is recognised by its content, and the id is the CRC.
static int Identity(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: could not create the core\n"); return 1; }

    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code)) {
        std::printf("FAIL: load: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }

    unsigned crc = snes_rom_crc32(c);
    std::printf("crc=%08x\n", crc);
    std::printf("code=%s\n", code);

    // The USA 1.0 dump measured in the design: CRC32 777AAC2F. Anything else is
    // a different file, and this probe is not the place to pretend otherwise.
    bool ok = (crc == 0x777AAC2Fu) && (std::strcmp(code, "crc:777aac2f") == 0);
    std::printf("identity=%s\n", ok ? "YES" : "NO");
    snes_destroy(c);
    return ok ? 0 : 1;
}

// Runs <frames> frames and reports what the console did. `press_start` holds
// Start from frame 120 to 150, which is enough to leave the title screen.
struct RunResult {
    int frames = 0;
    unsigned console_frames = 0;
    int w = 0, h = 0;
    size_t nonzero = 0;
    uint32_t hash = 0;
    bool framebuffer = false;
};

static bool RunFrames(const char* rom, const char* savedir, int frames,
                      bool press_start, RunResult* out)
{
    SnesCore* c = snes_create();
    if (!c) return false;
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return false;
    }

    // Uncapped: a scripted drive should not cost real minutes. The proof is
    // about correctness, not pacing; pacing is measured on the phone.
    snes_set_uncapped(c, true);

    int ran = 0;
    for (; ran < frames; ran++) {
        if (press_start)
            snes_set_button(c, SNES_BTN_START, ran >= 120 && ran < 150);
        if (!snes_frame(c)) break;
    }

    out->frames = ran;
    out->console_frames = snes_console_frame_count(c);
    out->framebuffer = snes_framebuffer(c, &out->w, &out->h);
    const uint8_t* px = snes_framebuffer_ptr(c);
    if (out->framebuffer && px) {
        size_t n = (size_t) out->w * (size_t) out->h * 4u;
        out->hash = Fnv1a(px, n);
        for (size_t i = 0; i < n; i++)
            if (px[i]) out->nonzero++;
    } else {
        out->hash = 0;
    }

    snes_stop(c);
    snes_destroy(c);
    return true;
}

// Task 2: frames, video and a stop that leaves the core usable.
static int Run(const char* rom, const char* savedir, int frames)
{
    RunResult r;
    if (!RunFrames(rom, savedir, frames, false, &r)) return 1;

    std::printf("frames=%d\n", r.frames);
    std::printf("console=%u\n", r.console_frames);
    std::printf("fb=%dx%d\n", r.w, r.h);
    std::printf("nonzero=%zu\n", r.nonzero);
    std::printf("hash=%08x\n", r.hash);
    if (!r.framebuffer) std::printf("video=%s\n", snes_video_result(nullptr));

    // A black frame has a nonzero count near zero; a real SNES frame has tens
    // of thousands of nonzero bytes. The console counter must have advanced at
    // least once per call: a "frame" that never ran is the failure this catches.
    bool ok = r.frames == frames && r.framebuffer && r.w > 0 && r.h > 0 &&
              r.nonzero > 1000 && r.console_frames >= (unsigned) frames;
    std::printf("frames-ran=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

// Task 2, input: two identical runs, one with Start held, must differ in video.
static int Input(const char* rom, const char* savedir)
{
    RunResult quiet, pressed;
    if (!RunFrames(rom, savedir, 600, false, &quiet)) return 1;
    if (!RunFrames(rom, savedir, 600, true, &pressed)) return 1;

    std::printf("quiet-hash=%08x\n", quiet.hash);
    std::printf("start-hash=%08x\n", pressed.hash);
    bool ok = quiet.hash != 0 && pressed.hash != 0 && quiet.hash != pressed.hash;
    std::printf("input-changes-video=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

// Task 2, reset: Mesen replaces the console object on reset; the frame wait
// must follow it rather than watch a dead counter.
static int Reset(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: could not create the core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int before = 0;
    for (; before < 120; before++) if (!snes_frame(c)) break;

    snes_reset(c);

    int after = 0;
    for (; after < 120; after++) if (!snes_frame(c)) break;

    std::printf("before-reset=%d\n", before);
    std::printf("after-reset=%d\n", after);
    bool ok = before == 120 && after == 120;
    std::printf("reset-survives=%s\n", ok ? "YES" : "NO");
    if (!ok) std::printf("frame-result=%s error=%s\n", snes_frame_result(c), snes_last_error(c));

    snes_stop(c);
    snes_destroy(c);
    return ok ? 0 : 1;
}

// Task 3: work-RAM reads. Every assertion is printed with its answer, and the
// exit code is the AND of them, so the shell output IS the test record.
static int Reads(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: could not create the core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    for (int i = 0; i < 60; i++) snes_frame(c);
    uint32_t a = 0, b = 0;
    bool got_a = snes_read(c, 0x7E001Au, 1, &a);
    for (int i = 0; i < 60; i++) snes_frame(c);
    bool got_b = snes_read(c, 0x7E001Au, 1, &b);

    uint32_t v = 0;
    bool in_range_byte = snes_read(c, 0x7E0000u, 1, &v);
    bool in_range_word = snes_read(c, 0x7E0020u, 2, &v);
    bool far_bank_word = snes_read(c, 0x7F0000u, 2, &v);
    bool counter_moves = got_a && got_b && a != b;

    // Refusals: an MMIO address, a read that would cross the end of RAM, and
    // widths this console does not have.
    bool mmio_refused = !snes_read(c, 0x002100u, 1, &v);
    bool wrap_refused = !snes_read(c, 0x7FFFFFu, 2, &v);
    bool width_refused = !snes_read(c, 0x7E0000u, 0, &v) &&
                         !snes_read(c, 0x7E0000u, 3, &v);

    std::printf("in-range-byte       %s\n", in_range_byte ? "ok" : "FAILED");
    std::printf("in-range-word       %s\n", in_range_word ? "ok" : "FAILED");
    std::printf("frame-counter-moves %s (0x%02x -> 0x%02x)\n", counter_moves ? "ok" : "FAILED", a, b);
    std::printf("far-bank-word       %s\n", far_bank_word ? "ok" : "FAILED");
    std::printf("mmio-refused        %s\n", mmio_refused ? "ok" : "FAILED");
    std::printf("wrap-refused        %s\n", wrap_refused ? "ok" : "FAILED");
    std::printf("bad-width-refused   %s\n", width_refused ? "ok" : "FAILED");

    bool ok = in_range_byte && in_range_word && counter_moves && far_bank_word &&
              mmio_refused && wrap_refused && width_refused;
    std::printf("reads=%s\n", ok ? "YES" : "NO");
    snes_stop(c);
    snes_destroy(c);
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc < 2) { Usage(argv[0]); return 2; }

    if (std::strcmp(argv[1], "--identity") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Identity(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--run") == 0) {
        if (argc < 5) { Usage(argv[0]); return 2; }
        return Run(argv[2], argv[3], std::atoi(argv[4]));
    }
    if (std::strcmp(argv[1], "--input") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Input(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--reset") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Reset(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--reads") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Reads(argv[2], argv[3]);
    }

    Usage(argv[0]);
    return 2;
}
