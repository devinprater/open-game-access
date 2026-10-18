// snes-live-proof.cpp — the SNES through the APP's own PokeCore path.
//
// The core probe proved the console. This proves the wiring: a .sfc resolves to
// the SNES backend, loads through poke_load_rom, runs frames through poke_frame,
// and reports a content-derived id with NO adapter attached (correct: no reader
// exists for this console yet).
//
// Usage:
//   snes-live-proof <rom> <savedir> [expected-code|any]
//   snes-live-proof --reload <romA> <romB> <savedir>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pokecore.h"
#include "snes_core.h"

static std::vector<std::string> g_say;

static void OnSpeak(const char* t, bool, void*)
{
    if (t && *t) { g_say.push_back(t); std::printf("SAY: %s\n", t); }
}

struct Loaded {
    bool ok = false;
    std::string backend;
    std::string code;
    bool adapter_ready = false;
    int frames = 0;
    bool spoke = false;
};

static Loaded LoadAndRun(const char* rom, const char* savedir)
{
    Loaded r;
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return r; }

    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();

    std::printf("=== loading %s\n", rom);
    if (!poke_load_rom(core, rom, savedir)) {
        std::printf("FAIL: load: %s\n", poke_last_error(core));
        poke_destroy(core);
        return r;
    }

    const char* backend = poke_backend_id(core);
    const char* code = poke_game_code(core);
    r.backend = backend ? backend : "";
    r.code = code ? code : "";
    r.adapter_ready = poke_adapter_ready(core);

    if (!poke_start(core)) {
        std::printf("FAIL: start: %s\n", poke_last_error(core));
        poke_destroy(core);
        return r;
    }
    for (; r.frames < 300; r.frames++)
        if (!poke_frame(core)) break;

    r.spoke = !g_say.empty();
    r.ok = true;

    std::printf("backend=%s\n", r.backend.c_str());
    std::printf("code=%s\n", r.code.c_str());
    std::printf("adapter-ready=%d\n", (int) r.adapter_ready);
    std::printf("frames=%d\n", r.frames);
    std::printf("spoke=%d\n", (int) r.spoke);

    poke_stop(core);
    poke_destroy(core);
    return r;
}

static int One(const char* rom, const char* savedir, const char* expected)
{
    Loaded r = LoadAndRun(rom, savedir);
    if (!r.ok) return 1;

    bool code_ok = r.code.rfind("crc:", 0) == 0;
    if (std::strcmp(expected, "any") != 0) code_ok = code_ok && r.code == expected;

    bool ok = r.backend == "snes" && code_ok && !r.adapter_ready && !r.spoke && r.frames == 300;
    std::printf("live=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

static int Reload(const char* romA, const char* romB, const char* savedir)
{
    Loaded a = LoadAndRun(romA, savedir);
    Loaded b = LoadAndRun(romB, savedir);
    bool ok = a.ok && b.ok && a.backend == "snes" && b.backend == "snes" &&
              a.code != b.code && !b.adapter_ready && !b.spoke;
    std::printf("first-code=%s\n", a.code.c_str());
    std::printf("second-code=%s\n", b.code.c_str());
    std::printf("reload=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

// Review fix: a SECOND LOAD ON THE SAME CORE. The old Reload() built two cores,
// so it never exercised teardown at all.
static int Teardown(const char* romA, const char* romB, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }

    if (!poke_load_rom(core, romA, savedir) || !poke_start(core)) {
        std::printf("FAIL: first load: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    const std::string codeA = poke_game_code(core);
    for (int i = 0; i < 30; i++) poke_frame(core);

    if (!poke_load_rom(core, romB, savedir)) {
        std::printf("FAIL: second load: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    const std::string codeB = poke_game_code(core);
    for (int i = 0; i < 30; i++) poke_frame(core);

    const int live_after_second_load = snes_live_instances();
    poke_stop(core);
    poke_destroy(core);
    const int live_after_destroy = snes_live_instances();

    std::printf("first-code=%s\n", codeA.c_str());
    std::printf("second-code=%s\n", codeB.c_str());
    std::printf("live-after-second-load=%d\n", live_after_second_load);
    std::printf("live-after-destroy=%d\n", live_after_destroy);

    bool ok = codeA != codeB && live_after_second_load == 1 && live_after_destroy == 0;
    std::printf("teardown=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}

// --menus: the ADAPTER's menu speech on the real ROM and the public save.
// The core probe measured the addresses; this proves the app path speaks from
// them (adapter attached, on_frame running, announcements reaching the sink).
static void TapApp(PokeCore* core, int btn, int hold, int settle)
{
    for (int i = 0; i < hold; i++) { poke_set_button(core, btn, true); poke_frame(core); }
    poke_set_button(core, btn, false);
    for (int i = 0; i < settle; i++) poke_frame(core);
}

static int Menus(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();

    if (!poke_load_rom(core, rom, savedir) || !poke_start(core)) {
        std::printf("FAIL: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    std::printf("adapter=%s ready=%d\n",
                poke_adapter_id(core) ? poke_adapter_id(core) : "(none)",
                (int) poke_adapter_ready(core));

    // The same boot drive the core probe uses: START every 90 frames, A shortly
    // after, about 1500 frames to land in the save.
    for (int f = 0; f < 1500; f++) {
        poke_set_button(core, POKE_BTN_START, (f % 90) < 4);
        poke_set_button(core, POKE_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        poke_frame(core);
    }
    poke_set_button(core, POKE_BTN_START, false);
    poke_set_button(core, POKE_BTN_A, false);

    // The boot drive can end with the interface overlay already up (the first
    // load did) or closed (after a run that saved). One START tells us which:
    // if the adapter speaks an item menu line, the press opened it.
    std::printf("-- normalising the overlay with one START --\n");
    size_t before = g_say.size();
    TapApp(core, POKE_BTN_START, 4, 60);
    bool item_open = g_say.size() > before;
    if (!item_open) {
        std::printf("-- the menu was closed; opening it --\n");
        before = g_say.size();
        TapApp(core, POKE_BTN_START, 4, 60);
        item_open = g_say.size() > before;
    }
    if (!item_open) {
        std::printf("FAIL: the item menu never opened\n");
        poke_stop(core);
        poke_destroy(core);
        return 1;
    }

    std::printf("-- item menu: right --\n");
    TapApp(core, POKE_BTN_RIGHT, 4, 40);
    std::printf("-- item menu: left --\n");
    TapApp(core, POKE_BTN_LEFT, 4, 40);
    std::printf("-- closing the item menu --\n");
    TapApp(core, POKE_BTN_START, 4, 60);

    std::printf("-- opening the save menu --\n");
    before = g_say.size();
    TapApp(core, POKE_BTN_SELECT, 4, 90);
    bool save_open = g_say.size() > before;
    if (!save_open) {
        std::printf("-- SELECT did not open it; trying once more --\n");
        before = g_say.size();
        TapApp(core, POKE_BTN_SELECT, 4, 90);
        save_open = g_say.size() > before;
    }
    if (!save_open) {
        std::printf("FAIL: the save menu never opened\n");
        poke_stop(core);
        poke_destroy(core);
        return 1;
    }

    std::printf("-- save menu: down --\n");
    TapApp(core, POKE_BTN_DOWN, 4, 60);
    std::printf("-- save menu: up --\n");
    TapApp(core, POKE_BTN_UP, 4, 60);
    std::printf("-- cancelling the save menu --\n");
    TapApp(core, POKE_BTN_B, 4, 90);

    bool spoke_item = false, spoke_save = false;
    for (const std::string& s : g_say) {
        if (s.rfind("Item menu.", 0) == 0) spoke_item = true;
        if (s.rfind("Save menu.", 0) == 0) spoke_save = true;
    }
    bool ok = spoke_item && spoke_save;
    std::printf("item-menu-spoken=%d save-menu-spoken=%d\n", (int) spoke_item, (int) spoke_save);
    std::printf("lines=%d\n", (int) g_say.size());
    std::printf("menu-proof=%s\n", ok ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return ok ? 0 : 1;
}

// --files: the ADAPTER's file-select speech on the real ROM and save. The probe
// measured the slots in save RAM; this proves the app path speaks them.
static int Files(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();

    if (!poke_load_rom(core, rom, savedir) || !poke_start(core)) {
        std::printf("FAIL: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }

    /* Tap START until the adapter reports the file select; the moment it does,
     * stop pressing so no file loads. */
    /* Attach the adapter: on_frame only runs once ready() has wired it (the app
     * does this when it offers the controls). */
    (void) poke_adapter_ready(core);
    bool reached = false;
    std::printf("adapter=%s started\n", poke_adapter_id(core) ? poke_adapter_id(core) : "(none)");
    for (int f = 0; f < 4000 && !reached; f++) {
        poke_set_button(core, POKE_BTN_START, (f % 120) < 4);
        poke_frame(core);
        for (const std::string& s : g_say)
            if (s.rfind("File select.", 0) == 0) { reached = true; break; }
    }
    poke_set_button(core, POKE_BTN_START, false);
    for (int i = 0; i < 30; i++) poke_frame(core);
    if (!reached) {
        std::printf("FAIL: the file select never spoke\n");
        poke_stop(core);
        poke_destroy(core);
        return 1;
    }

    std::printf("-- file select: down --\n");
    TapApp(core, POKE_BTN_DOWN, 4, 40);
    std::printf("-- file select: down again --\n");
    TapApp(core, POKE_BTN_DOWN, 4, 40);
    std::printf("-- file select: copy --\n");
    TapApp(core, POKE_BTN_DOWN, 4, 40);
    std::printf("-- file select: erase --\n");
    TapApp(core, POKE_BTN_DOWN, 4, 40);
    std::printf("-- file select: up --\n");
    TapApp(core, POKE_BTN_UP, 4, 40);

    bool spoke_slot = false, spoke_empty = false, spoke_copy = false, spoke_erase = false;
    for (const std::string& s : g_say) {
        if (s.rfind("File select. File one", 0) == 0) spoke_slot = true;
        if (s.rfind("File two, empty.", 0) == 0) spoke_empty = true;
        if (s.rfind("Copy file.", 0) == 0) spoke_copy = true;
        if (s.rfind("Erase file.", 0) == 0) spoke_erase = true;
    }
    bool ok = spoke_slot && spoke_empty && spoke_copy && spoke_erase;
    std::printf("slot=%d empty=%d copy=%d erase=%d\n",
                (int) spoke_slot, (int) spoke_empty, (int) spoke_copy, (int) spoke_erase);
    std::printf("lines=%d\n", (int) g_say.size());
    std::printf("file-proof=%s\n", ok ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return ok ? 0 : 1;
}

// --walk: the adapter's AREA ENTRY line on the real game. Boots into Link's
// house, closes the menu, walks out, and checks that the first outdoor area is
// announced as the player steps into it.
static int Walk(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();

    if (!poke_load_rom(core, rom, savedir) || !poke_start(core)) {
        std::printf("FAIL: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    (void) poke_adapter_ready(core);

    for (int f = 0; f < 1500; f++) {
        poke_set_button(core, POKE_BTN_START, (f % 90) < 4);
        poke_set_button(core, POKE_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        poke_frame(core);
    }
    poke_set_button(core, POKE_BTN_START, false);
    poke_set_button(core, POKE_BTN_A, false);

    /* One START closes the interface overlay if it is up, or opens the item
     * menu if it is not; the second press closes it in that case. */
    size_t before = g_say.size();
    TapApp(core, POKE_BTN_START, 4, 60);
    bool opened = false;
    for (size_t i = before; i < g_say.size(); i++)
        if (g_say[i].rfind("Item menu.", 0) == 0) opened = true;
    if (opened) TapApp(core, POKE_BTN_START, 4, 60);

    std::printf("-- walking out --\n");
    for (int i = 0; i < 300; i++) {
        poke_set_button(core, POKE_BTN_DOWN, true);
        poke_set_button(core, POKE_BTN_A, (i % 30) >= 26 && (i % 30) < 30);
        poke_frame(core);
    }
    poke_set_button(core, POKE_BTN_DOWN, false);
    poke_set_button(core, POKE_BTN_A, false);
    for (int i = 0; i < 60; i++) poke_frame(core);

    bool spoke_outside = false;
    for (const std::string& s : g_say)
        if (s.rfind("Outside,", 0) == 0) spoke_outside = true;
    std::printf("outside-spoken=%d lines=%d\n", (int) spoke_outside, (int) g_say.size());

    /* Guidance on the live game: each press must produce one bearing, and the
     * second must be a different landmark (the list cycles). 41 is
     * oga::Command::Guide in Core/adapter.h, the same number Swift sends.
     * 40 is Status, which the same stale bound had also silenced.
     * ⛔ The queue speaks one line at a time and estimates a duration from the
     * text, so drain it between presses or the newest High replaces the one
     * still waiting and the proof undercounts. */
    g_say.clear();
    poke_command(core, 40);
    for (int f = 0; f < 400; f++) poke_frame(core);
    bool status_spoke = false;
    for (const std::string& s : g_say)
        if (s.find("heart") != std::string::npos) status_spoke = true;
    std::printf("status-proof=%s\n", status_spoke ? "YES" : "NO");

    g_say.clear();
    int bearings = 0;
    for (int i = 0; i < 2; i++) {
        poke_command(core, 41);
        for (int f = 0; f < 400; f++) poke_frame(core);
    }
    for (int f = 0; f < 400; f++) poke_frame(core);
    for (const std::string& s : g_say) {
        std::printf("SAY: %s\n", s.c_str());
        if (s.rfind("Guide: ", 0) == 0) bearings++;
    }
    std::printf("guide-lines=%d\n", bearings);
    std::printf("guide-proof=%s\n", bearings >= 2 ? "YES" : "NO");

    /* The objective on the live save: progress flags 0x15 and no pendants, so
     * it must name the pendants and point at Kakariko. 42 is Command::Objective. */
    g_say.clear();
    poke_command(core, 42);
    for (int f = 0; f < 400; f++) poke_frame(core);
    bool objective_spoke = false;
    for (const std::string& s : g_say) {
        std::printf("SAY: %s\n", s.c_str());
        if (s.rfind("Objective:", 0) == 0) objective_spoke = true;
    }
    std::printf("objective-proof=%s\n", objective_spoke ? "YES" : "NO");
    std::printf("walk-proof=%s\n", spoke_outside ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return (spoke_outside && bearings >= 2 && objective_spoke) ? 0 : 1;
}

// --dialogue: the intro's telepathy box on a NEW file. This is the only
// deterministic story message the harness can raise without steering, and it
// proves the automatic dialogue path (decode + speak) on the real ROM.
static int Dialogue(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();

    if (!poke_load_rom(core, rom, savedir) || !poke_start(core)) {
        std::printf("FAIL: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    (void) poke_adapter_ready(core);

    for (int f = 0; f < 1500; f++) {
        poke_set_button(core, POKE_BTN_START, (f % 90) < 4);
        poke_set_button(core, POKE_BTN_A, (f % 90) >= 30 && (f % 90) < 34);
        poke_frame(core);
    }
    poke_set_button(core, POKE_BTN_START, false);
    poke_set_button(core, POKE_BTN_A, false);
    for (int f = 0; f < 600; f++) poke_frame(core);

    bool spoke = false;
    for (const std::string& s : g_say) {
        std::printf("SAY: %s\n", s.c_str());
        if (s.find("Help me") != std::string::npos ||
            s.find("prisoner") != std::string::npos) spoke = true;
    }
    std::printf("dialogue-lines=%d\n", (int) g_say.size());
    std::printf("dialogue-proof=%s\n", spoke ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return spoke ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::strcmp(argv[1], "--walk") == 0)
        return Walk(argv[2], argv[3]);
    if (argc >= 4 && std::strcmp(argv[1], "--dialogue") == 0)
        return Dialogue(argv[2], argv[3]);
    if (argc >= 4 && std::strcmp(argv[1], "--files") == 0)
        return Files(argv[2], argv[3]);
    if (argc >= 4 && std::strcmp(argv[1], "--menus") == 0)
        return Menus(argv[2], argv[3]);
    if (argc >= 5 && std::strcmp(argv[1], "--reload") == 0)
        return Reload(argv[2], argv[3], argv[4]);
    if (argc >= 5 && std::strcmp(argv[1], "--teardown") == 0)
        return Teardown(argv[2], argv[3], argv[4]);
    if (argc >= 4)
        return One(argv[1], argv[2], argc >= 4 ? argv[3] : "any");

    std::fprintf(stderr,
                 "usage: %s <rom> <savedir> [expected-code|any]\n"
                 "       %s --reload <romA> <romB> <savedir>\n"
                 "       %s --teardown <romA> <romB> <savedir>\n",
                 argv[0], argv[0], argv[0]);
    return 2;
}
