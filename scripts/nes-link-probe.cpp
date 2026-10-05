// nes-link-probe.cpp — does the NES core LINK and BOOT a real ROM?
//
// This is the smallest thing that can tell the truth about the glue: create the core, load a ROM,
// start it, run frames, read RAM through the core's own read path, and report. It deliberately does
// NOT go through PokeCore yet -- pokecore.cpp has to grow an NES branch first, and this probe
// isolates "is the core itself sound" from "is the app wired to it".
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "mesen_core.h"

static int g_logs = 0;
static void OnLog(const char* t, void*) { if (t && *t && g_logs < 40) { printf("LOG: %s\n", t); g_logs++; } }

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);   // unbuffered: a hang must not swallow progress
    if (argc < 2) { fprintf(stderr, "usage: %s <rom.nes> [frames]\n", argv[0]); return 2; }
    const char* rom = argv[1];
    int cap = (argc > 2) ? atoi(argv[2]) : 120;

    NesCore* c = nes_create();
    if (!c) { printf("FAIL: nes_create\n"); return 1; }
    nes_set_log_callback(c, OnLog, nullptr);

    char code[16] = {0};
    printf("loading %s\n", rom);
    if (!nes_load_rom(c, rom, nullptr, code)) {
        printf("FAIL: load: %s\n", nes_last_error(c));
        nes_destroy(c);
        return 1;
    }
    printf("loaded OK. game code=\"%s\" ram_base=0x%04X\n", code, nes_ram_base(c));

    if (!nes_start(c)) { printf("FAIL: start: %s\n", nes_last_error(c)); nes_destroy(c); return 1; }
    printf("started OK\n");

    // Run the frames. A real NES boot takes a few seconds of emulated time; a core that is not
    // actually stepping will show a STATIC framebuffer and a RAM block that never changes.
    for (int i = 0; i < cap; i++) {
        if (!nes_frame(c)) { printf("FAIL: frame %d: %s\n", i, nes_last_error(c)); break; }
    }
    printf("ran %llu frames\n", nes_frames_completed(c));

    int w = 0, h = 0;
    if (nes_framebuffer(c, &w, &h)) printf("framebuffer %dx%d\n", w, h);
    else printf("NO framebuffer (the PPU never produced one)\n");

    // Read the zero page, and print RAW values beside any derived verdict.
    unsigned zeros = 0, ffs = 0, sum = 0;
    for (unsigned a = 0; a < 0x100; a++) {
        uint32_t v = 0;
        if (!nes_read(c, a, 1, &v)) { printf("FAIL: read 0x%04X refused\n", a); break; }
        if (v == 0) zeros++;
        if (v == 0xFF) ffs++;
        sum += v;
    }
    printf("zero page: %u zeros, %u 0xFFs, sum=%u\n", zeros, ffs, sum);
    printf("  first 8 bytes:");
    for (unsigned a = 0; a < 8; a++) {
        uint32_t v = 0; nes_read(c, a, 1, &v); printf(" %02X", v);
    }
    printf("\n");

    // ⛔ THE FIXTURE'S OWN PROPERTY, checked exactly. fixture.nes does INC $00 ; JMP $C000 forever,
    // so RAM[0x00] must be non-zero and must ADVANCE between reads. This is the assertion a
    // "ram is not all zero" heuristic cannot make: a dead core and a core stuck in a loop both
    // pass that, and only the advancing check tells "running the right code" from "loaded".
    uint32_t a0 = 0, b0 = 0;
    nes_read(c, 0x00, 1, &a0);
    for (int i = 0; i < 30; i++) nes_frame(c);
    nes_read(c, 0x00, 1, &b0);
    printf("RAM[0x00] advanced %u -> %u : %s\n", a0, b0,
           (b0 != a0) ? "THE CPU IS EXECUTING" : "STUCK (loaded but not stepping)");
    printf("RAM[0x00] nonzero: %s\n", b0 != 0 ? "yes" : "NO");

    // Prove the read path returns FALSE outside what it can vouch for -- the adapter contract's
    // "tell 0 from outside the map" rule.
    uint32_t dummy = 0;
    printf("read of a 2-byte at 0xFFFF (would wrap): %s\n",
           nes_read(c, 0xFFFF, 2, &dummy) ? "ALLOWED -- BUG" : "refused (correct)");

    nes_stop(c);
    nes_destroy(c);
    printf("done\n");
    return 0;
}
