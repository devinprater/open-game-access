// psp-live-proof-main.cpp — Dissidia live-RAM adapter proof.
//
// Boots the real game in the real PPSSPP core (via the production PokeCore
// path, not the raw PspCore), attaches the REAL Dissidia adapter from the
// registry, and reports what it says about live RAM: attach/ready, MENU
// progression, and spoken answers to WhereAmI/DumpState.
//
// The synthetic-RAM test (dissidia_adapter_test.cpp) proves the adapter logic;
// this proves the addresses it reads are live in a real boot.
#include <cstdio>
#include <cstring>
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
    }
}

static void OnLog(const char *text, void * /*userdata*/) {
    if (text && *text) {
        g_log.push_back(text);
        if (g_log.size() > 5000) g_log.erase(g_log.begin());
        if (strstr(text, "MENU")) printf("LIVE-LOG: %s\n", text);
    }
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s <image> <savedir> <frame-cap> [state]\n", argv[0]);
        return 2;
    }
    const char *image = argv[1];
    const char *savedir = argv[2];
    int cap = atoi(argv[3]);
    if (cap <= 0) cap = 12000;
    const char *resume = (argc == 5) ? argv[4] : nullptr;

    PokeCore *core = poke_create();
    if (!core) { fprintf(stderr, "LIVE-FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    poke_set_log_callback(core, OnLog, nullptr);
    if (!poke_load_rom(core, image, savedir)) {
        fprintf(stderr, "LIVE-FAIL: load: %s\n", poke_last_error(core));
        return 1;
    }
    const char *id = poke_adapter_id(core);
    const char *name = poke_adapter_name(core);
    printf("LIVE: adapter id=%s name=%s code=%s\n",
           id ? id : "(none)", name ? name : "(none)", poke_game_code(core));
    if (!id || strcmp(id, "dissidia") != 0) {
        fprintf(stderr, "LIVE-FAIL: wrong adapter\n");
        return 1;
    }

    // Every failure shuts the core down first: PPSSPP's audio, IO and worker
    // threads otherwise keep the process alive after main returns (a failed
    // run used to hang until killed instead of exiting 1).
    auto fail = [&](const char *msg) {
        fprintf(stderr, "LIVE-FAIL: %s\n", msg);
        poke_stop(core);
        poke_destroy(core);
        return 1;
    };

    if (!poke_start(core)) return fail(poke_last_error(core));
    if (resume && !poke_load_state(core, resume)) return fail(poke_last_error(core));
    if (resume) printf("LIVE: resumed from %s\n", resume);

    // ⛔ ASK, DO NOT WAIT. The adapter writes "MENU <screen>" only in answer
    // to the MenuState command; nothing logs it spontaneously. The first
    // version of this loop only scanned the log, so it could never see the
    // title however the presses went. Poll MenuState every half second, and
    // press (A, with Start every 4th tap) only while the title is NOT up, so
    // a tap can no longer carry the game past it.
    static const int kPollEvery = 30;
    static const int kTapEvery = 120;
    int frames = 0, ready_frame = -1, title_frame = -1;
    std::string screen;
    while (frames < cap && poke_frame(core)) {
        frames++;
        if (ready_frame < 0) {
            if (frames % 100 == 0 && poke_adapter_ready(core)) {
                ready_frame = frames;
                printf("LIVE: adapter ready at frame %d\n", frames);
            }
            continue;
        }
        if (frames % kPollEvery == 0) {
            size_t before = g_log.size();
            poke_command(core, (int)oga::Command::MenuState);
            for (size_t i = before; i < g_log.size(); i++)
                if (g_log[i].rfind("MENU ", 0) == 0) screen = g_log[i];
            if (screen == "MENU title") {
                title_frame = frames;
                printf("LIVE: title menu tracked at frame %d\n", frames);
                break;
            }
        }
        if (frames % kTapEvery == 0) {
            int btn = ((frames / kTapEvery) % 4 == 3) ? POKE_BTN_START : POKE_BTN_A;
            poke_set_button(core, btn, true);
            for (int k = 0; k < 10; k++) { poke_frame(core); frames++; }
            poke_set_button(core, btn, false);
        }
    }
    if (ready_frame < 0) return fail("adapter never ready");

    printf("LIVE: frames=%d/%d ready_frame=%d title_frame=%d last=%s\n",
           frames, cap, ready_frame, title_frame, screen.empty() ? "(none)" : screen.c_str());
    if (title_frame < 0) return fail("title menu never tracked");
    g_say.clear();
    bool wai = poke_command(core, (int)oga::Command::WhereAmI);
    bool dump = poke_command(core, (int)oga::Command::DumpState);
    printf("LIVE: whereami=%d dump=%d say_lines=%zu\n",
           (int)wai, (int)dump, g_say.size());
    if (!wai || g_say.empty()) return fail("WhereAmI produced no speech");
    // The answer must name a real title row, not the boot-gap fallback.
    bool names_row = false;
    for (const auto &l : g_say)
        if (l.find("New Game") != std::string::npos ||
            l.find("Load Game") != std::string::npos ||
            l.find("Data Install") != std::string::npos) names_row = true;
    if (!names_row) return fail("WhereAmI did not name a title row");
    printf("LIVE-PASS: live RAM attaches, readies, and answers\n");
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
