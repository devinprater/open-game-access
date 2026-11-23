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
#include "alttp_graph.h"
#include "oga_core.h"
#include "alttp_text.h"

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
    uint32_t saved_mod = 0, msg = 0, incap = 0, palace = 0;
    snes_read(c, 0x7E010Cu, 1, &saved_mod);
    snes_read(c, 0x7E1CF0u, 2, &msg);
    snes_read(c, 0x7E0046u, 1, &incap);
    snes_read(c, 0x7E040Cu, 2, &palace);
    /* Story progress (zelda3 variables.h): sram_progress_indicator at 0xF3C5,
     * sram_progress_flags at 0xF3C6, link_which_pendants at 0xF374, and the
     * sword we already read elsewhere. These name the objective stage. */
    uint32_t prog = 0, flags = 0, pend = 0, sword = 0;
    snes_read(c, 0x7EF3C5u, 1, &prog);
    snes_read(c, 0x7EF3C6u, 1, &flags);
    snes_read(c, 0x7EF374u, 1, &pend);
    snes_read(c, 0x7EF359u, 1, &sword);
    std::printf("f=%3d mod=%u sub=%u real=%u in=%u x=%u y=%u scr=%u room=%u area=%u pal=%04x hp=%u/%u mag=%u rup=%u msg=%u incap=%u prog=%u flags=%02x pend=%u sword=%u\n",
                frame, mod, sub, saved_mod, indoors, x, y, screen, room, area, palace, cur, cap, magic, rupees, msg, incap,
                prog, flags, pend, sword);

    /* ⛔ DECODE THE MESSAGE WHILE IT IS UP. The adapter only speaks a message
     * when its index changes, so a dump that shows the index but not the words
     * cannot tell a real line from cursor decoration. */
    if (msg) {
        uint8_t buf[128];
        for (int i = 0; i < (int) sizeof(buf); i++) {
            uint32_t v = 0;
            snes_read(c, 0x7F1200u + (uint32_t) i, 1, &v);
            buf[i] = (uint8_t) v;
        }
        char text[192];
        int n = oga::AlttpDecodeText(buf, (int) sizeof(buf), text, (int) sizeof(text));
        if (n > 0) std::printf("    msg-text: %s\n", text);
    }
}

/* ---- talk: find a real message and decode it -------------------------------
 * The adapter speaks a message when its index changes, so a live proof needs a
 * message the GAME shows, not one a dump happens to leave in the buffer. This
 * mode walks out of the house, then pokes and prods in four directions,
 * printing every message the game raises.
 */
static void TalkDump(SnesCore* c, const char* tag, int frame)
{
    uint32_t msg = 0, area = 0, indoors = 0;
    snes_read(c, 0x7E1CF0u, 2, &msg);
    snes_read(c, 0x7E040Au, 2, &area);
    snes_read(c, 0x7E001Bu, 1, &indoors);
    if (!msg) {
        std::printf("f=%d %s msg=0 area=%u in=%u\n", frame, tag, area, indoors);
        return;
    }
    uint8_t buf[128];
    for (int i = 0; i < (int) sizeof(buf); i++) {
        uint32_t v = 0;
        snes_read(c, 0x7F1200u + (uint32_t) i, 1, &v);
        buf[i] = (uint8_t) v;
    }
    char text[192];
    int n = oga::AlttpDecodeText(buf, (int) sizeof(buf), text, (int) sizeof(text));
    std::printf("f=%d %s msg=%u area=%u in=%u text=\"%s\"\n", frame, tag, msg, area, indoors,
                n > 0 ? text : "");
    if (frame <= 1534) {
        std::printf("    hex:");
        for (int i = 0; i < 128; i++) std::printf(" %02x", buf[i]);
        std::printf("\n");
    }
}

static int Talk(const char* rom, const char* savedir)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);

    /* Close the interface overlay the boot taps opened. */
    for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); f++; }
    for (int i = 0; i < 30; i++) { snes_set_button(c, SNES_BTN_START, false); snes_frame(c); f++; }

    TalkDump(c, "loaded", f);

    /* Walk out of the house the way the walk proof does. */
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c);
        f++;
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 30; i++) { snes_frame(c); f++; }
    TalkDump(c, "outside", f);

    const struct { int btn; const char* name; } dirs[] = {
        { SNES_BTN_UP, "up" }, { SNES_BTN_DOWN, "down" },
        { SNES_BTN_LEFT, "left" }, { SNES_BTN_RIGHT, "right" },
    };
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 40; i++) { snes_set_button(c, dirs[d].btn, true); snes_frame(c); f++; }
        snes_set_button(c, dirs[d].btn, false);
        for (int i = 0; i < 10; i++) { snes_frame(c); f++; }
        for (int a = 0; a < 4; a++) {
            for (int i = 0; i < 3; i++) { snes_set_button(c, SNES_BTN_A, true); snes_frame(c); f++; }
            for (int i = 0; i < 17; i++) { snes_set_button(c, SNES_BTN_A, false); snes_frame(c); f++; }
            TalkDump(c, dirs[d].name, f);
        }
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

/* ---- dungeon: drive the real save to a dungeon ----------------------------
 * Keys and room notes had no live proof because nothing had ever walked into a
 * dungeon. This mode boots the save, walks out, then uses an eight-way greedy
 * walk toward the target area with a stuck detector and a detour, and writes a
 * savestate once the player is indoors with a dungeon id. Screenshots are
 * optional, because a wrong turn is easier to see than to infer from RAM.
 */
struct NavPos { uint32_t x, y; uint32_t room; uint16_t area; uint8_t indoors, palace; };

static bool WritePpm(SnesCore* c, const char* path);   /* defined below */

/* ⛔ THE BOOT TAPS LEAVE THE PAUSE MENU OPEN OR CLOSED AT RANDOM. A fixed
 * "press START once" turns a closed menu into an open one, and the walk then
 * goes nowhere while the game ignores the d-pad. Read the module and close it
 * only when it is the interface. */
static void CloseMenu(SnesCore* c)
{
    for (int tries = 0; tries < 4; tries++) {
        uint32_t mod = 0;
        snes_read(c, 0x7E0010u, 1, &mod);
        if (mod != 14) return;
        for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
        for (int i = 0; i < 24; i++) { snes_set_button(c, SNES_BTN_START, false); snes_frame(c); }
    }
}

static NavPos ReadNav(SnesCore* c)
{
    NavPos p = {};
    uint32_t v = 0;
    snes_read(c, 0x7E0022u, 2, &p.x);
    snes_read(c, 0x7E0020u, 2, &p.y);
    snes_read(c, 0x7E040Au, 2, &v); p.area = (uint16_t) v;
    snes_read(c, 0x7E001Bu, 1, &v); p.indoors = (uint8_t) v;
    snes_read(c, 0x7E040Cu, 1, &v); p.palace = (uint8_t) v;
    snes_read(c, 0x7E00A0u, 2, &v); p.room = v;
    return p;
}

/* ---- teleport: a harness fixture, never an adapter feature ----------------
 * Walking to the Eastern Palace has failed on terrain every time. This moves
 * Link with snes_write instead: a few pixels a frame toward the palace screen,
 * then a real walk up into the door. Everything after the door is the game's own
 * state. The cheat is confined to this probe mode. */
static int Teleport(const char* rom, const char* savedir, const char* stateout)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    CloseMenu(c);
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c); f++;
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 40; i++) { snes_frame(c); f++; }
    CloseMenu(c);

    /* 0x1E Eastern Ruins NW: x 3072-3583, y 1536-2047. Land south of the door. */
    const int tx = 3328, ty = 1800;
    NavPos start = ReadNav(c);
    std::printf("start area=%u x=%u y=%u in=%u pal=%02x -> %d,%d\n",
                start.area, start.x, start.y, start.indoors, start.palace, tx, ty);

    for (int i = 0; i < 9000; i++) {
        NavPos p = ReadNav(c);
        if (p.indoors) break;
        if (p.area == 0x1E && p.y <= 1900) break;
        int dx = tx - (int) p.x, dy = ty - (int) p.y;
        if (dx == 0 && dy == 0) break;
        /* Walking speed, not a jump: the screen-transition code has to see a
         * normal crossing, or the world keeps the old screen while the
         * coordinates move. */
        int sx = dx > 4 ? 4 : (dx < -4 ? -4 : dx);
        int sy = dy > 4 ? 4 : (dy < -4 ? -4 : dy);
        snes_write(c, 0x7E0022u, 2, (uint32_t) ((int) p.x + sx) & 0xFFFF);
        snes_write(c, 0x7E0020u, 2, (uint32_t) ((int) p.y + sy) & 0xFFFF);
        snes_frame(c); f++;
        if (i % 120 == 0) {
            NavPos q = ReadNav(c);
            std::printf("f=%d area=%u x=%u y=%u\n", f, q.area, q.x, q.y);
        }
    }

    NavPos p = ReadNav(c);
    std::printf("arrived area=%u x=%u y=%u in=%u pal=%02x\n", p.area, p.x, p.y, p.indoors, p.palace);

    /* The coordinates moved but the world did not: area stayed at the house.
     * Ask the overworld module to load the palace screen by name. */
    snes_write(c, 0x7E008Au, 2, 0x1E);   /* overworld_screen_index */
    snes_write(c, 0x7E0700u, 2, 0x1E);   /* current_area_of_player */
    for (int i = 0; i < 240; i++) snes_frame(c);
    NavPos r = ReadNav(c);
    std::printf("after screen set: area=%u x=%u y=%u in=%u pal=%02x\n",
                r.area, r.x, r.y, r.indoors, r.palace);

    /* Now walk, honestly, into the door. */
    bool entered = false;
    for (int i = 0; i < 900; i++) {
        NavPos q = ReadNav(c);
        if (q.indoors && q.palace != 0xFF) { entered = true; break; }
        snes_set_button(c, SNES_BTN_UP, true);
        snes_frame(c); f++;
    }
    snes_set_button(c, SNES_BTN_UP, false);
    NavPos end = ReadNav(c);
    std::printf("end area=%u x=%u y=%u in=%u pal=%02x dungeon-arrived=%s\n",
                end.area, end.x, end.y, end.indoors, end.palace, entered ? "YES" : "NO");
    if (stateout && entered) {
        bool ok = snes_save_state(c, stateout);
        std::printf("state=%s wrote=%d\n", stateout, ok ? 1 : 0);
    }

    snes_stop(c);
    snes_destroy(c);
    return entered ? 0 : 1;
}





/* ServoVertical: the precision half of the Eastern Palace climb.
 *
 * A 0x22 stair strip is exactly one body wide: moving vertically the
 * engine samples the tiles at link_x, link_x+8 and link_x+15, so the
 * strip only carries Link when his x puts all three samples on treads.
 * Rather than fight the steering deadband (+/-8 px), this servos x to
 * within 3 px of the strip's exact line with one-frame taps on open
 * ground, then holds a single vertical direction: inside the game's
 * bonk-snap capture window the engine nudges Link the last pixels onto
 * the line itself. Returns true when y_stop is reached (or, with
 * watch_indoors, when Link is inside the palace). */
static bool ServoVertical(SnesCore* c, int& f, int target_x, int y_stop,
                          bool north, bool watch_indoors, int stage_y)
{
    snes_set_button(c, SNES_BTN_UP, false);
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_LEFT, false);
    snes_set_button(c, SNES_BTN_RIGHT, false);
    snes_set_button(c, SNES_BTN_A, false);

    /* Phase 0: the staging waypoint can be consumed with Link already
     * beside the strip, where sideways taps hit the cliff face. Back
     * out to the open staging floor first. */
    for (int i = 0; i < 900; i++) {
        NavPos p = ReadNav(c);
        if (watch_indoors && p.indoors && p.palace != 0xFF) return true;
        if (north && (int) p.y >= stage_y) break;
        if (!north && (int) p.y <= stage_y) break;
        snes_set_button(c, north ? SNES_BTN_DOWN : SNES_BTN_UP, true);
        snes_frame(c); f++;
        snes_set_button(c, SNES_BTN_DOWN, false);
        snes_set_button(c, SNES_BTN_UP, false);
    }

    /* Phase 1: tap x onto the line. */
    for (int i = 0; i < 600; i++) {
        NavPos p = ReadNav(c);
        if (watch_indoors && p.indoors && p.palace != 0xFF) return true;
        int dx = (int) p.x - target_x;
        if (dx >= -3 && dx <= 3) break;
        if (dx < 0) snes_set_button(c, SNES_BTN_RIGHT, true);
        else snes_set_button(c, SNES_BTN_LEFT, true);
        snes_frame(c); f++;
        snes_set_button(c, SNES_BTN_RIGHT, false);
        snes_set_button(c, SNES_BTN_LEFT, false);
        snes_frame(c); f++;
    }

    /* Phase 2: hold the one direction; the engine self-aligns. */
    if (north) snes_set_button(c, SNES_BTN_UP, true);
    else snes_set_button(c, SNES_BTN_DOWN, true);
    NavPos p0 = ReadNav(c);
    int last_y = (int) p0.y, still = 0;
    bool ok = false;
    for (int i = 0; i < 3600; i++) {
        snes_frame(c); f++;
        NavPos p = ReadNav(c);
        if (watch_indoors && p.indoors && p.palace != 0xFF) { ok = true; break; }
        if (north && (int) p.y <= y_stop) { ok = true; break; }
        if (!north && (int) p.y >= y_stop) { ok = true; break; }
        if ((int) p.y == last_y) {
            if (++still > 900) break;   /* wedged */
        } else {
            still = 0;
            last_y = (int) p.y;
        }
        if ((i % 600) == 599) {
            std::printf("servo: x=%u y=%u target_x=%d y_stop=%d north=%d\n",
                        p.x, p.y, target_x, y_stop, (int) north);
        }
    }
    snes_set_button(c, SNES_BTN_UP, false);
    snes_set_button(c, SNES_BTN_DOWN, false);
    NavPos p = ReadNav(c);
    std::printf("servo-done ok=%d x=%u y=%u target_x=%d y_stop=%d north=%d in=%u pal=%02x room=%u\n",
                (int) ok, p.x, p.y, target_x, y_stop, (int) north,
                p.indoors, p.palace, p.room);
    return ok;
}


static int Dungeon(const char* rom, const char* savedir, const char* stateout, const char* shot)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    CloseMenu(c);

    /* Walk out of the house. */
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c); f++;
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 30; i++) { snes_frame(c); f++; }
    CloseMenu(c);

    /* Waypoint steering on the measured crossing points of the generated
     * overworld graph (scripts/gen-alttp-graph.py, gap runs with their
     * centre tiles): east out of the house screen, north up the river's
     * west bank (the channel is at x 2876), east across the land bridge,
     * north through the ruins screen.
     *
     * THE CLIFF CLIMB, SOLVED (2026-11-22, from the zelda3 decomp and the
     * ROM's own tile data): the palace plateau is reached by a staircase
     * of tile-attribute 0x22 ("indoor stairs" treads) at world x
     * 3216..3231, y 2256..2351. The treads carry NO wall bit (player.c's
     * TileHandlerIndoor_22 only records the stair tile), so Link walks
     * straight up them; the Phase A walk grid simply never admitted
     * 0x22 as walkable, and the diagonal one-subtile channel at x
     * 3120..3152 the old chain aimed at is the cliff face's visual edge -
     * a decoy no 16px body can traverse. The catch is alignment: moving
     * vertically, the engine samples tiles at link_x, link_x+8 and
     * link_x+15, so all three land on the 16px-wide strip only at
     * link_x == 3216 exactly. One pixel off, exactly one outer sample
     * touches cliff and the game's bonk-snap (player.c
     * HandlePushingBonkingSnaps_Y) nudges Link 1px per frame TOWARD the
     * strip instead of blocking, as long as the middle sample is free -
     * a capture window of x 3209..3223. ServoVertical does the precise
     * part: servo x onto the strip on the open floor, then hold one
     * direction and let the engine self-align.
     *
     * The palace door is the same trick a third time - and it is NOT
     * the grand door on the building's south face. That door (link_x
     * 3312..3319, link_y <= 1848) is entrance 69, which the game's
     * entrance table sends to room 261: Sahasrahla's house, palace:
     * None. (This walker proudly entered it twice before the table was
     * believed over the architecture.) The Eastern Palace is entrance
     * 8, in the north east of the ruins: link_x 3920..3927, link_y
     * <= 1576, at the top of a one-body channel (x 3920..3935) with a
     * 0x22 strip at y 1648..1672. The route goes around the east side:
     * across the forecourt, through the slope notch, down the east
     * stair strip (0x22 at x 3648..3663, y 2144..2200), east along
     * the lower terrace, north up the east channel, then east and
     * north into the door channel, where ServoVertical makes the
     * final aligned push. */
    static const int kWaypoints[][2] = {
        { 2876, 2948 },   /* east, then the bank channel */
        { 2876, 2708 },   /* north up the west bank */
        { 3188, 2708 },   /* east across the land bridge */
        { 3188, 2524 },   /* north into the palace screen's south cell */
        { 3216, 2440 },   /* north up the pillar avenue */
        { 3216, 2352 },   /* stair staging (servo climb follows) */
        { 3336, 2244 },   /* east along the terrace */
        { 3336, 1944 },   /* north up the channel into the forecourt */
        { 3452, 1944 },   /* east across the forecourt plaza */
        { 3504, 1952 },   /* east to the slope notch */
        { 3628, 1960 },   /* through the notch */
        { 3644, 2000 },   /* south-east out of the notch */
        { 3652, 2216 },   /* south down the east stair strip */
        { 3756, 2212 },   /* east along the lower terrace */
        { 3756, 1948 },   /* north up the east channel (a stair strip) */
        { 3776, 1924 },   /* north-east out of the channel mouth */
        { 3868, 1928 },   /* east along the north plaza */
        { 3868, 1796 },   /* north, up the last strip's side */
        { 3924, 1796 },   /* east along the open band to the door channel */
        { 3920, 1690 },   /* door staging (servo into the palace follows) */
    };
    const int kWaypointCount = (int) (sizeof(kWaypoints) / sizeof(kWaypoints[0]));
    int wp = 0;
    bool stair_done = false, door_done = false, channel_cleared = false;
    int stair_tries = 0;

    NavPos prev = ReadNav(c);
    std::printf("start area=%u x=%u y=%u in=%u pal=%02x\n",
                prev.area, prev.x, prev.y, prev.indoors, prev.palace);

    int stuck = 0, detour = 0, detour_dir = 0, detour_span = 90;
    int fight = 0;
    uint32_t last_hp = 0xA0;
    bool arrived = false;
    for (int i = 0; i < 60000; i++) {
        NavPos p = ReadNav(c);
        uint32_t mod = 0;
        snes_read(c, 0x7E0010u, 1, &mod);
        /* Arrival: indoors, with the palace byte set or the game in
         * the dungeon module - either signal accepted; the live proof
         * referees whether the speech names the Eastern Palace. */
        if (p.indoors && (p.palace != 0xFF || mod == 7)) { arrived = true; break; }
        if (mod == 18) {
            /* Module 18 is the death sequence, not yet the game-over
             * screen: the sequence scans the bottles for a fairy
             * (content 6) and diverts to the revival path (submodule
             * 10+), restoring 7 hearts where Link fell. The proof save
             * carries two fairies for exactly this. Wait the sequence
             * out with buttons released; only the save/continue menu
             * (submodule 9) means no fairy came. */
            snes_set_button(c, SNES_BTN_UP, false);
            snes_set_button(c, SNES_BTN_DOWN, false);
            snes_set_button(c, SNES_BTN_LEFT, false);
            snes_set_button(c, SNES_BTN_RIGHT, false);
            snes_set_button(c, SNES_BTN_A, false);
            bool revived = false;
            for (int k = 0; k < 1800; k++) {
                snes_frame(c); f++;
                uint32_t m2 = 0, sub = 0;
                snes_read(c, 0x7E0010u, 1, &m2);
                snes_read(c, 0x7E0011u, 1, &sub);
                if (m2 != 18) { revived = true; break; }
                if (sub == 9) break;   /* save/continue menu: real game over */
            }
            if (!revived) { std::printf("game-over at f=%d\n", f); break; }
            std::printf("fairy-revived at f=%d\n", f);
            prev = ReadNav(c); stuck = 0; detour = 0; fight = 0;
            last_hp = 0xA0;
            continue;
        }

        /* Precision segments (see the chain comment): when a staging
         * waypoint is consumed, hand control to ServoVertical for the
         * aligned push, then resume ordinary steering. */
        if (wp == 6 && !stair_done) {
            snes_set_button(c, SNES_BTN_A, false);
            if (ServoVertical(c, f, 3216, 2244, true, false, 2360)) {
                stair_done = true;
            } else if (++stair_tries >= 2) {
                NavPos q = ReadNav(c);
                std::printf("climb: stair servo failed twice at x=%u y=%u\n",
                            q.x, q.y);
                break;
            }
            prev = ReadNav(c); stuck = 0; detour = 0;
            continue;
        }
        if (wp == 14 && !channel_cleared) {
            channel_cleared = true;
            uint32_t hp_now = 0;
            snes_read(c, 0x7EF36Du, 1, &hp_now);
            if (hp_now < 150) {
                /* Something followed Link to the east channel's base,
                 * and the strip ascent is slow: kill it here on open
                 * ground, spinning, instead of being ground down on
                 * the stairs (one run died in that channel). */
                for (int k = 0; k < 720; k++) {
                    /* Pulse A: holding it only charges a spin that
                     * fires once on release; pulsing swings. */
                    snes_set_button(c, SNES_BTN_A, (k % 12) < 6);
                    snes_set_button(c, SNES_BTN_UP, ((k / 30) % 4) == 0);
                    snes_set_button(c, SNES_BTN_RIGHT, ((k / 30) % 4) == 1);
                    snes_set_button(c, SNES_BTN_DOWN, ((k / 30) % 4) == 2);
                    snes_set_button(c, SNES_BTN_LEFT, ((k / 30) % 4) == 3);
                    snes_frame(c); f++;
                }
                snes_set_button(c, SNES_BTN_UP, false);
                snes_set_button(c, SNES_BTN_RIGHT, false);
                snes_set_button(c, SNES_BTN_DOWN, false);
                snes_set_button(c, SNES_BTN_LEFT, false);
                snes_set_button(c, SNES_BTN_A, false);
                prev = ReadNav(c); stuck = 0; detour = 0;
                continue;
            }
        }
        if (wp >= kWaypointCount && !door_done) {
            snes_set_button(c, SNES_BTN_A, false);
            door_done = true;   /* one servo attempt; plain push backstops */
            ServoVertical(c, f, 3920, 0, true, true, 1690);
            prev = ReadNav(c); stuck = 0; detour = 0;
            continue;
        }
        /* A SWINGS THE SWORD; B is the Boomerang in this save. The first attempts
         * mashed B, which only stuns, and the guards killed Link. */
        snes_set_button(c, SNES_BTN_A, (i % 12) < 6);

        if (wp < kWaypointCount) {
            int dx = kWaypoints[wp][0] - (int) p.x;
            int dy = kWaypoints[wp][1] - (int) p.y;
            if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) < 28) wp++;
        }
        bool up = false, down = false, left = false, right = false;
        if (fight > 0) {
            /* Something is hitting Link and he is not moving: face each
             * direction in turn and keep swinging, so a guard standing
             * off-axis dies instead of grinding him down on the spot. */
            fight--;
            /* A stays on the steering pulse above: holding it would
             * charge a spin instead of swinging. */
            switch ((fight / 30) % 4) {
                case 0: up = true; break;
                case 1: right = true; break;
                case 2: down = true; break;
                default: left = true; break;
            }
        } else if (wp < kWaypointCount) {
            int dx = kWaypoints[wp][0] - (int) p.x;
            int dy = kWaypoints[wp][1] - (int) p.y;
            if (dx > 8) right = true; else if (dx < -8) left = true;
            if (dy > 8) down = true; else if (dy < -8) up = true;
            if (!up && !down && !left && !right) up = true;   /* at the door: push in */
        } else {
            up = true;
        }

        /* Wall slide: when the wanted direction is blocked, ADD the
         * perpendicular direction in growing sweeps instead of replacing the
         * wanted one. Random four-way detours turned the earlier runs into a
         * random walk. */
        if (detour > 0) {
            detour--;
            if (up || down) { if (detour_dir) right = true; else left = true; }
            else { if (detour_dir) up = true; else down = true; }
            if (detour == 0) detour_dir ^= 1;   /* next sweep goes the other way */
        }

        snes_set_button(c, SNES_BTN_UP, up);
        snes_set_button(c, SNES_BTN_DOWN, down);
        snes_set_button(c, SNES_BTN_LEFT, left);
        snes_set_button(c, SNES_BTN_RIGHT, right);
        snes_frame(c); f++;

        if ((i % 40) == 0) {
            uint32_t hp_now = 0;
            snes_read(c, 0x7EF36Du, 1, &hp_now);
            int moved = (int) (p.x > prev.x ? p.x - prev.x : prev.x - p.x) +
                        (int) (p.y > prev.y ? p.y - prev.y : prev.y - p.y);
            if (moved < 8 && hp_now < last_hp && fight == 0) fight = 240;
            last_hp = hp_now;
            if (moved < 8) {
                stuck++;
                if (stuck > 2 && detour == 0) {
                    detour = detour_span;   /* a sweep along the wall, growing */
                    if (detour_span < 720) detour_span *= 2;
                    stuck = 0;
                }
            } else {
                stuck = 0;
                detour_span = 90;
            }
            prev = p;
        }
        if ((i % 600) == 0) {
            uint32_t hp = 0, cap = 0, mmod = 0;
            snes_read(c, 0x7EF36Du, 1, &hp);
            snes_read(c, 0x7EF36Cu, 1, &cap);
            snes_read(c, 0x7E0010u, 1, &mmod);
            std::printf("f=%d area=%u x=%u y=%u in=%u pal=%02x room=%u wp=%d stuck=%d hp=%u/%u mod=%u\n",
                        f, p.area, p.x, p.y, p.indoors, p.palace, p.room, wp, stuck, hp, cap, mmod);
            if (shot && *shot) {
                char path[512];
                std::snprintf(path, sizeof(path), "%s.%d.ppm", shot, i / 600);
                WritePpm(c, path);
            }
        }
    }

    snes_set_button(c, SNES_BTN_UP, false);
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_LEFT, false);
    snes_set_button(c, SNES_BTN_RIGHT, false);

    NavPos end = ReadNav(c);
    std::printf("end area=%u x=%u y=%u in=%u pal=%02x room=%u\n",
                end.area, end.x, end.y, end.indoors, end.palace, end.room);
    if (shot && *shot) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s.final.ppm", shot);
        std::printf("ppm=%s wrote=%d\n", path, (int) WritePpm(c, path));
    }
    std::printf("dungeon-arrived=%s\n", arrived ? "YES" : "NO");
    bool saved = false;
    if (arrived && stateout && *stateout) saved = snes_save_state(c, stateout);
    std::printf("state=%s wrote=%d\n", stateout ? stateout : "", (int) saved);
    snes_stop(c);
    snes_destroy(c);
    return arrived ? 0 : 1;
}


/* ---- walkmap: the screen's own tile attributes ---------------------------
 * The planner needs to see a screen, not feel along its walls. The game keeps
 * the current screen's tile attribute ids at overworld_tileattr (0x7E2000),
 * addressed exactly as Overworld_GetTileAttributeAtLocation does:
 *   t = ((y - base_y) & mask_y) * 8 + ((x - base_x) & mask_x)
 *   attr = u16(0x7E2000 + (t >> 1) * 2)
 * This prints a coarse map of those ids so the walkable class can be read from
 * the picture instead of guessed.
 */
static int WalkMap(const char* rom, const char* savedir)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    CloseMenu(c);
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c); f++;
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 40; i++) { snes_frame(c); f++; }

    uint32_t by = 0, my = 0, bx = 0, mx = 0;
    snes_read(c, 0x7E0708u, 2, &by);
    snes_read(c, 0x7E070Au, 2, &my);
    snes_read(c, 0x7E070Cu, 2, &bx);
    snes_read(c, 0x7E070Eu, 2, &mx);
    NavPos p = ReadNav(c);
    uint32_t mod = 0, sub = 0, oms = 0;
    snes_read(c, 0x7E0010u, 1, &mod);
    snes_read(c, 0x7E0011u, 1, &sub);
    snes_read(c, 0x7E0200u, 1, &oms);
    std::printf("mod=%u sub=%u oms=%u area=%u x=%u y=%u base=%u,%u mask=%04x,%04x\n",
                mod, sub, oms, p.area, p.x, p.y, bx, by, mx, my);

    /* One character per 16x16 pixel cell (x in 8px units, y in pixels). */
    uint16_t ids[64]; char glyph[64]; int nids = 0;
    char rows[32][33];
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            /* Absolute world coords: the formula subtracts the base, so a
             * screen-relative coordinate would underflow through the mask. */
            uint32_t px = bx + (uint32_t) rx * 2, py = by + (uint32_t) ry * 16;
            uint32_t t = (((uint32_t) py - by) & my) * 8 + (((uint32_t) px - bx) & mx);
            uint32_t v = 0;
            snes_read(c, 0x7E2000u + ((t >> 1) * 2u), 2, &v);
            uint16_t id = (uint16_t) v;
            int k = 0;
            for (; k < nids; k++) if (ids[k] == id) break;
            if (k == nids && nids < 64) { ids[nids] = id; glyph[nids] = (char) ('A' + nids); nids++; }
            rows[ry][rx] = (k < nids) ? glyph[k] : '?';
        }
        rows[ry][32] = 0;
    }
    for (int ry = 0; ry < 32; ry++) std::printf("%s\n", rows[ry]);
    /* Machine-readable copy for scripts/alttp-walkability.py. */
    std::printf("attrs=");
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            int k = 0;
            for (; k < nids; k++) if (glyph[k] == rows[ry][rx]) break;
            std::printf("%s%04x", (ry || rx) ? "," : "", (unsigned) ids[k]);
        }
    }
    std::printf("\n");
    std::printf("legend:");
    for (int k = 0; k < nids; k++) std::printf(" %c=%04x", glyph[k], ids[k]);
    std::printf("\n");

    snes_stop(c);
    snes_destroy(c);
    return 0;
}


static int WalkMap2(const char* rom, const char* savedir)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    CloseMenu(c);
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c); f++;
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int rep = 0; rep < 3; rep++) {
        for (int i = 0; i < (rep ? 600 : 40); i++) { snes_frame(c); f++; }
        uint32_t by = 0, my = 0, bx = 0, mx = 0;
        snes_read(c, 0x7E0708u, 2, &by);
        snes_read(c, 0x7E070Au, 2, &my);
        snes_read(c, 0x7E070Cu, 2, &bx);
        snes_read(c, 0x7E070Eu, 2, &mx);
        int n4241 = 0;
        std::printf("rep=%d base=%u,%u mask=%04x,%04x attrs=", rep, bx, by, mx, my);
        for (int ry = 0; ry < 32; ry++) {
            for (int rx = 0; rx < 32; rx++) {
                uint32_t px = bx + (uint32_t) rx * 2, py = by + (uint32_t) ry * 16;
                uint32_t t = (((uint32_t) py - by) & my) * 8 + (((uint32_t) px - bx) & mx);
                uint32_t v = 0;
                snes_read(c, 0x7E2000u + ((t >> 1) * 2u), 2, &v);
                if (v == 0x4241) n4241++;
                std::printf("%s%04x", (ry || rx) ? "," : "", (unsigned) v);
            }
        }
        std::printf("\nn4241=%d\n", n4241);
    }
    snes_stop(c);
    snes_destroy(c);
    return 0;
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

    /* ⛔ THE SAVE IS BEHIND THE FILE SELECT. A bare START press reaches the title
     * and stops; the measured boot is the Menus() sequence, START plus A taps,
     * which walks title -> file select -> the loaded file. Measured 2026-10-17. */
    int f = 0;
    for (; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);

    for (int i = 0; i <= 600; i++) {
        if (i % 60 == 0) DumpReaderAddresses(c, i);
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

    /* ⛔ THE DRIVE LEAVES THE GAME IN THE INTERFACE MODULE (14). A START press
     * closes it and hands control back (measured: module 14 sub 1 -> 7 sub 0).
     * Without this the directions below move nothing. */
    for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
    snes_set_button(c, SNES_BTN_START, false);
    for (int i = 0; i < 30; i++) snes_frame(c);
    std::printf("-- after closing the interface --\n");
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

// Prints the game's decompressed message buffer, which the decompilation names
// messaging_text_buffer (g_ram+0x11200 -> 0x7F1200), plus the message index.
static int TextDump(const char* rom, const char* savedir)
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

    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }

    uint32_t msg = 0, pos = 0, mod = 0, sub = 0, real = 0;
    snes_read(c, 0x7E1CF0u, 2, &msg);
    snes_read(c, 0x7E1CF2u, 2, &pos);
    snes_read(c, 0x7E0010u, 1, &mod);
    snes_read(c, 0x7E0011u, 1, &sub);
    snes_read(c, 0x7E010Cu, 1, &real);
    std::printf("module=%u sub=%u real=%u message=%u read_pos=%u\n", mod, sub, real, msg, pos);

    std::printf("buffer:");
    for (int i = 0; i < 96; i++) {
        uint32_t v = 0;
        if (!snes_read(c, 0x7F1200u + (uint32_t) i, 1, &v)) break;
        std::printf(" %02x", v);
        if (i % 16 == 15) std::printf("\n       ");
    }
    std::printf("\n");

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// Does a button change the game state in the loaded save? Presses Start, then A,
// and prints the module after each, so a frozen game can be told from dead input.
static int KeyTest(const char* rom, const char* savedir)
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

    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    std::printf("-- at rest --\n"); DumpReaderAddresses(c, 0);

    for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
    snes_set_button(c, SNES_BTN_START, false);
    for (int i = 0; i < 30; i++) snes_frame(c);
    std::printf("-- after START --\n"); DumpReaderAddresses(c, 1);

    for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_A, true); snes_frame(c); }
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 30; i++) snes_frame(c);
    std::printf("-- after A --\n"); DumpReaderAddresses(c, 2);

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// ---- menus: measure the item menu cursor and the save/quit text ----
//
// Task 6 of docs/plans/alttp-reader.md. The item menu is module 14 submodule 1
// (the interface layer, entered with START); the save and quit box is module 14
// submodule 11 (entered with SELECT). Both are measured here before the adapter
// speaks a word about either.
static void DumpMenu(SnesCore* c, const char* label, int frame)
{
    uint32_t mod = 0, sub = 0, real = 0, oms = 0, hud = 0, choice = 0, msg = 0;
    uint32_t sword = 0, shield = 0, armor = 0;
    snes_read(c, 0x7E0010u, 1, &mod);
    snes_read(c, 0x7E0011u, 1, &sub);
    snes_read(c, 0x7E010Cu, 1, &real);
    snes_read(c, 0x7E0200u, 1, &oms);
    snes_read(c, 0x7E0202u, 1, &hud);
    snes_read(c, 0x7E1CE8u, 1, &choice);
    snes_read(c, 0x7E1CF0u, 2, &msg);
    snes_read(c, 0x7EF359u, 1, &sword);
    snes_read(c, 0x7EF35Au, 1, &shield);
    snes_read(c, 0x7EF35Bu, 1, &armor);

    std::printf("%s f=%d mod=%u sub=%u real=%u oms=%u hud_cur=%u choice=%u msg=%u sword=%u shield=%u armor=%u\n",
                label, frame, mod, sub, real, oms, hud, choice, msg, sword, shield, armor);

    std::printf("  items:");
    for (int i = 0; i < 24; i++) {
        uint32_t v = 0;
        snes_read(c, 0x7EF340u + (uint32_t) i, 1, &v);
        std::printf(" %02x", v);
    }
    std::printf("\n  text:");
    for (int i = 0; i < 48; i++) {
        uint32_t v = 0;
        snes_read(c, 0x7F1200u + (uint32_t) i, 1, &v);
        std::printf(" %02x", v);
    }
    std::printf("\n");
}

static void Tap(SnesCore* c, int btn, int hold, int settle, int* f)
{
    for (int i = 0; i < hold; i++, (*f)++) {
        snes_set_button(c, btn, true);
        snes_frame(c);
    }
    snes_set_button(c, btn, false);
    for (int i = 0; i < settle; i++, (*f)++) snes_frame(c);
}

static int Menus(const char* rom, const char* savedir)
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
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);

    /* The downloaded save loads with the interface overlay up; close it so the
     * menu presses below start from gameplay (measured: 14/1 -> 7/0). */
    Tap(c, SNES_BTN_START, 4, 40, &f);
    DumpMenu(c, "-- at rest --", f);

    /* Item menu: START. */
    Tap(c, SNES_BTN_START, 4, 60, &f);
    DumpMenu(c, "-- item menu opened --", f);
    Tap(c, SNES_BTN_RIGHT, 4, 30, &f);
    DumpMenu(c, "-- item menu right --", f);
    Tap(c, SNES_BTN_DOWN, 4, 30, &f);
    DumpMenu(c, "-- item menu down --", f);
    Tap(c, SNES_BTN_LEFT, 4, 30, &f);
    DumpMenu(c, "-- item menu left --", f);
    Tap(c, SNES_BTN_UP, 4, 30, &f);
    DumpMenu(c, "-- item menu up --", f);

    /* Close the item menu, then open the save/quit box: SELECT. */
    Tap(c, SNES_BTN_START, 4, 60, &f);
    DumpMenu(c, "-- item menu closed --", f);
    Tap(c, SNES_BTN_SELECT, 4, 90, &f);
    DumpMenu(c, "-- save menu opened --", f);
    Tap(c, SNES_BTN_DOWN, 4, 60, &f);
    DumpMenu(c, "-- save menu down --", f);
    Tap(c, SNES_BTN_UP, 4, 60, &f);
    DumpMenu(c, "-- save menu up --", f);
    Tap(c, SNES_BTN_B, 4, 90, &f);
    DumpMenu(c, "-- save menu cancelled --", f);

    snes_stop(c);
    snes_destroy(c);
    return 0;
}

// ---- file select: the cursor, the three slots, and a screenshot ----
//
// Task 5 of docs/plans/alttp-reader.md. The slots are not in work RAM: the game
// draws them straight out of the cartridge's save RAM, which the core exposes at
// 0x700000 (LoROM SRAM). One PPM is written when a path is given, because the
// name bytes are the game's own font indices and the screenshot is how the
// mapping gets measured rather than guessed.
static void DumpFileSelect(SnesCore* c, const char* label, int frame)
{
    uint32_t mod = 0, sub = 0, cursor = 0, arr1 = 0;
    snes_read(c, 0x7E0010u, 1, &mod);
    snes_read(c, 0x7E0011u, 1, &sub);
    snes_read(c, 0x7E00C8u, 1, &cursor);
    snes_read(c, 0x7E00BFu, 1, &arr1);
    std::printf("%s f=%d mod=%u sub=%u cursor=%u arr1=%02x\n", label, frame, mod, sub, cursor, arr1);

    for (int k = 0; k < 3; k++) {
        uint32_t base = 0x700000u + (uint32_t) k * 0x500u;
        uint32_t exists = 0, cap = 0, sword = 0, shield = 0, armor = 0;
        uint16_t name[6] = {0};
        snes_read(c, base + 0x3E5u, 2, &exists);
        snes_read(c, base + 0x36Cu, 1, &cap);
        snes_read(c, base + 0x359u, 1, &sword);
        snes_read(c, base + 0x35Au, 1, &shield);
        snes_read(c, base + 0x35Bu, 1, &armor);
        for (int i = 0; i < 6; i++) {
            uint32_t v = 0;
            if (snes_read(c, base + 0x3D9u + (uint32_t) i * 2u, 2, &v)) name[i] = (uint16_t) v;
        }
        std::printf("  slot %d exists=%04x name=%04x %04x %04x %04x %04x %04x health=%u sword=%u shield=%u armor=%u\n",
                    k, exists, name[0], name[1], name[2], name[3], name[4], name[5],
                    cap, sword, shield, armor);
    }
}

static bool WritePpm(SnesCore* c, const char* path)
{
    int w = 0, h = 0;
    if (!snes_framebuffer(c, &w, &h)) return false;
    const uint8_t* px = snes_framebuffer_ptr(c);
    if (!px || w <= 0 || h <= 0) return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
        std::fwrite(px + (size_t) i * 4, 1, 3, f);
    std::fclose(f);
    return true;
}

static int FileSelect(const char* rom, const char* savedir, const char* ppm)
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

    /* The intro runs for an unknown number of frames before the title. Press
     * START in short taps until the game itself says it is on module 1 (File
     * Select); the moment it does, stop pressing so no file loads. */
    int f = 0;
    while (f < 4000) {
        snes_set_button(c, SNES_BTN_START, (f % 120) < 4);
        if (!snes_frame(c)) break;
        f++;
        uint32_t mod = 0;
        if (snes_read(c, 0x7E0010u, 1, &mod) && mod == 1) break;
    }
    snes_set_button(c, SNES_BTN_START, false);
    for (int i = 0; i < 60; i++, f++) snes_frame(c);
    DumpFileSelect(c, "-- file select --", f);
    if (ppm && *ppm) std::printf("ppm=%s wrote=%d\n", ppm, (int) WritePpm(c, ppm));

    Tap(c, SNES_BTN_DOWN, 4, 30, &f);
    DumpFileSelect(c, "-- down --", f);
    Tap(c, SNES_BTN_DOWN, 4, 30, &f);
    DumpFileSelect(c, "-- down again --", f);
    Tap(c, SNES_BTN_UP, 4, 30, &f);
    DumpFileSelect(c, "-- up --", f);

    if (ppm && *ppm) {
        char second[512];
        std::snprintf(second, sizeof(second), "%s.2.ppm", ppm);
        std::printf("ppm2=%s wrote=%d\n", second, (int) WritePpm(c, second));
    }

    snes_stop(c);
    snes_destroy(c);
    return 0;
}



/* ---- walkdiag: why does LiveRegion refuse the real screen? ----
 * Boots into the save, walks out of the house exactly as the live
 * proof does, then prints the gate registers and replicates
 * LiveRegion's fill for the current screen: region count, Link's
 * tile, and the walkable map as text. */
static uint32_t DiagRead(SnesCore* c, uint32_t addr, int size)
{
    uint32_t v = 0;
    snes_read(c, addr, size, &v);
    return v;
}

static int WalkDiag(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    /* Close the interface overlay if it is up: two START taps. */
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
        snes_set_button(c, SNES_BTN_START, false);
        for (int i = 0; i < 60; i++) snes_frame(c);
    }
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 60; i++) snes_frame(c);
    /* The bench's step off the road. */
    for (int i = 0; i < 55; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    for (int i = 0; i < 30; i++) snes_frame(c);

    uint32_t mod = DiagRead(c, 0x7E0010, 1), saved = DiagRead(c, 0x7E010C, 1);
    uint32_t sub = DiagRead(c, 0x7E0011, 1), dlg = DiagRead(c, 0x7E1CF0, 2);
    uint32_t indoors = DiagRead(c, 0x7E001B, 1);
    uint32_t screenRaw = DiagRead(c, 0x7E008A, 2);
    uint32_t area = DiagRead(c, 0x7E040A, 2);
    uint32_t lx = DiagRead(c, 0x7E0022, 2), ly = DiagRead(c, 0x7E0020, 2);
    uint32_t health = DiagRead(c, 0x7EF36D, 1);
    std::printf("module=%u saved=%u sub=%u dialogue=%u indoors=%u screen=%u area=%u "
                "link=(%u,%u) health=%u\n",
                mod, saved, sub, dlg, indoors, screenRaw & 0x7F, area, lx, ly, health);
    uint32_t by = DiagRead(c, 0x7E0708, 2), my = DiagRead(c, 0x7E070A, 2);
    uint32_t bx = DiagRead(c, 0x7E070C, 2), mx = DiagRead(c, 0x7E070E, 2);
    std::printf("base_y=%u mask_y=%u base_x=%u mask_x=%u direct=%d\n",
                by, my, bx, mx, (int) (my == 0 || mx == 0));

    int screen = (int) (screenRaw & 0x7F);
    int cell = screen & 63;
    int cellX = (cell & 7) * 0x200, cellY = ((cell >> 3) & 7) * 0x200;
    static bool walk[1024];
    static bool steer[1024];
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            uint32_t idx;
            if (my == 0 || mx == 0) {
                idx = (uint32_t) (ry * 64 + rx);
            } else {
                int px8 = (cellX + rx * 16 + 8) / 8;
                int py = cellY + ry * 16 + 8;
                uint32_t t = (uint32_t) (((py - (int) by) & (int) my) * 8 +
                                         ((px8 - (int) bx) & (int) mx));
                idx = t >> 1;
            }
            uint32_t m16 = DiagRead(c, 0x7E2000 + idx * 2, 2);
            walk[ry * 32 + rx] = oga::AlttpMap16Walkable((uint16_t) m16);
            steer[ry * 32 + rx] = oga::AlttpMap16Steerable((uint16_t) m16);
        }
    }
    static int region[1024];
    static uint16_t stack[1024];
    for (int i = 0; i < 1024; i++) region[i] = -1;
    int next = 0;
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            int seed = ry * 32 + rx;
            if (region[seed] >= 0 || !walk[seed]) continue;
            int sp = 0;
            stack[sp++] = (uint16_t) seed;
            region[seed] = next;
            while (sp > 0) {
                int cur = stack[--sp];
                int cx = cur & 31, cy = cur >> 5;
                static const int kDx[4] = { 1, -1, 0, 0 };
                static const int kDy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; d++) {
                    int nx = cx + kDx[d], ny = cy + kDy[d];
                    if (nx < 0 || nx >= 32 || ny < 0 || ny >= 32) continue;
                    int ni = ny * 32 + nx;
                    if (region[ni] >= 0 || !walk[ni]) continue;
                    region[ni] = next;
                    stack[sp++] = (uint16_t) ni;
                }
            }
            next++;
        }
    }
    int tx = ((int) lx - cellX) / 16, ty = ((int) ly - cellY) / 16;
    std::printf("regions=%d expected=%u link-tile=(%d,%d) link-region=%d\n",
                next, oga::AlttpScreenRegionCount((uint8_t) screen), tx, ty,
                (tx >= 0 && tx < 32 && ty >= 0 && ty < 32) ? region[ty * 32 + tx] : -99);
    for (int ry = 0; ry < 32; ry++) {
        char line[33];
        for (int rx = 0; rx < 32; rx++)
            line[rx] = (rx == tx && ry == ty) ? 'L' : (walk[ry * 32 + rx] ? '#' : '.');
        line[32] = 0;
        std::printf("%s\n", line);
    }
    std::printf("steer grid (S = steerable):\n");
    for (int ry = 0; ry < 32; ry++) {
        char line[33];
        for (int rx = 0; rx < 32; rx++)
            line[rx] = (rx == tx && ry == ty) ? 'L' : (steer[ry * 32 + rx] ? 'S' : '.');
        line[32] = 0;
        std::printf("%s\n", line);
    }
    if (tx >= 0 && tx < 32 && ty >= 0 && ty < 32) {
        /* Link's steer component (start tile exempt, as the walker
         * treats it) and whether the west border goals are in it. */
        static bool seen[1024];
        for (int i = 0; i < 1024; i++) seen[i] = false;
        uint16_t st[1024];
        int sp = 0, comp = 0;
        st[sp++] = (uint16_t) (ty * 32 + tx);
        seen[ty * 32 + tx] = true;
        bool goalW = false;
        while (sp > 0) {
            int cur = st[--sp];
            comp++;
            int cx = cur & 31, cy = cur >> 5;
            if (cx <= 1 && cy <= 10) goalW = true;
            static const int kDx[4] = { 1, -1, 0, 0 };
            static const int kDy[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) {
                int nx = cx + kDx[d], ny = cy + kDy[d];
                if (nx < 0 || nx >= 32 || ny < 0 || ny >= 32) continue;
                int ni = ny * 32 + nx;
                if (seen[ni] || !steer[ni]) continue;
                seen[ni] = true;
                st[sp++] = (uint16_t) ni;
            }
        }
        std::printf("steer-component=%d west-goal-reachable=%d\n",
                    comp, (int) goalW);
        for (int third = 0; third < 3; third++) {
            bool reach = false;
            for (int ry = 0; ry < 32 && !reach; ry++) {
                if ((ry * 3) / 32 != third) continue;
                for (int rx = 0; rx <= 1; rx++)
                    if (seen[ry * 32 + rx]) reach = true;
            }
            std::printf("west-third-%d-reachable=%d\n", third, (int) reach);
        }
        for (int side = 0; side < 4; side++) {
            /* 0N 1S 2W 3E: which thirds of each border the component
             * touches at all (border row/col plus one inside). */
            for (int third = 0; third < 3; third++) {
                bool reach = false;
                for (int i = 0; i < 32 && !reach; i++) {
                    if ((i * 3) / 32 != third) continue;
                    int cells[2];
                    if (side == 0) { cells[0] = i; cells[1] = 32 + i; }
                    else if (side == 1) { cells[0] = 31 * 32 + i; cells[1] = 30 * 32 + i; }
                    else if (side == 2) { cells[0] = i * 32; cells[1] = i * 32 + 1; }
                    else { cells[0] = i * 32 + 31; cells[1] = i * 32 + 30; }
                    if (seen[cells[0]] || seen[cells[1]]) reach = true;
                }
                std::printf("side-%d-third-%d=%d\n", side, third, (int) reach);
            }
        }
    }
    snes_destroy(c);
    return 0;
}


/* ---- msgdiag: the registers while a message box is really open ----
 * A NEW file (empty save dir): the boot drive starts the game and
 * Zelda's telepathy message opens on its own. Dump the module,
 * submodule, saved module and message index at that moment, so the
 * walker's dialogue gate can use the game's real signal instead of
 * the message index (which the public save proves is stale at 388
 * with no box open). */
static int MsgDiag(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 600; i++) snes_frame(c);

    std::printf("module=%u saved=%u sub=%u dialogue=%u indoors=%u\n",
                DiagRead(c, 0x7E0010, 1), DiagRead(c, 0x7E010C, 1),
                DiagRead(c, 0x7E0011, 1), DiagRead(c, 0x7E1CF0, 2),
                DiagRead(c, 0x7E001B, 1));
    uint8_t buf[128];
    for (int i = 0; i < (int) sizeof(buf); i++)
        buf[i] = (uint8_t) DiagRead(c, 0x7F1200 + (uint32_t) i, 1);
    char text[192];
    int n = oga::AlttpDecodeText(buf, (int) sizeof(buf), text, (int) sizeof(text));
    std::printf("text=\"%s\"\n", n > 0 ? text : "");
    snes_destroy(c);
    return 0;
}


/* ---- deathdiag: where does the live-proof drive lose Link? ----
 * Mirrors snes-live-proof's --autowalk drive checkpoint by checkpoint
 * (boot, ONE overlay tap, walk out, a Guide press and its drain, then
 * idle), printing module/health/position at each one. */
static void DeathMark(SnesCore* c, const char* tag)
{
    std::printf("%s: module=%u saved=%u sub=%u health=%u link=(%u,%u) screen=%u\n",
                tag, DiagRead(c, 0x7E0010, 1), DiagRead(c, 0x7E010C, 1),
                DiagRead(c, 0x7E0011, 1), DiagRead(c, 0x7EF36D, 1),
                DiagRead(c, 0x7E0022, 2), DiagRead(c, 0x7E0020, 2),
                DiagRead(c, 0x7E008A, 2) & 0x7F);
}

static int DeathDiag(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    DeathMark(c, "boot");

    /* The live proof's overlay dance: tap, see the item menu, tap again. */
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
        snes_set_button(c, SNES_BTN_START, false);
        for (int i = 0; i < 60; i++) snes_frame(c);
        DeathMark(c, t == 0 ? "tap1" : "tap2");
    }

    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 60; i++) snes_frame(c);
    DeathMark(c, "out");

    for (int k = 0; k < 20; k++) {
        for (int i = 0; i < 150; i++) snes_frame(c);
        char tag[32];
        std::snprintf(tag, sizeof(tag), "idle%d", (k + 1) * 150);
        DeathMark(c, tag);
    }
    snes_destroy(c);
    return 0;
}


/* ---- spritedump: what body is standing at the field wedge? ----
 * Replicates the bench's approach (boot, walk out, cross west into
 * the next screen, climb into the field), then holds west along the
 * field line where the walker wedges, and dumps every sprite slot. */
static int SpriteDump(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
        snes_set_button(c, SNES_BTN_START, false);
        for (int i = 0; i < 60; i++) snes_frame(c);
    }
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 60; i++) snes_frame(c);

    /* West into screen 43, then up into the field, then west along
     * the wedge line, all closed-loop on Link's position. */
    auto pos = [&]() {
        return std::pair<int, int>((int) DiagRead(c, 0x7E0022, 2),
                                   (int) DiagRead(c, 0x7E0020, 2));
    };
    auto drive = [&](int tx, int ty, int ticks) {
        for (int k = 0; k < ticks; k++) {
            auto [x, y] = pos();
            if (std::abs(x - tx) <= 12 && std::abs(y - ty) <= 12) break;
            snes_set_button(c, SNES_BTN_LEFT, x > tx + 6);
            snes_set_button(c, SNES_BTN_RIGHT, x < tx - 6);
            snes_set_button(c, SNES_BTN_UP, y > ty + 6);
            snes_set_button(c, SNES_BTN_DOWN, y < ty - 6);
            for (int f = 0; f < 30; f++) snes_frame(c);
        }
        snes_set_button(c, SNES_BTN_LEFT, false);
        snes_set_button(c, SNES_BTN_RIGHT, false);
        snes_set_button(c, SNES_BTN_UP, false);
        snes_set_button(c, SNES_BTN_DOWN, false);
    };
    drive(2000, 2936, 30);          /* into screen 43 */
    drive(1870, 2845, 30);          /* the field hand-over spot */
    /* West along the wedge line, stopping the moment x stalls:
     * that is the wedge, and Link must be alive when we dump. */
    {
        int lx = -1, stall = 0;
        for (int k = 0; k < 60; k++) {
            auto [x, y] = pos();
            if (x == lx) stall++;
            else stall = 0;
            lx = x;
            if (stall >= 3) break;
            snes_set_button(c, SNES_BTN_LEFT, true);
            snes_set_button(c, SNES_BTN_DOWN, y < 2850);
            snes_set_button(c, SNES_BTN_UP, y > 2862);
            for (int f = 0; f < 30; f++) snes_frame(c);
        }
        snes_set_button(c, SNES_BTN_LEFT, false);
        snes_set_button(c, SNES_BTN_DOWN, false);
        snes_set_button(c, SNES_BTN_UP, false);
    }
    auto [lx, ly] = pos();
    std::printf("link=(%d,%d) health=%u\n", lx, ly, DiagRead(c, 0x7EF36D, 1));
    for (int s = 0; s < 16; s++) {
        int type = DiagRead(c, 0x7E0E20 + s, 1);
        int x = DiagRead(c, 0x7E0D00 + s, 1) | (DiagRead(c, 0x7E0D20 + s, 1) << 8);
        int y = DiagRead(c, 0x7E0D10 + s, 1) | (DiagRead(c, 0x7E0D30 + s, 1) << 8);
        int state = DiagRead(c, 0x7E0DD0 + s, 1);
        if (type || state)
            std::printf("sprite %d: type=0x%02x at=(%d,%d) state=%u\n",
                        s, type, x, y, state);
    }
    snes_destroy(c);
    return 0;
}


/* ---- guardmap: patrol bounding boxes on the castle approaches ----
 * Boots the save, walks out, and from three vantage points (the west
 * field, the doorstep, the road east) samples every sprite slot for
 * a few hundred frames each, printing each body's type and the
 * bounding box it patrols. Recon for the auto-walk bench's staging:
 * a hurt stop is only survivable where no patrol passes. */
static int GuardMap(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 4; i++) { snes_set_button(c, SNES_BTN_START, true); snes_frame(c); }
        snes_set_button(c, SNES_BTN_START, false);
        for (int i = 0; i < 60; i++) snes_frame(c);
    }
    for (int i = 0; i < 300; i++) {
        snes_set_button(c, SNES_BTN_DOWN, true);
        snes_set_button(c, SNES_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_DOWN, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 60; i++) snes_frame(c);

    auto pos = [&]() {
        return std::pair<int, int>((int) DiagRead(c, 0x7E0022, 2),
                                   (int) DiagRead(c, 0x7E0020, 2));
    };
    auto drive = [&](int tx, int ty, int ticks) {
        for (int k = 0; k < ticks; k++) {
            auto [x, y] = pos();
            if (std::abs(x - tx) <= 12 && std::abs(y - ty) <= 12) break;
            if ((int) DiagRead(c, 0x7EF36D, 1) <= 4) break;
            snes_set_button(c, SNES_BTN_LEFT, x > tx + 6);
            snes_set_button(c, SNES_BTN_RIGHT, x < tx - 6);
            snes_set_button(c, SNES_BTN_UP, y > ty + 6);
            snes_set_button(c, SNES_BTN_DOWN, y < ty - 6);
            for (int f = 0; f < 30; f++) snes_frame(c);
        }
        snes_set_button(c, SNES_BTN_LEFT, false);
        snes_set_button(c, SNES_BTN_RIGHT, false);
        snes_set_button(c, SNES_BTN_UP, false);
        snes_set_button(c, SNES_BTN_DOWN, false);
    };
    auto sample = [&](const char* tag, int samples) {
        int mnx[16], mxx[16], mny[16], mxy[16], typ[16];
        for (int s = 0; s < 16; s++) { mnx[s] = 1 << 28; mxx[s] = -1; mny[s] = 1 << 28; mxy[s] = -1; typ[s] = 0; }
        for (int k = 0; k < samples; k++) {
            for (int f = 0; f < 60; f++) snes_frame(c);
            for (int s = 0; s < 16; s++) {
                int type = DiagRead(c, 0x7E0E20 + s, 1);
                int state = DiagRead(c, 0x7E0DD0 + s, 1);
                if (!type && !state) continue;
                int x = DiagRead(c, 0x7E0D00 + s, 1) | (DiagRead(c, 0x7E0D20 + s, 1) << 8);
                int y = DiagRead(c, 0x7E0D10 + s, 1) | (DiagRead(c, 0x7E0D30 + s, 1) << 8);
                typ[s] = type;
                if (x < mnx[s]) mnx[s] = x;
                if (x > mxx[s]) mxx[s] = x;
                if (y < mny[s]) mny[s] = y;
                if (y > mxy[s]) mxy[s] = y;
            }
        }
        auto [lx, ly] = pos();
        std::printf("%s: link=(%d,%d) health=%d\n", tag, lx, ly,
                    (int) DiagRead(c, 0x7EF36D, 1));
        for (int s = 0; s < 16; s++)
            if (mxx[s] >= 0)
                std::printf("  slot %d type=0x%02x x=[%d..%d] y=[%d..%d]\n",
                            s, typ[s], mnx[s], mxx[s], mny[s], mxy[s]);
    };

    drive(2000, 2936, 30);
    drive(1870, 2845, 30);
    sample("west-field", 8);
    drive(2256, 2944, 40);
    sample("doorstep", 5);
    drive(2750, 2943, 40);
    sample("road-east", 8);
    snes_destroy(c);
    return 0;
}

/* ---- walklease: the direction lease and player arbitration, measured ----
 * The auto-walk core contract, measured through the provider bits the
 * console actually consumed: a direction refreshed every frame persists;
 * one unrefreshed write dies within the 3-frame lease bound (the sampled
 * count is 2 or 3 depending on where in the in-flight frame the write
 * lands — the in-flight frame's own aging can consume one count before
 * its poll); an explicit release clears it at once; and a real player
 * button press clears the direction layer on the frame it lands while
 * the player's own bit still reads. */
static int WalkLease(const char* rom, const char* savedir)
{
    SnesCore* c = snes_create();
    if (!c) { std::printf("FAIL: no core\n"); return 1; }
    char code[16] = {0};
    if (!snes_load_rom(c, rom, savedir, code) || !snes_start(c)) {
        std::printf("FAIL: %s\n", snes_last_error(c));
        snes_destroy(c);
        return 1;
    }
    for (int f = 0; f < 1500; f++) {
        snes_set_button(c, SNES_BTN_START, (f % 90) < 4);
        snes_set_button(c, SNES_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        snes_frame(c);
    }
    snes_set_button(c, SNES_BTN_START, false);
    snes_set_button(c, SNES_BTN_A, false);
    for (int i = 0; i < 30; i++) snes_frame(c);

    /* Refreshed every frame, the direction persists at every sample. */
    bool hold = true;
    for (int i = 0; i < 60; i++) {
        snes_set_direction(c, 0, true);
        snes_frame(c);
        if (!(snes_provider_bits(c) & (1u << SNES_BTN_UP))) hold = false;
    }
    std::printf("hold-persists=%s\n", hold ? "YES" : "NO");

    /* One write, never refreshed: sampled for at most the lease bound. */
    snes_set_direction(c, 0, true);
    int lease = 0;
    for (int i = 0; i < 10; i++) {
        snes_frame(c);
        if (snes_provider_bits(c) & (1u << SNES_BTN_UP)) lease++;
    }
    std::printf("lease-frames=%d\n", lease);
    bool bounded = lease >= 1 && lease <= 3;
    std::printf("lease-bounded=%s\n", bounded ? "YES" : "NO");

    /* An explicit release clears the layer at once. */
    for (int i = 0; i < 10; i++) { snes_set_direction(c, 0, true); snes_frame(c); }
    snes_set_direction(c, 0, false);
    snes_frame(c);
    bool released = !(snes_provider_bits(c) & (1u << SNES_BTN_UP));
    std::printf("release-clears=%s\n", released ? "YES" : "NO");

    /* A player press arbitrates instantly; the player's bit still reads. */
    for (int i = 0; i < 10; i++) { snes_set_direction(c, 0, true); snes_frame(c); }
    bool wasHeld = (snes_provider_bits(c) & (1u << SNES_BTN_UP)) != 0;
    snes_set_button(c, SNES_BTN_B, true);
    snes_frame(c);
    unsigned bits = snes_provider_bits(c);
    bool arb = wasHeld && !(bits & (1u << SNES_BTN_UP)) && (bits & (1u << SNES_BTN_B));
    snes_set_button(c, SNES_BTN_B, false);
    std::printf("arbitration=%s\n", arb ? "YES" : "NO");

    bool ok = hold && bounded && released && arb;
    std::printf("walklease=%s\n", ok ? "YES" : "NO");
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
    if (std::strcmp(argv[1], "--resolve") == 0) return Resolve();
    if (std::strcmp(argv[1], "--headered") == 0) {
        if (argc < 5) { Usage(argv[0]); return 2; }
        return Headered(argv[2], argv[3], argv[4]);
    }
    if (std::strcmp(argv[1], "--keytest") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return KeyTest(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--text") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return TextDump(argv[2], argv[3]);
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
    if (std::strcmp(argv[1], "--deathdiag") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return DeathDiag(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--msgdiag") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return MsgDiag(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--guardmap") == 0) {
        if (argc < 4) { std::printf("usage: --guardmap ROM SAVEDIR\n"); return 2; }
        return GuardMap(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--spritedump") == 0) {
        if (argc < 4) { std::printf("usage: --spritedump ROM SAVEDIR\n"); return 2; }
        return SpriteDump(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--walkdiag") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return WalkDiag(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--walklease") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return WalkLease(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--drive") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Drive(argv[2], argv[3], argc > 4 ? std::atoi(argv[4]) : 1800);
    }
    if (std::strcmp(argv[1], "--walkmap2") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return WalkMap2(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--walkmap") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return WalkMap(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--teleport") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Teleport(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    }
    if (std::strcmp(argv[1], "--dungeon") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Dungeon(argv[2], argv[3], argc > 4 ? argv[4] : nullptr,
                       argc > 5 ? argv[5] : nullptr);
    }
    if (std::strcmp(argv[1], "--talk") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Talk(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--memory") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Memory(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--after-stop") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return AfterStop(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--menus") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Menus(argv[2], argv[3]);
    }
    if (std::strcmp(argv[1], "--file") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return FileSelect(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    }
    if (std::strcmp(argv[1], "--states") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return States(argv[2], argv[3]);
    }

    Usage(argv[0]);
    return 2;
}
