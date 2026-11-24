/* ps1_core_test.cpp — the PlayStation core, booted with no game files.
 *
 * Built and run by scripts/ps1-core-test.sh against the pinned PCSX ReARMed.
 * No disc and no BIOS ship with the repo, so the test WRITES its own program: a
 * 2 KiB PS-EXE header and nine MIPS instructions that store a marker word into
 * RAM and then count up in the word after it, forever. The HLE BIOS boots it.
 * That proves, end to end and in-process: the core links under its oga_ps1_
 * prefix, loads, runs frames, produces video and audio, reads RAM through every
 * mirror the adapters will use, refuses what it must, saves and restores a
 * state, and can be torn down and started again (the singleton lifecycle).
 */
#include "ps1_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int gFails = 0;
static int gChecks = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        gChecks++;                                                             \
        if (!(cond)) { gFails++; fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static const uint32_t kMarker = 0x4F474131u;   /* "1AGO" little-endian: 'O' 'G' 'A' '1' */

static void Put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24);
}

/* The PS-EXE: header (2048 bytes) then code loaded at 0x80010000. */
static bool WriteExe(const std::string &path)
{
    static const uint32_t code[] = {
        0x3C088010u,   /* lui   t0, 0x8010            t0 = 0x80100000          */
        0x3C094F47u,   /* lui   t1, 0x4F47                                     */
        0x35294131u,   /* ori   t1, t1, 0x4131        t1 = marker              */
        0xAD090000u,   /* sw    t1, 0(t0)                                      */
        0x8D0A0004u,   /* loop: lw t2, 4(t0)                                   */
        0x254A0001u,   /* addiu t2, t2, 1                                      */
        0xAD0A0004u,   /* sw    t2, 4(t0)                                      */
        0x08004004u,   /* j     loop (0x80010010)                              */
        0x00000000u,   /* nop (delay slot)                                     */
    };
    unsigned char hdr[2048];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "PS-X EXE", 8);
    const uint32_t textSize = 2048;                 /* padded to a sector */
    Put32(hdr + 0x10, 0x80010000u);                 /* pc0 */
    Put32(hdr + 0x14, 0);                           /* gp0 */
    Put32(hdr + 0x18, 0x80010000u);                 /* t_addr */
    Put32(hdr + 0x1C, textSize);                    /* t_size */
    Put32(hdr + 0x30, 0x801FFF00u);                 /* s_addr (stack) */
    Put32(hdr + 0x34, 0);                           /* s_size */
    memcpy(hdr + 0x4C, "Sony Computer Entertainment Inc. for North America area", 55);
    unsigned char text[textSize];
    memset(text, 0, sizeof(text));
    for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); i++) Put32(text + i * 4, code[i]);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && fwrite(text, 1, sizeof(text), f) == sizeof(text);
    fclose(f);
    return ok;
}

static uint32_t Read32(Ps1Core *c, uint32_t addr)
{
    uint32_t v = 0xDEADBEEFu;
    if (!ps1_read(c, addr, 4, &v)) return 0xDEADBEEFu;
    return v;
}

static void RunSession(const std::string &dir, int round)
{
    const std::string exe = dir + "/oga-test.exe";
    const std::string card = dir + "/card1.mcd";
    const std::string state = dir + "/state.bin";

    Ps1Core *c = ps1_create();
    CHECK(c != nullptr, "ps1_create returns a core");
    if (!c) return;
    CHECK(ps1_create() == nullptr, "a second ps1_create while one lives is refused");

    /* refusals, in words */
    char code[16] = "junk";
    CHECK(!ps1_load_disc(c, (dir + "/no-such-disc.cue").c_str(), nullptr, code), "a missing file is refused");
    CHECK(code[0] == '\0', "a refused load leaves no serial");
    CHECK(strlen(ps1_last_error(c)) > 0, "a refusal says why");
    CHECK(!ps1_frame(c), "no frame without a loaded disc");
    uint32_t v = 0;
    CHECK(!ps1_read(c, 0x80000000u, 4, &v), "no read without a loaded disc");
    CHECK(ps1_frame_rate(c) == 0.0, "no frame rate before a disc loads");

    CHECK(ps1_load_disc(c, exe.c_str(), card.c_str(), code), "the synthetic PS-EXE loads");
    if (round == 0 && strlen(ps1_last_error(c)) > 0) fprintf(stderr, "  load error: %s\n", ps1_last_error(c));
    CHECK(code[0] == '\0', "a PS-EXE has no disc serial");
    CHECK(ps1_using_hle_bios(c), "with no BIOS folder the HLE BIOS runs");
    {
        /* The core's own timing: NTSC (about 59.94) or PAL (50), never the
         * app's guess. A PS-EXE carries no region, so either is the core's. */
        double fps = ps1_frame_rate(c);
        CHECK((fps > 59.0 && fps < 61.0) || (fps > 49.0 && fps < 51.0),
              "the loaded program reports an NTSC or PAL frame rate");
    }
    CHECK(!ps1_frame(c), "no frame before start");
    CHECK(ps1_start(c) && ps1_running(c), "start");

    int16_t audio[2 * 4096];
    int audioFrames = 0;
    for (int i = 0; i < 120; i++) {
        CHECK(ps1_frame(c), "a frame runs");
        audioFrames += ps1_read_audio(c, audio, 4096);
    }
    CHECK(ps1_frames_completed(c) == 120, "120 frames counted");
    /* ~32768 Hz over ~2 s; generous bounds, the point is "audio flows at the
     * app's rate", not a sample-exact count. */
    CHECK(audioFrames > 32768, "audio flows (at least ~1 s for 2 s of frames)");
    CHECK(audioFrames < 32768 * 3, "audio is resampled down, not passed at 44.1 kHz in bulk");

    int w = 0, h = 0;
    CHECK(ps1_framebuffer(c, &w, &h), "a framebuffer exists");
    CHECK(w >= 256 && w <= 1024 && h >= 224 && h <= 512, "the framebuffer has a PlayStation size");
    const uint8_t *fb = ps1_framebuffer_ptr(c);
    CHECK(fb != nullptr && fb[3] == 0xFF, "pixels are RGBA with opaque alpha");

    /* The program's marker, through each spelling an adapter may use. */
    CHECK(Read32(c, 0x80100000u) == kMarker, "the marker reads at KSEG0");
    CHECK(Read32(c, 0x00100000u) == kMarker, "the marker reads at KUSEG");
    CHECK(Read32(c, 0xA0100000u) == kMarker, "the marker reads at KSEG1");
    CHECK(Read32(c, 0x80300000u) == kMarker, "the marker reads through the 2 MiB mirror");
    uint32_t b = 0, hw = 0;
    CHECK(ps1_read(c, 0x80100000u, 1, &b) && b == 0x31u, "a byte read is little-endian");
    CHECK(ps1_read(c, 0x80100002u, 2, &hw) && hw == 0x4F47u, "a halfword read is little-endian");
    CHECK(!ps1_read(c, 0x80100000u, 3, &v), "width 3 is refused");
    CHECK(!ps1_read(c, 0x1F801070u, 4, &v), "hardware registers are refused (reads have side effects)");
    CHECK(!ps1_read(c, 0x1FC00000u, 4, &v), "the BIOS ROM is outside the map");
    CHECK(!ps1_read(c, 0x801FFFFEu, 4, &v), "a read straddling the end of RAM is refused");
    CHECK(ps1_read(c, 0x1F800000u, 4, &v), "the scratchpad reads");
    CHECK(ps1_read(c, 0x1F8003FCu, 4, &v), "the last scratchpad word reads");
    CHECK(!ps1_read(c, 0x1F8003FEu, 4, &v), "a read past the scratchpad is refused");

    /* The program counts; a frame must move the count. */
    uint32_t n1 = Read32(c, 0x80100004u);
    ps1_frame(c);
    uint32_t n2 = Read32(c, 0x80100004u);
    CHECK(n2 != n1, "the CPU runs between frames (the counter moves)");

    /* A state round trip restores the machine exactly. */
    CHECK(ps1_save_state(c, state.c_str()), "a state saves");
    uint32_t saved = Read32(c, 0x80100004u);
    for (int i = 0; i < 10; i++) ps1_frame(c);
    CHECK(Read32(c, 0x80100004u) != saved, "the counter moves on after the save");
    CHECK(ps1_load_state(c, state.c_str()), "the state loads");
    CHECK(Read32(c, 0x80100004u) == saved, "the restored counter is the saved one");
    CHECK(Read32(c, 0x80100000u) == kMarker, "the marker survives the round trip");
    CHECK(!ps1_load_state(c, (dir + "/no-such-state").c_str()), "a missing state is refused");
    CHECK(ps1_frame(c), "frames run after a state load");

    /* Buttons are accepted (the program does not read the pad; this proves
     * the range checks, not the input path). */
    ps1_set_button(c, PS1_BTN_CROSS, true);
    ps1_set_button(c, PS1_BTN_CROSS, false);
    ps1_set_button(c, -1, true);
    ps1_set_button(c, PS1_BTN_COUNT, true);
    ps1_set_button(c, PS1_BTN_L2, true);
    ps1_set_button(c, PS1_BTN_L2, false);
    /* The sticks, clamped and NaN-safe; frames keep running through them
     * (the DualShock reads them every frame). */
    ps1_set_analog(c, PS1_STICK_LEFT, 0.5f, -0.5f);
    ps1_set_analog(c, PS1_STICK_RIGHT, 2.0f, -2.0f);
    ps1_set_analog(c, PS1_STICK_LEFT, 0.0f / 0.0f, 0.0f);
    ps1_set_analog(c, 2, 1.0f, 1.0f);
    ps1_set_analog(c, -1, 1.0f, 1.0f);
    CHECK(ps1_frame(c), "frames run with the sticks moved");
    ps1_set_analog(c, PS1_STICK_LEFT, 0.0f, 0.0f);
    ps1_set_analog(c, PS1_STICK_RIGHT, 0.0f, 0.0f);

    ps1_stop(c);
    CHECK(!ps1_running(c), "stop");
    ps1_destroy(c);
}

int main()
{
    char tmpl[] = "/tmp/oga-ps1-test-XXXXXX";
    const char *d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); return 1; }
    const std::string dir = d;
    if (!WriteExe(dir + "/oga-test.exe")) { fprintf(stderr, "could not write the test program\n"); return 1; }

    /* Twice: the second round proves destroy really releases the singleton
     * and that the core boots clean after a deinit. */
    for (int round = 0; round < 2; round++) RunSession(dir, round);

    std::string rm = "rm -rf '" + dir + "'";
    if (system(rm.c_str()) != 0) fprintf(stderr, "note: could not remove %s\n", dir.c_str());
    if (gFails) { fprintf(stderr, "ps1-core: %d of %d check(s) FAILED\n", gFails, gChecks); return 1; }
    printf("ps1-core: PASS (%d checks)\n", gChecks);
    return 0;
}
