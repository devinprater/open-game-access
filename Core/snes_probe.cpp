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
#include "oga_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

static void Usage(const char* argv0)
{
    std::fprintf(stderr,
                 "usage: %s --identity <rom> <savedir>\n"
                 "       %s --run <rom> <savedir> <frames>\n"
                 "       %s --input <rom> <savedir>\n"
                 "       %s --reset <rom> <savedir>\n"
                 "       %s --reads <rom> <savedir>\n"
                 "       %s --states <rom> <savedir>\n"
                 "       %s --headered <rom> <savedir> <headered-copy>\n"
                 "       %s --after-stop <rom> <savedir>\n",
                 argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
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
    unsigned crc = 0;
    unsigned console_frames = 0;
    int w = 0, h = 0;
    size_t nonzero = 0;
    uint32_t hash = 0;
    unsigned module = 0;
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
            snes_set_button(c, SNES_BTN_START, ran >= 30 && ran < 90);
        if (!snes_frame(c)) break;
    }

    out->crc = snes_rom_crc32(c);
    snes_read(c, 0x7E0010u, 1, &out->module);
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

// Task 2, input: the host's button press must reach Mesen's SNES controller.
// ⛔ MEASURED: the screen is not a usable oracle here. The guest reaches attract
// mode early and uncapped frame capture is not frame-exact, so two identical runs
// produced three different hashes. The pad state is deterministic.
static int Input(const char* rom, const char* savedir)
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

    for (int i = 0; i < 5; i++) snes_frame(c);
    unsigned idle = snes_provider_bits(c);
    (void) snes_controller_bits(c);

    snes_set_button(c, SNES_BTN_START, true);
    for (int i = 0; i < 2; i++) snes_frame(c);
    unsigned held = snes_provider_bits(c);

    snes_set_button(c, SNES_BTN_START, false);
    for (int i = 0; i < 2; i++) snes_frame(c);
    unsigned released = snes_provider_bits(c);

    std::printf("provider-calls=%d last-type=%d\n", snes_provider_calls(c), snes_provider_last_type(c));
    std::printf("idle-bits=%08x\n", idle);
    std::printf("start-bits=%08x\n", held);
    std::printf("released-bits=%08x\n", released);

    bool ok = idle == 0 && (held & (1u << SNES_BTN_START)) != 0 && released == 0;
    std::printf("input-reaches-pad=%s\n", ok ? "YES" : "NO");
    snes_stop(c);
    snes_destroy(c);
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

// Task 4: the resolver. Which backend a file reaches is the decision that must
// not drift; every line is a path and the id it must produce.
static int Resolve()
{
    struct Case { const char* path; const char* want; };
    static const Case cases[] = {
        { "/roms/alttp.sfc",   "snes" },
        { "/roms/alttp.smc",   "snes" },
        { "/roms/alttp.fig",   "snes" },
        { "/roms/alttp.swc",   "snes" },
        { "/roms/pokemon.gb",  "gba"  },   // the Game Boy backend, never SNES
        { "/roms/zelda1.nes",  "nes"  },
        { "/roms/game.nds",    "nds"  },
        { "/roms/thing.xyz",   "(none)" },
    };

    int fails = 0;
    for (const Case& c : cases) {
        const OgaResolvedBackend* b = oga_resolve_backend(c.path);
        const char* got = b ? b->id : "(none)";
        bool ok = std::strcmp(got, c.want) == 0;
        std::printf("%-16s -> %-6s %s\n", c.path, got, ok ? "ok" : "FAILED");
        if (!ok) fails++;
    }
    std::printf("resolve=%s\n", fails == 0 ? "YES" : "NO");
    return fails == 0 ? 0 : 1;
}

// Task 6: states. Save, let the game move, load, and the counter must come back
// to the saved value exactly.
static int States(const char* rom, const char* savedir)
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

    for (int i = 0; i < 120; i++) snes_frame(c);
    uint32_t saved_val = 0, later_val = 0, restored_val = 0;
    bool saved_ok = snes_read(c, 0x7E001Au, 1, &saved_val);

    std::string path = std::string(savedir) + "/alttp.state";
    bool wrote = snes_save_state(c, path.c_str());

    for (int i = 0; i < 60; i++) snes_frame(c);
    bool later_ok = snes_read(c, 0x7E001Au, 1, &later_val);

    bool loaded = snes_load_state(c, path.c_str());
    bool restored_ok = snes_read(c, 0x7E001Au, 1, &restored_val);

    std::printf("saved=%u\n", saved_val);
    std::printf("later=%u\n", later_val);
    std::printf("restored=%u\n", restored_val);

    bool ok = saved_ok && wrote && later_ok && loaded && restored_ok &&
              later_val != saved_val && restored_val == saved_val;
    if (!ok) std::printf("error=%s\n", snes_last_error(c));
    std::printf("state-restores=%s\n", ok ? "YES" : "NO");
    snes_stop(c);
    snes_destroy(c);
    return ok ? 0 : 1;
}

// Review fix: a headered dump must be the same game, not just the same CRC.
// Same payload CRC AND the same frame hash after 60 frames is the proof that
// Mesen stripped the same bytes the id was computed over.
static int Headered(const char* rom, const char* savedir, const char* other)
{
    RunResult plain, headered;
    if (!RunFrames(rom, savedir, 60, false, &plain)) return 1;
    if (!RunFrames(other, savedir, 60, false, &headered)) return 1;

    std::printf("plain-crc=%08x plain-hash=%08x\n", plain.crc, plain.hash);
    std::printf("headered-crc=%08x headered-hash=%08x\n", headered.crc, headered.hash);
    bool ok = plain.crc == headered.crc && plain.hash == headered.hash;
    std::printf("headered-same=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

// Review fix: after a stop, the read must refuse and the reset must not crash.
static int AfterStop(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int i = 0; i < 10; i++) snes_frame(c);
    snes_stop(c);

    uint32_t v = 0xDEADBEEFu;
    bool read_after_stop = snes_read(c, 0x7E001Au, 1, &v);
    snes_reset(c);          // must be a no-op, not a null-console dereference
    bool ok = !read_after_stop;
    std::printf("read-after-stop=%s\n", read_after_stop ? "ANSWERED (bad)" : "refused");
    std::printf("reset-after-stop=survived\n");
    std::printf("after-stop=%s\n", ok ? "YES" : "NO");
    snes_destroy(c);
    return ok ? 0 : 1;
}

// Task 2 of the reader plan: observe the candidate reader addresses while the
// game boots. This prints, it does not assert: the point is a record of what the
// bytes actually do, not a pass.
static void DumpReaderAddresses(SnesCore* c, int frame)
{
    uint32_t mod = 0, sub = 0, indoors = 0, x = 0, y = 0, screen = 0, room = 0, area = 0;
    uint32_t cap = 0, cur = 0, magic = 0, rupees = 0;
    snes_read(c, 0x7E0010u, 1, &mod);
    snes_read(c, 0x7E0011u, 1, &sub);
    snes_read(c, 0x7E001Bu, 1, &indoors);
    snes_read(c, 0x7E0022u, 2, &x);
    snes_read(c, 0x7E0020u, 2, &y);
    snes_read(c, 0x7E008Au, 2, &screen);
    snes_read(c, 0x7E00A0u, 2, &room);
    snes_read(c, 0x7E040Au, 2, &area);
    snes_read(c, 0x7EF36Cu, 1, &cap);
    snes_read(c, 0x7EF36Du, 1, &cur);
    snes_read(c, 0x7EF36Eu, 1, &magic);
    snes_read(c, 0x7EF362u, 2, &rupees);
    std::printf("f=%3d mod=%u sub=%u in=%u x=%u y=%u scr=%u room=%u area=%u hp=%u/%u mag=%u rup=%u\n",
                frame, mod, sub, indoors, x, y, screen, room, area, cur, cap, magic, rupees);
}

static int Memory(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    for (int f = 0; f <= 600; f++) {
        if (f == 120) snes_set_button(c, SNES_BTN_START, true);
        if (f == 160) snes_set_button(c, SNES_BTN_START, false);
        if (f % 60 == 0) DumpReaderAddresses(c, f);
        snes_frame(c);
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Drives the boot with periodic Start presses and prints the module state, so
// the screen flow is observed rather than assumed.
static int Drive(const char* rom, const char* savedir, int frames)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int last_mod = -1, last_sub = -1;
    for (int f = 0; f < frames; f++) {
        /* Start for 4 frames every 90, then A for 4 frames 30 later: enough for a
         * title screen, a file slot and a name entry to each accept a press. */
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);

        uint32_t mod = 0, sub = 0;
        snes_read(c, 0x7E0010u, 1, &mod);
        snes_read(c, 0x7E0011u, 1, &sub);
        if ((int) mod != last_mod || (int) sub != last_sub) {
            std::printf("f=%4d module=%u submodule=%u\n", f, mod, sub);
            last_mod = (int) mod;
            last_sub = (int) sub;
        }
    }
    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Drives into gameplay (Start at the title, A at the file slot, Start at name
// entry), then walks Link with Down so the reader's coordinates and area are
// observed while the game is actually running.
static int Gameplay(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    for (; f < 4200; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }

    std::printf("-- after the drive --\n");
    DumpReaderAddresses(c, f);

    for (int i = 0; i < 1800; i++, f++) {
        bool walk = (i % 180) < 120;
        snes_set_button(c, SNES_BTN_DOWN, walk && ((i / 180) % 2 == 0));
        snes_set_button(c, SNES_BTN_LEFT, walk && ((i / 180) % 2 == 1));
        snes_set_button(c, SNES_BTN_A, (i % 120) >= 100 && (i % 120) < 104);
        snes_frame(c);
        if (i % 60 == 0) DumpReaderAddresses(c, f);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_LEFT, false);
    snes_set_button(c, SNES_BTN_DOWN, false);

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// After the boot drive, try each direction in turn and print the coordinates,
// so the scripted drive can tell a wall from a locked intro.
static int Explore(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    // Boot: Start at the title, A on the file slot, Start at name entry, and a
    // steady A tap through the opening text.
    for (; f < 3600; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 120) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 60) >= 50 && (f % 60) < 54);
        snes_frame(c);
    }
    std::printf("-- boot done --\n");
    DumpReaderAddresses(c, f);

    static const int dirs[] = { SNES_BTN_DOWN, SNES_BTN_RIGHT, SNES_BTN_UP, SNES_BTN_LEFT };
    static const char* names[] = { "down", "right", "up", "left" };
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 120; i++, f++) {
            snes_set_button(c, dirs[d], true);
            snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
            snes_frame(c);
        }
        snes_set_button(c, dirs[d], false);
        std::printf("-- after %s --\n", names[d]);
        DumpReaderAddresses(c, f);
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Walks north out of Link's house, tapping A through the opening text, and
// prints the reader state so leaving the house is observed rather than assumed.
static int Leave(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    for (; f < 3600; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 120) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 60) >= 50 && (f % 60) < 54);
        snes_frame(c);
    }
    std::printf("-- boot done --\n");
    DumpReaderAddresses(c, f);

    for (int i = 0; i < 3600; i++, f++) {
        snes_set_button(c, SNES_BTN_UP, true);
        snes_set_button(c, SNES_BTN_A, (i % 45) >= 40 && (i % 45) < 44);
        snes_frame(c);
        if (i % 300 == 0) DumpReaderAddresses(c, f);
    }
    snes_set_button(c, SNES_BTN_UP, false);

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Sweeps along the top wall looking for the house door: diagonally up-right,
// then up-left, repeating, and stops the moment the game says it is outdoors.
static int Wander(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    for (; f < 3600; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 120) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 60) >= 50 && (f % 60) < 54);
        snes_frame(c);
    }
    std::printf("-- boot done --\n");
    DumpReaderAddresses(c, f);

    for (int leg = 0; leg < 24; leg++, f += 180) {
        bool right = (leg % 2) == 0;
        for (int i = 0; i < 180; i++) {
            snes_set_button(c, SNES_BTN_UP, true);
            snes_set_button(c, SNES_BTN_RIGHT, right);
            snes_set_button(c, SNES_BTN_LEFT, !right);
            snes_set_button(c, SNES_BTN_A, (i % 45) >= 40 && (i % 45) < 44);
            snes_frame(c);
        }
        std::printf("-- leg %d (%s) --\n", leg, right ? "up-right" : "up-left");
        DumpReaderAddresses(c, f);
        uint32_t indoors = 0;
        snes_read(c, 0x7E001Bu, 1, &indoors);
        if (indoors == 0) { std::printf("OUTDOORS\n"); break; }
    }

    snes_set_button(c, SNES_BTN_UP, false);
    snes_set_button(c, SNES_BTN_RIGHT, false);
    snes_set_button(c, SNES_BTN_LEFT, false);
    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Boots with a real save file present: Start past the title, Start on the file
// slot, then observe the reader state and walk.
static int Resume(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    for (; f < 900; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 120) < 4);
        snes_frame(c);
    }
    std::printf("-- after the file select --\n");
    DumpReaderAddresses(c, f);

    static const int dirs[] = { SNES_BTN_DOWN, SNES_BTN_LEFT, SNES_BTN_UP, SNES_BTN_RIGHT };
    static const char* names[] = { "down", "left", "up", "right" };
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 120; i++, f++) {
            snes_set_button(c, dirs[d], true);
            snes_frame(c);
        }
        snes_set_button(c, dirs[d], false);
        std::printf("-- after %s --\n", names[d]);
        DumpReaderAddresses(c, f);
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// With a real save present: drive into the game, then walk each direction and
// dump the reader state, so outdoor area/screen ids are measured.
static int Measure(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    snes_set_uncapped(c, true);

    int f = 0;
    for (; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    std::printf("-- after the drive --\n");
    DumpReaderAddresses(c, f);

    static const int dirs[] = { SNES_BTN_DOWN, SNES_BTN_LEFT, SNES_BTN_UP, SNES_BTN_RIGHT };
    static const char* names[] = { "down", "left", "up", "right" };
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 180; i++, f++) {
            snes_set_button(c, dirs[d], true);
            /* A tap dismisses a message box; without it the text holds the player. */
            snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
            snes_frame(c);
        }
        snes_set_button(c, SNES_BTN_A, false);
        snes_set_button(c, dirs[d], false);
        std::printf("-- after %s --\n", names[d]);
        DumpReaderAddresses(c, f);
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
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
    if (std::strcmp(argv[1], "--resolve") == 0) return Resolve();
    if (std::strcmp(argv[1], "--headered") == 0) {
        if (argc < 5) { Usage(argv[0]); return 2; }
        return Headered(argv[2], argv[3], argv[4]);
    }
    if (std::strcmp(argv[1], "--measure") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Measure(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--resume") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Resume(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--wander") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Wander(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--leave") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Leave(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--explore") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Explore(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--gameplay") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Gameplay(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--drive") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Drive(argv[2], argv[3], argc > 4 ? std::atoi(argv[4]) : 1800);
    }
    if (std::strcmp(argv[1], "--memory") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Memory(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--after-stop") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return AfterStop(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--states") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return States(argv[2], argv[3]);
    }

    Usage(argv[0]);
    return 2;
}
