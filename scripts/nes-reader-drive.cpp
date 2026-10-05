// nes-reader-drive.cpp — press buttons and see whether the reader narrates real gameplay.
//
// The undriven run proved the reader loads, finds its speech file, and reads live RAM (it announced
// the inventory menu). This presses START on the Zelda title and then plays a little, so the reader
// has real transitions to talk about -- which is what "it reads the game" actually means.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "pch.h"
#include "mesen_core.h"

static int g_spoken = 0;
static void OnSpeech(const char* t, bool, void*) {
    if (t && *t) { g_spoken++; printf("SPEAK: %s\n", t); fflush(stdout); }
}
static void OnLog(const char* t, void*) { if (t && *t) { printf("LOG: %s\n", t); fflush(stdout); } }

static void run(NesCore* c, int n) { for (int i = 0; i < n && nes_frame(c); i++) {} }
static void tap(NesCore* c, int b, int hold, int settle) {
    nes_set_button(c, b, true);  run(c, hold);
    nes_set_button(c, b, false); run(c, settle);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <reader_dir> [frames]\n", argv[0]); return 2; }

    NesCore* c = nes_create();
    nes_set_log_callback(c, OnLog, nullptr);
    nes_set_speech_callback(c, OnSpeech, nullptr);
    char code[16] = {0};
    if (!nes_load_rom(c, argv[1], "/home/devin/nes-rom/home", code)) { printf("FAIL load\n"); return 1; }
    nes_set_script_dir(c, argv[2]);
    if (!nes_start(c)) { printf("FAIL start: %s\n", nes_last_error(c)); return 1; }

    printf("reader loaded. booting %s...\n", code);
    run(c, 240);

    // START at the title, then a short walk south, which is the first thing Zelda says anything about.
    printf("--- pressing START\n");
    tap(c, NES_BTN_START, 12, 120);
    run(c, 180);
    printf("--- walking\n");
    nes_set_button(c, NES_BTN_DOWN, true);
    run(c, 120);
    nes_set_button(c, NES_BTN_DOWN, false);
    run(c, 120);

    printf("\n=== VERDICT\n");
    printf("  frames:            %llu\n", nes_frames_completed(c));
    printf("  reader live:       %s\n", nes_script_loaded(c) ? "yes" : "no");
    printf("  SPOKEN lines:      %d\n", g_spoken);
    nes_stop(c); nes_destroy(c);
    return 0;
}
