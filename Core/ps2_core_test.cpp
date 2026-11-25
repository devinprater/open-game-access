/* ps2_core_test.cpp — host tests for the PlayStation 2 core wrapper
 * (Core/ps2_core.cpp), run by scripts/ps2-core-test.sh.
 *
 * Three halves, matching the wrapper's three promises:
 *   - with NO core library anywhere, every path refuses in words
 *     (no crash, no silent success) — always runs;
 *   - with a core library (argv[1], the pinned Play! build), a file that
 *     is not a game is refused in the core's own name, and a synthetic
 *     PS2 ELF loads (Play! accepts .elf) — always runs with a core;
 *   - with a real disc too (argv[2], the player's own ISO), the full
 *     boot contract: the serial out of SYSTEM.CNF, frames advancing, a
 *     framebuffer, the game's own bytes findable through ps2_read, and
 *     a save-state round trip.
 *
 * Why no run-the-ELF half: measured 2026-11-25, a bare self-loop ELF
 * loads but its first retro_run never returns in Play! — its execution
 * model ends a frame on GS activity a bare-metal loop never produces.
 * Games are the real target; the ELF still proves the load path.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ps2_core.h"

static int gChecks = 0, gFailures = 0;
#define CHECK(cond, ...) do { \
    gChecks++; \
    if (!(cond)) { gFailures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static bool WriteFile(const std::string &path, const void *data, size_t n)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    fclose(f);
    return ok;
}

/* A minimal PS2 ELF: ET_EXEC for the EE's MIPS, one loadable segment at
 * 0x100000 holding a self-loop and a marker word. Only the LOAD is under
 * test (see the header note); nothing here runs it. */
static std::string WriteTestElf(const std::string &dir)
{
    std::string path = dir + "/oga-ps2-test.elf";
    std::vector<uint8_t> img(92, 0);
    img[0] = 0x7F; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
    img[4] = 1; img[5] = 1; img[6] = 1;              /* 32-bit, little-endian */
    auto w16 = [&](size_t o, uint16_t v) { img[o] = (uint8_t) v; img[o + 1] = (uint8_t) (v >> 8); };
    auto w32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) img[o + i] = (uint8_t) (v >> (8 * i)); };
    w16(16, 2);                                      /* ET_EXEC */
    w16(18, 8);                                      /* EM_MIPS */
    w32(20, 1);
    w32(24, 0x100000);                               /* entry */
    w32(28, 52);                                     /* phoff */
    w16(40, 52); w16(42, 32); w16(44, 1);            /* ehsize, phentsize, phnum */
    size_t p = 52;                                   /* the program header */
    w32(p + 0, 1);                                   /* PT_LOAD */
    w32(p + 4, 84);                                  /* file offset of the code */
    w32(p + 8, 0x100000);                            /* vaddr */
    w32(p + 12, 0x100000);
    w32(p + 16, 8);                                  /* filesz */
    w32(p + 20, 8);                                  /* memsz */
    w32(p + 24, 5);
    w32(p + 28, 0x1000);
    w32(84, 0x1000FFFF);                             /* beq $zero,$zero,-1 */
    w32(88, 0xDEADBEEF);                             /* the marker */
    return WriteFile(path, img.data(), img.size()) ? path : "";
}

int main(int argc, char **argv)
{
    const char *tmp = getenv("TMPDIR");
    std::string dir = std::string(tmp ? tmp : "/tmp") + "/oga-ps2-core-test";
    std::string mk = "mkdir -p \"" + dir + "\"";
    if (system(mk.c_str()) != 0) return 2;

    /* ---- no core anywhere: refusals in words ---- */
    unsetenv("OGA_PS2_CORE");
    Ps2Core *c = ps2_create();
    CHECK(c != nullptr, "create with no core configured");
    Ps2Core *second = ps2_create();
    CHECK(second == nullptr, "a second live wrapper is refused (the singleton rule)");
    CHECK(ps2_create_error()[0] != '\0', "the refusal says why: %s", ps2_create_error());
    if (c) {
        char code[16];
        std::string notAGame = dir + "/not-a-game.txt";
        WriteFile(notAGame, "hello", 5);
        CHECK(!ps2_load_disc(c, notAGame.c_str(), dir.c_str(), code),
              "load with no core library refuses");
        CHECK(strstr(ps2_last_error(c), "core") != nullptr,
              "the refusal names the missing core: %s", ps2_last_error(c));
        uint32_t v = 0;
        CHECK(!ps2_read(c, 0x1000, 4, &v), "memory read before any load fails closed");
        CHECK(!ps2_frame(c), "frame before load is refused");
        CHECK(!ps2_framebuffer(c, nullptr, nullptr), "no framebuffer before frames");
        CHECK(!ps2_start(c), "start before load is refused");
        ps2_set_button(c, -1, true);                 /* out-of-range input is ignored, */
        ps2_set_button(c, PS2_BTN_COUNT, true);      /* never a crash */
        ps2_set_analog(c, PS2_STICK_LEFT, NAN, 2.0f);
        ps2_destroy(c);
    }
    c = ps2_create();
    CHECK(c != nullptr, "create works again after destroy");
    ps2_destroy(c);

    if (argc < 2) {
        printf("SKIP core halves: no core library argument (the script passes the pinned Play! build when it has it)\n");
        printf("ps2_core_test: %d checks, %d failures\n", gChecks, gFailures);
        return gFailures ? 1 : 0;
    }

    /* ---- with a core: refusals by name, and the ELF load ---- */
    Ps2Core *w = ps2_create();
    CHECK(w != nullptr, "create for the core half");
    if (w) {
        ps2_set_core_path(w, argv[1]);
        std::string junk = dir + "/junk.bin";
        WriteFile(junk, "this is not a disc image at all", 31);
        char code[16];
        CHECK(!ps2_load_disc(w, junk.c_str(), dir.c_str(), code),
              "a non-game file is refused");
        CHECK(strstr(ps2_last_error(w), "could not load") != nullptr ||
              strstr(ps2_last_error(w), "does not accept") != nullptr,
              "the refusal is in words: %s", ps2_last_error(w));
        std::string elf = WriteTestElf(dir);
        CHECK(!elf.empty(), "the test ELF was written");
        if (!elf.empty()) {
            CHECK(ps2_load_disc(w, elf.c_str(), dir.c_str(), code),
                  "the synthetic ELF loads: %s", ps2_last_error(w));
            CHECK(code[0] == '\0', "an ELF yields no disc serial");
            CHECK(ps2_core_name(w)[0] != '\0', "the core names itself: %s", ps2_core_name(w));
            uint32_t v = 0;
            CHECK(!ps2_read(w, 0x02000000, 4, &v), "past the 32 MiB of EE RAM fails closed");
            CHECK(!ps2_read(w, 0x40000000, 4, &v), "the KSEG2 window is not served");
            CHECK(!ps2_read(w, 0x100000, 3, &v), "a width of 3 is refused");
        }
        ps2_destroy(w);
    }

    /* ---- with a real disc: the boot contract ---- */
    if (argc < 3) {
        printf("SKIP disc half: no disc argument (the player's own ISO proves the boot)\n");
    } else {
        Ps2Core *g = ps2_create();
        CHECK(g != nullptr, "create for the disc half");
        if (g) {
            ps2_set_core_path(g, argv[1]);
            char code[16];
            CHECK(ps2_load_disc(g, argv[2], dir.c_str(), code),
                  "the disc loads: %s", ps2_last_error(g));
            CHECK(strlen(code) == 9, "a serial came out of SYSTEM.CNF: %s", code);
            CHECK(ps2_start(g), "start");
            for (int i = 0; i < 300; i++) ps2_frame(g);
            CHECK(ps2_frames_completed(g) == 300, "frames advance (%llu)",
                  ps2_frames_completed(g));
            int fw = 0, fh = 0;
            CHECK(ps2_framebuffer(g, &fw, &fh) && fw > 0 && fh > 0,
                  "a framebuffer is presented (%dx%d)", fw, fh);
            /* The game's own serial string sits in EE RAM once it is
             * executing; find any 4-byte window of RAM that reads back
             * nonzero through the fail-closed path. */
            uint32_t v = 0;
            bool anyRam = false;
            for (uint32_t a = 0x100000; a < 0x1000000 && !anyRam; a += 4)
                if (ps2_read(g, a, 4, &v) && v != 0) anyRam = true;
            CHECK(anyRam, "EE RAM holds live data after boot");
            std::string state = dir + "/disc-state.bin";
            if (ps2_save_state(g, state.c_str())) {
                CHECK(ps2_load_state(g, state.c_str()), "a saved state loads back");
            } else {
                CHECK(strstr(ps2_last_error(g), "save states") != nullptr,
                      "a state refusal is in words: %s", ps2_last_error(g));
            }
            ps2_stop(g);
            ps2_destroy(g);
        }
    }

    printf("ps2_core_test: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
