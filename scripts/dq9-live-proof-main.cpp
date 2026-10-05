// dq9-live-proof.cpp — DQ9 live-RAM adapter proof.
//
// Boots the real ROM in the real melonDS core through the production PokeCore path, attaches the
// REAL DQ9 adapter from the registry, and reports what it says about LIVE RAM. This closes the one
// open verification gap: every DQ9 check so far ran on SYNTHETIC RAM (dq9_adapter_test.cpp) using
// addresses taken from the mod author, never confirmed inside a running game.
//
// Shaped deliberately like scripts/psp-live-proof-main.cpp, including its two hard-won rules:
//
//   1. ASK, DO NOT WAIT. Nothing logs the screen spontaneously -- read state only in answer to a
//      command. (A loop that scans the log for lines nobody logs can never see anything.)
//   2. Read RAM through the ADAPTER, never around it. There is no public peek in pokecore.h, and
//      adding one would prove the harness works rather than the adapter. If the adapter cannot
//      answer, the harness reports that as the finding.
//
// The proof is two-sided, and the refusal half matters as much:
//   * at the TITLE the adapter must refuse  -- a gate that never refuses is not a gate
//   * past name entry it must report a real map code and party
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "pokecore.h"
#include "adapter.h"

static std::vector<std::string> g_say;
static std::vector<std::string> g_log;

static void OnSpeak(const char *text, bool /*interrupt*/, void * /*userdata*/) {
    if (text && *text) {
        g_say.push_back(text);
        printf("LIVE-SAY: %s\n", text);
        fflush(stdout);
    }
}

static void OnLog(const char *text, void * /*userdata*/) {
    if (text && *text) {
        g_log.push_back(text);
        if (g_log.size() > 20000) g_log.erase(g_log.begin());
    }
}

// Advance N frames pressing nothing. Distinct from tap(): "sit still and let the game run" is a
// different act from "press something and wait for it to land", and conflating them is how a
// walk ends up pressing buttons through a screen it was supposed to be reading.
static bool run(PokeCore *core, int frames) {
    for (int i = 0; i < frames; i++) if (!poke_frame(core)) return false;
    return true;
}

// Tap a button, hold briefly, let the game settle. Used only to walk menus, never to script a
// story, so it tolerates a missed confirm.
static bool tap(PokeCore *core, int button, int hold, int settle) {
    poke_set_button(core, button, true);
    for (int i = 0; i < hold; i++) if (!poke_frame(core)) return false;
    poke_set_button(core, button, false);
    for (int i = 0; i < settle; i++) if (!poke_frame(core)) return false;
    return true;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <dq9.nds> <savedir> <frame-cap> [bios9 bios7 firmware]\n", argv[0]);
        return 2;
    }
    const char *rom = argv[1];
    const char *savedir = argv[2];
    long cap = atol(argv[3]);
    if (cap <= 0) cap = 20000;

    PokeCore *core = poke_create();
    if (!core) { fprintf(stderr, "LIVE-FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    poke_set_log_callback(core, OnLog, nullptr);

    if (argc >= 7) {
        poke_set_firmware(core, argv[4], argv[5], argv[6]);
        printf("LIVE: firmware paths given: %s / %s / %s\n", argv[4], argv[5], argv[6]);
        // has_firmware is only decided DURING the load (Firmware::IsBootable), so reporting it
        // here would always print 0. It is printed after poke_load_rom below.
    }

    // The DS boot path refuses to start without a bundled script ("No accessibility script was
    // bundled"). That guard is right for the app. It is irrelevant to what is being proved here:
    // the C++ DQ9 adapter is STANDALONE -- it reads RAM through the Host vtable and touches no Lua.
    // A minimal script satisfies the boot path without dragging in the whole mod, so a failure in
    // this run cannot be a failure of the mod's script either.
    poke_set_script(core,
        "-- minimal: satisfies the boot path so the standalone C++ adapter can be driven.\n"
        "return true\n");

    if (!poke_load_rom(core, rom, savedir)) {
        fprintf(stderr, "LIVE-FAIL: load: %s\n", poke_last_error(core));
        return 1;
    }
    const char *id = poke_adapter_id(core);
    const char *name = poke_adapter_name(core);
    const char *code = poke_game_code(core);
    printf("LIVE: adapter id=%s name=%s code=%s\n",
           id ? id : "(none)", name ? name : "(none)", code ? code : "(none)");
    printf("LIVE: firmware bootable = %d  (decided during the load)\n",
           (int) poke_has_firmware(core));
    if (!id || strcmp(id, "dq9") != 0) {
        fprintf(stderr, "LIVE-FAIL: the registry did not hand back the dq9 adapter (got %s)\n",
                id ? id : "(none)");
        poke_destroy(core);
        return 1;
    }

    if (!poke_start(core)) {
        fprintf(stderr, "LIVE-FAIL: start: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }

    long frames = 0;
    int ready_frame = -1;

    // ---- PHASE 0: THE REFUSAL HALF OF THE PROOF ------------------------------------------
    // Sit on the title and ASK. Nothing may be spoken about a map, a party or a position:
    // at that point the "addresses" are reading BIOS/junk, and anything printable is a
    // coincidence. A gate that never refuses is not a gate.
    run(core, 600);
    g_say.clear();
    poke_command(core, (int) oga::Command::WhereAmI);
    for (int i = 0; i < 30; i++) poke_frame(core);          // the queue delivers over frames
    int title_lines = 0;
    for (auto &s : g_say) {
        printf("  AT TITLE WhereAmI -> \"%s\"\n", s.c_str());
        if (strncmp(s.c_str(), "On ", 3) == 0) title_lines++;
    }
    printf("  AT TITLE: %d map-shaped line(s) spoken. %s\n", title_lines,
           title_lines == 0 ? "CORRECT -- the gate refused." : "WRONG -- junk was spoken as a map.");
    g_say.clear();

    // Phase 1: boot, mashing only until the adapter first claims readiness.
    while (frames < cap) {
        if (!poke_frame(core)) { printf("LIVE: core stopped at frame %ld\n", frames); break; }
        frames++;
        if (frames % 200 == 0 && poke_adapter_ready(core)) {
            ready_frame = (int) frames;
            printf("LIVE: adapter reported READY at frame %ld\n", frames);
            fflush(stdout);
            break;
        }
        if (frames % 90 == 0) {   // walk title / file select / name entry
            int btn = ((frames / 90) % 5 == 4) ? POKE_BTN_START : POKE_BTN_A;
            if (!tap(core, btn, 10, 40)) break;
            frames += 50;
        }
    }

    if (ready_frame < 0) {
        printf("\n=== VERDICT ===\n");
        printf("  the adapter NEVER reported ready in %ld frames.\n", frames);
        printf("  Real but incomplete: EITHER the walk never got past the title, OR the gate refuses\n");
        printf("  for a reason that persists. Distinguish by driving the same ROM by hand before\n");
        printf("  concluding the addresses are wrong.\n");
        fflush(stdout);
        poke_stop(core); poke_destroy(core);
        return 0;
    }

    // Phase 2: the adapter says it is ready -- ask it what it sees, and keep walking so a lucky
    // first-ready (title RAM that happened to look printable) cannot stand as the proof.
    printf("\n=== IN GAME: what does the adapter report? ===\n");
    bool got_map = false, got_party = false;
    for (int round = 0; round < 24; round++) {
        g_say.clear();
        poke_command(core, (int) oga::Command::WhereAmI);
        for (int i = 0; i < 4; i++) poke_frame(core);
        for (auto &s : g_say) {
            printf("  WhereAmI -> \"%s\"\n", s.c_str());
            // Match what the adapter ACTUALLY says. The first version looked for the word "map"
            // and reported "NO" on a run whose whole output was "On M01M1200, position -6, 13.
            // In battle." -- a harness lying about its own success, which is the worst kind of
            // bug because the verdict is what gets quoted.
            if (strncmp(s.c_str(), "On ", 3) == 0) got_map = true;
        }
        // Ask for a party member by name: the real proof that the party addresses are live,
        // not merely that a map code is printable. Poll ACROSS frames: Say() routes through the
        // announcement queue, so the answer lands later than the command (the first version
        // cleared the buffer and read it immediately, then reported the previous line as if it
        // were the party reply).
        size_t before = g_say.size();
        poke_command(core, (int) oga::Command::NextAlly);
        bool answered = false;
        for (int i = 0; i < 600 && !answered; i++) {   // up to ~10 s of speech
            poke_frame(core);
            for (size_t k = before; k < g_say.size(); k++) {
                printf("  NextAlly -> \"%s\"\n", g_say[k].c_str());
                // the adapter's real party format is "<name>, N of 4." -- matching for the word
                // "level" looked for something it never says.
                if (strstr(g_say[k].c_str(), " of 4")) got_party = true;
                answered = true;
            }
        }
        if (!answered) printf("  NextAlly -> (no answer within 600 frames)\n");
        fflush(stdout);
        if (!tap(core, POKE_BTN_A, 8, 120)) break;
        frames += 128;
        if (frames > cap) break;
    }

    printf("\n=== VERDICT ===\n");
    printf("  adapter id handed back by the registry: %s (%s)\n", id, name ? name : "?");
    printf("  adapter reported ready at frame:        %d\n", ready_frame);
    printf("  at the TITLE it refused to speak a map: %s\n", title_lines == 0 ? "YES" : "NO");
    printf("  a map-code answer was spoken:           %s\n", got_map ? "YES" : "NO");
    printf("  a party/level answer was spoken:        %s\n", got_party ? "YES" : "NO");
    printf("  (a NO is honest: the adapter may read correct addresses this particular walk never\n");
    printf("   reached. Say which, do not merge the two.)\n");
    fflush(stdout);

    poke_stop(core);
    poke_destroy(core);
    return 0;
}
