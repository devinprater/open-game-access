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
                 "usage: %s --identity <rom> <savedir>\n",
                 argv0);
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

int main(int argc, char** argv)
{
    if (argc < 2) { Usage(argv[0]); return 2; }

    if (std::strcmp(argv[1], "--identity") == 0) {
        if (argc < 4) { Usage(argv[0]); return 2; }
        return Identity(argv[2], argv[3]);
    }

    Usage(argv[0]);
    return 2;
}
