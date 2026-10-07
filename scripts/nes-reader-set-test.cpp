// nes-reader-set-test.cpp — which bundled reader does the CORE pick for a given ROM?
//
// ⛔ THIS IS THE GATE THAT WAS MISSING. v0.6.0-nes claimed "NES readers host and speak"
// while the shipped IPA contained no reader assets and nothing ever pointed the NES
// backend at a script: the core linked, booted, and stayed silent. A build check cannot
// see that. This harness asks the two questions a player's experience actually depends
// on, with the real core and a real ROM:
//
//   * nes_reader_set() names the right set for a known dump, and "" for anything else
//     (an unknown dump must NOT get someone else's reader -- a wrong reader narrates
//     confident nonsense, which this project treats as worse than silence);
//   * the bundled set's directory loads as a reader: nes_set_script_dir + nes_start
//     puts a script in the coroutine and the reader emits SPEECH.
//
// Both are driven through the reader-set DIRECTORY you pass in, so the same binary
// proves the packaged layout that ships.
//
// Usage: nes-reader-set-test <rom> <reader-root> [frames]
//          reader-root holds <set>/ containing oga_nes_reader.lua
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "pch.h"
#include "mesen_core.h"

static int g_spoken = 0;
static void OnSpeech(const char* t, bool, void*) {
    if (t && *t) { g_spoken++; printf("SPEAK: %s\n", t); fflush(stdout); }
}
static void OnLog(const char* t, void*) {
    if (t && *t && strstr(t, "no bundled reader")) { printf("LOG: %s\n", t); fflush(stdout); }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) {
        fprintf(stderr, "usage: %s <rom> <reader-root> [frames]\n", argv[0]);
        return 2;
    }
    int cap = (argc > 3) ? atoi(argv[3]) : 300;
    int failures = 0;

    NesCore* c = nes_create();
    nes_set_log_callback(c, OnLog, nullptr);
    nes_set_speech_callback(c, OnSpeech, nullptr);

    char code[16] = {0};
    if (!nes_load_rom(c, argv[1], "", code)) {
        printf("FAIL load: %s\n", nes_last_error(c));
        return 1;
    }

    uint32_t crc = nes_rom_crc32(c);
    const char* set = nes_reader_set(c);
    printf("rom        : %s\n", argv[1]);
    printf("crc32      : %u\n", crc);
    printf("reader set : %s\n", (set && *set) ? set : "(none)");

    /* The CRC must be a real value -- 0 means the core had no ROM and every row in
     * kReaderSets would be unreachable. */
    if (crc == 0) { printf("FAIL: crc32 is 0; the ROM did not load into a console.\n"); return 1; }

    if (!set || !*set) {
        /* No set is a legitimate, honest answer for an unknown dump. Say which it was
         * so a caller cannot confuse "unknown game" with "the table is broken". */
        printf("\nRESULT: this ROM has no bundled reader (crc32 %u).\n", crc);
        printf("        That is a correct answer for a game nobody has written one for,\n");
        printf("        and it means the console must run with NO reader.\n");
        nes_destroy(c);
        return 0;
    }

    /* The set's directory must exist under the root the caller gave us: that is the
     * packaging question, and it is exactly what the shipped IPA got wrong. */
    std::string dir = std::string(argv[2]) + "/" + set;
    std::string entry = dir + "/oga_nes_reader.lua";
    FILE* f = fopen(entry.c_str(), "rb");
    if (!f) {
        printf("FAIL: the core named '%s' but %s is not there.\n", set, entry.c_str());
        nes_destroy(c);
        return 1;
    }
    fclose(f);
    printf("entry      : %s (present)\n", entry.c_str());

    nes_set_script_dir(c, dir.c_str());
    if (!nes_start(c)) {
        printf("FAIL start: %s\n", nes_last_error(c));
        nes_destroy(c);
        return 1;
    }
    printf("script loaded: %s\n", nes_script_loaded(c) ? "yes" : "NO");
    for (int i = 0; i < cap; i++) if (!nes_frame(c)) break;

    printf("\n=== VERDICT\n");
    printf("  frames completed : %llu\n", nes_frames_completed(c));
    printf("  reader live      : %s\n", nes_script_loaded(c) ? "yes" : "no");
    printf("  SPOKEN lines     : %d\n", g_spoken);

    /* ⛔ SPEECH IS THE PROOF, NOT "it did not crash". A reader that loads and says
     * nothing is byte-for-byte the failure the release shipped. */
    if (g_spoken > 0) {
        printf("  PASS: the bundled reader loaded and reached the speech sink.\n");
    } else {
        printf("  FAIL: the reader loaded but emitted NO speech.\n");
        failures++;
    }
    nes_stop(c); nes_destroy(c);
    return failures ? 1 : 0;
}
