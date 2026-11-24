// n64-host-proof-main.cpp — drives Core/n64_core.cpp (the app's real N64 glue)
// against the synthetic cartridge from scripts/n64-smoke-rom.py. Built and run
// by scripts/n64-host-proof.sh; every CHECK names what it proves.
#include "n64_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int gFail = 0;
#define CHECK(cond, what) do { \
    if (cond) printf("ok: %s\n", what); \
    else { printf("FAIL: %s\n", what); gFail++; } } while (0)

static uint32_t Read(N64Core *c, uint32_t addr, int width)
{
    uint32_t v = 0xDEADBEEF;
    if (!n64_read(c, addr, width, &v)) return 0xDEADBEEF;
    return v;
}

static bool FileSize(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fclose(f);
    return true;
}

// One full boot of `rom`, returning false on a fatal step so a second boot can
// still be attempted (the re-init check).
static bool Boot(const char *rom, const char *save, const char *state, bool full)
{
    N64Core *c = n64_create();
    CHECK(c != nullptr, "n64_create returns a core");
    if (!c) return false;
    CHECK(n64_create() == nullptr, "a second live core is refused (the core is a singleton)");

    char code[16] = {0};
    bool loaded = n64_load_rom(c, rom, save, code);
    if (!loaded) printf("   load error: %s\n", n64_last_error(c));
    CHECK(loaded, "n64_load_rom loads the synthetic cartridge");
    if (!loaded) { n64_destroy(c); return false; }
    CHECK(strcmp(code, "n64:NOGE") == 0, "the game code is n64: plus the header's cartridge ID (n64:NOGE)");
    CHECK(n64_start(c), "n64_start");

    int videoFrames = 0;
    for (int i = 0; i < 120; i++) {
        if (!n64_frame(c)) break;
        int w = 0, h = 0;
        if (n64_framebuffer(c, &w, &h)) videoFrames++;
    }
    CHECK(n64_frames_completed(c) == 120, "120 frames ran (retro_run returns once per VI)");
    CHECK(videoFrames > 0, "the software RDP path delivered video frames (angrylion, no GL)");

    int w = 0, h = 0;
    bool fb = n64_framebuffer(c, &w, &h);
    const uint8_t *px = n64_framebuffer_ptr(c);
    CHECK(fb && px && w >= 320 && h >= 200, "the framebuffer has the VI's size");
    if (fb && px) {
        // The ROM fills the whole picture with 0xFF8000; find it in the middle.
        const uint8_t *mid = px + ((size_t) (h / 2) * w + w / 2) * 4;
        printf("   framebuffer %dx%d, centre pixel %02x%02x%02x%02x\n", w, h, mid[0], mid[1], mid[2], mid[3]);
        CHECK(mid[0] == 0xFF && mid[1] == 0x80 && mid[2] == 0x00 && mid[3] == 0xFF,
              "the centre pixel is the ROM's orange, converted XRGB8888 -> RGBA8888");
    }

    // ---- RDRAM through the adapter read path, in the CPU's (big-endian) view ----
    CHECK(Read(c, 0x80000400, 4) == 0x4F474136, "word at KSEG0 0x80000400 reads the ROM's marker 0x4F474136");
    CHECK(Read(c, 0xA0000400, 4) == 0x4F474136, "the KSEG1 mirror reads the same word");
    CHECK(Read(c, 0x00000400, 4) == 0x4F474136, "the physical address reads the same word");
    CHECK(Read(c, 0x80000400, 1) == 0x4F, "byte 0x80000400 is the word's MOST significant byte (big-endian CPU view)");
    CHECK(Read(c, 0x80000403, 1) == 0x36, "byte 0x80000403 is its least significant byte");
    CHECK(Read(c, 0x80000408, 2) == 0x1234, "halfword 0x80000408 reads 0x1234");
    CHECK(Read(c, 0x8000040A, 2) == 0x5678, "halfword 0x8000040A reads 0x5678");
    CHECK(Read(c, 0x80000409, 2) == 0x3456, "an unaligned halfword composes across the swizzle (0x3456)");
    uint32_t dummy;
    CHECK(!n64_read(c, 0x80800000, 4, &dummy), "a read past the 8 MiB RDRAM is refused, not zero");
    CHECK(!n64_read(c, 0xA4000000, 4, &dummy), "a read of SP memory (not RDRAM) is refused");
    CHECK(!n64_read(c, 0x80000400, 3, &dummy), "width 3 is refused");

    uint32_t c1 = Read(c, 0x80000404, 4);
    for (int i = 0; i < 30; i++) n64_frame(c);
    uint32_t c2 = Read(c, 0x80000404, 4);
    printf("   run-loop counter %u -> %u\n", c1, c2);
    CHECK(c2 > c1 && c1 != 0xDEADBEEF, "the CPU keeps running: the ROM's counter advanced over 30 frames");

    // ---- audio ----
    printf("   audio buffer starts %08x %08x\n", Read(c, 0x80200000, 4), Read(c, 0x80200100, 4));
    CHECK(Read(c, 0x80200000, 4) == 0x40004000, "the ROM wrote its square wave to RDRAM (the AI's source)");
    std::vector<int16_t> buf(32768 * 2);
    int got = n64_read_audio(c, buf.data(), 32768);
    int nonzero = 0, positive = 0, negative = 0;
    for (int i = 0; i < got * 2; i++) {
        if (buf[i] != 0) nonzero++;
        if (buf[i] > 0x1000) positive++;
        if (buf[i] < -0x1000) negative++;
    }
    printf("   audio: %d frames drained (150 frames of emulation), %d nonzero samples\n", got, nonzero);
    // 150 VI frames at 60 Hz is 2.5 s; at 32768 Hz that is ~81920 frames, capped
    // here by the 32768-frame read and the core's own one-second backlog bound.
    CHECK(got > 8000, "audio arrives at the app's rate (thousands of frames, resampled to 32768 Hz)");
    CHECK(positive > 100 && negative > 100, "the ROM's square wave is in it (both levels present)");

    if (full) {
        // ---- savestate round trip ----
        CHECK(n64_save_state(c, state), "n64_save_state writes a state");
        long ss = 0;
        CHECK(FileSize(state, &ss) && ss > 1000000, "the state file has the core's full size (> 1 MB)");
        uint32_t atSave = Read(c, 0x80000404, 4);
        for (int i = 0; i < 30; i++) n64_frame(c);
        uint32_t later = Read(c, 0x80000404, 4);
        CHECK(later > atSave, "the counter moves on after the save");
        CHECK(n64_load_state(c, state), "n64_load_state accepts it");
        uint32_t restored = Read(c, 0x80000404, 4);
        printf("   counter at save %u, later %u, after load %u\n", atSave, later, restored);
        CHECK(restored < later && restored >= atSave && restored - atSave < 1000000,
              "loading the state rewound the counter to the save point");
        std::vector<uint8_t> junk(64, 0xAB);
        FILE *f = fopen((std::string(state) + ".bad").c_str(), "wb");
        if (f) { fwrite(junk.data(), 1, junk.size(), f); fclose(f); }
        CHECK(!n64_load_state(c, (std::string(state) + ".bad").c_str()), "a junk state file is refused");

        // A state of the right size and magic but an unknown version: the core
        // itself refuses it. Its refusal must reach the caller (retro_unserialize
        // says true regardless), and the core must still run afterwards --
        // the libretro build has a lock that some refusals never release.
        std::vector<uint8_t> real;
        {
            FILE *in = fopen(state, "rb");
            if (in) { real.resize((size_t) ss); size_t n = fread(real.data(), 1, real.size(), in); real.resize(n); fclose(in); }
        }
        if (real.size() > 12) {
            real[8] ^= 0x7F;   // the version word follows the 8-byte magic
            FILE *o = fopen((std::string(state) + ".ver").c_str(), "wb");
            if (o) { fwrite(real.data(), 1, real.size(), o); fclose(o); }
            CHECK(!n64_load_state(c, (std::string(state) + ".ver").c_str()),
                  "a state with a foreign version is refused, and the refusal is reported");
            unsigned long long before = n64_frames_completed(c);
            for (int i = 0; i < 5; i++) n64_frame(c);
            CHECK(n64_frames_completed(c) == before + 5, "the core still runs after a refused state (no stuck lock)");
            CHECK(n64_save_state(c, state) && n64_load_state(c, state), "and a real state still saves and loads after it");
        }
    }

    n64_stop(c);
    CHECK(!n64_frame(c), "no frames run after n64_stop");
    if (full) {
        // The ROM copied its markers into SRAM by PI DMA. Find them in the save.
        std::vector<uint8_t> sv;
        FILE *f = fopen(save, "rb");
        if (f) { uint8_t b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) sv.insert(sv.end(), b, b + n); fclose(f); }
        bool found = false;
        static const uint8_t be[4] = {0x4F, 0x47, 0x41, 0x36}, le[4] = {0x36, 0x41, 0x47, 0x4F};
        for (size_t i = 0; i + 4 <= sv.size() && !found; i++)
            found = memcmp(&sv[i], be, 4) == 0 || memcmp(&sv[i], le, 4) == 0;
        printf("   battery save: %zu bytes\n", sv.size());
        CHECK(!sv.empty() && found, "n64_stop wrote the battery save, holding what the game put in SRAM");
    }
    n64_destroy(c);
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s <rom.z64> <rom.v64> <save.srm> <state>\n", argv[0]); return 2; }
    const char *rom = argv[1], *romV64 = argv[2], *save = argv[3], *state = argv[4];

    printf("== boot 1: z64 byte order, full checks\n");
    Boot(rom, save, state, true);
    printf("   core: %s\n", n64_core_version());

    // ⛔ THE SECOND BOOT IS THE POINT. pokecore tears a backend down between
    // games and creates a fresh one; a core that only works once in a process
    // would play the first game and black-screen the second.
    printf("== boot 2: a fresh core in the same process, v64 byte order\n");
    Boot(romV64, save, state, false);

    // A file that is not a cartridge is refused in words.
    {
        N64Core *c = n64_create();
        char code[16];
        bool ok = n64_load_rom(c, argv[0], nullptr, code);
        CHECK(!ok, "a file that is not an N64 ROM (this program) is refused");
        CHECK(strstr(n64_last_error(c), "Nintendo 64") != nullptr, "...with a reason that says so");
        n64_destroy(c);
    }

    if (gFail) { printf("FAILED: %d check(s)\n", gFail); return 1; }
    printf("PASS: N64 host proof\n");
    return 0;
}
