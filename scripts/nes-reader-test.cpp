// nes-reader-test.cpp — does a real reader script RUN inside the NES core?
//
// This is the first test of the whole reader path: the console boots, the Lua host installs its
// bindings, the wrapper loads the mod, the mod finds its own Data/ files, and the reader's
// `while true do emu.frameadvance() end` yields to the core's frame loop, which resumes it.
//
// What proves it worked:
//   * the script loaded without error (the core reports the error string if not)
//   * SPEECH came out -- the reader talks through the log/speech sink, and a silent run means the
//     bindings are wrong even if it "ran"
//   * frames advanced (the coroutine actually yields and resumes per frame)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "pch.h"
#include "mesen_core.h"

static std::vector<std::string> g_lines;
static int g_spoken = 0;

static void OnSpeech(const char* t, bool, void*) {
    if (t && *t) { g_spoken++; printf("SPEAK: %s\n", t); fflush(stdout); }
}
static void OnLog(const char* t, void*) {
    if (t && *t) {
        g_lines.push_back(t);
        if (g_lines.size() <= 60) { printf("LOG: %s\n", t); fflush(stdout); }
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <reader_dir> [frames]\n", argv[0]); return 2; }
    int cap = (argc > 3) ? atoi(argv[3]) : 900;

    NesCore* c = nes_create();
    nes_set_log_callback(c, OnLog, nullptr);
    nes_set_speech_callback(c, OnSpeech, nullptr);

    char code[16] = {0};
    if (!nes_load_rom(c, argv[1], "/home/devin/nes-rom/home", code)) {
        printf("FAIL load: %s\n", nes_last_error(c)); return 1;
    }
    printf("loaded rom: code=%s\n", code);

    nes_set_script_dir(c, argv[2]);
    if (!nes_start(c)) {
        printf("FAIL start: %s\n", nes_last_error(c));
        printf("  (a reader error here is the finding: it means the script or the bindings failed)\n");
        return 1;
    }
    printf("started; reader loaded: %s\n", nes_script_loaded(c) ? "yes" : "NO");

    for (int i = 0; i < cap; i++) {
        if (!nes_frame(c)) { printf("frame %d failed: %s\n", i, nes_last_error(c)); break; }
        if (i % 300 == 0 && i) printf("  ...%d frames, spoken=%d, script=%s\n",
                                      i, g_spoken, nes_script_loaded(c) ? "live" : "STOPPED");
    }

    printf("\n=== VERDICT\n");
    printf("  frames completed:      %llu\n", nes_frames_completed(c));
    printf("  reader still loaded:   %s\n", nes_script_loaded(c) ? "yes" : "no");
    printf("  log lines:             %zu\n", g_lines.size());
    printf("  SPOKEN lines:          %d\n", g_spoken);
    printf("  (spoken > 0 is the real proof: the reader is reaching the speech sink)\n");
    nes_stop(c); nes_destroy(c);
    return 0;
}
