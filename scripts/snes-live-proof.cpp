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
    bool guide_objective = false;
    bool guide_route = false;
    for (const std::string& s : g_say) {
        std::printf("SAY: %s\n", s.c_str());
        if (s.rfind("Guide: ", 0) == 0) {
            bearings++;
            if (s.find("Kakariko Village") != std::string::npos) guide_objective = true;
            if (s.find("Route:") != std::string::npos) guide_route = true;
        }
    }
    std::printf("guide-lines=%d\n", bearings);
    std::printf("guide-objective=%s\n", guide_objective ? "YES" : "NO");
    std::printf("guide-route=%s\n", guide_route ? "YES" : "NO");
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
    return (spoke_outside && bearings >= 2 && objective_spoke && guide_objective && guide_route) ? 0 : 1;
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

// --darkobj: the dark-world objective on the real game. The save directory
// given to this mode is a CRAFTED COPY of the public save whose slot 1 has
// sram_progress_indicator 3 (Agahnim down) and three crystal bits set, with
// the file checksum recomputed by the game's own rule (select_file.c:
// word 0x27F makes the first 0x280 words sum to 0x5A5A). The fixture and the
// golden save are never written. Link still stands at his house in the
// light world, so the Objective must name the maidens stage, the crystals
// held and remaining, and the nearest remaining dungeon as lying in the
// dark world.
static int DarkObj(const char* rom, const char* savedir)
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
    std::printf("outside-spoken=%d\n", (int) spoke_outside);

    g_say.clear();
    poke_command(core, 42);
    for (int f = 0; f < 400; f++) poke_frame(core);
    bool maidens = false, held = false, remaining = false, nearest = false;
    for (const std::string& s : g_say) {
        if (s.rfind("Objective:", 0) != 0) continue;
        if (s.find("Rescue the seven maidens.") != std::string::npos) maidens = true;
        if (s.find("Three of seven crystals held.") != std::string::npos) held = true;
        if (s.find("Remaining: Thieves' Town, Ice Palace, Misery Mire, Turtle Rock.") != std::string::npos) remaining = true;
        if (s.find("Nearest remaining: Thieves' Town, in the dark world.") != std::string::npos) nearest = true;
    }
    bool ok = spoke_outside && maidens && held && remaining && nearest;
    std::printf("maidens=%d held=%d remaining=%d nearest=%d\n",
                (int) maidens, (int) held, (int) remaining, (int) nearest);
    std::printf("darkobj-proof=%s\n", ok ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return ok ? 0 : 1;
}


/* --dungeon: the in-dungeon proof. A state saved at the Eastern Palace
 * door (the probe's --dungeon walk steers Link there on the real game)
 * is loaded through the app path, and the adapter must say where he is
 * and give the guide's first indoor route - the big chest route from
 * the entrance hall, computed from the room graph. */
static int ProofDungeon(const char* rom, const char* savedir, const char* state)
{
    std::printf("== live proof: inside the Eastern Palace\n");
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    g_say.clear();
    if (!poke_load_rom(core, rom, savedir) || !poke_start(core)) {
        std::printf("FAIL: %s\n", poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    if (!poke_load_state(core, state)) {
        std::printf("FAIL: poke_load_state(%s): %s\n", state, poke_last_error(core));
        poke_destroy(core);
        return 1;
    }
    for (int f = 0; f < 600; f++) poke_frame(core);   /* the entrance settles */
    bool rdy = poke_adapter_ready(core);
    std::printf("adapter ready inside the dungeon: %s\n", rdy ? "YES" : "NO");
    if (!rdy) { poke_destroy(core); return 1; }

    g_say.clear();
    poke_command(core, 0);   /* WhereAmI */
    for (int f = 0; f < 400; f++) poke_frame(core);
    bool inPalace = false;
    for (const std::string& s : g_say)
        if (s.find("Inside Eastern Palace") != std::string::npos) inPalace = true;

    g_say.clear();
    poke_command(core, 41);  /* Guide */
    for (int f = 0; f < 400; f++) poke_frame(core);
    bool routed = false;
    for (const std::string& s : g_say)
        if (s.find("Guide: Big Chest. Route: north through the shutter door, "
                   "then north through the door into the big chest room.") != std::string::npos)
            routed = true;

    std::printf("dungeon-proof=%s\n", (inPalace && routed) ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return (inPalace && routed) ? 0 : 1;
}


// --autowalk: the auto-walk bench on the real game. Boots into the save,
// walks out of the house, steps off the patrol road, turns the setting
// on, and walks Link to Link's House (stage A: guide, start, arrival,
// hold, arrival line — green). Stage B rides to Hyrule Castle with a
// retreat-retry advance, proves the player-press interrupt (instant,
// silent) on the Kakariko walk's first leg, and then attempts the
// transport ride to the village and the castle route's ledge stop.
// MEASURED BLOCKER (2026-11-22): the transport's route from the only
// startable castle-grounds component runs the patrolled south road,
// and Link's health does not survive the crossings the retry loops
// need — stage B's transport and ledge verdicts are still NO; see
// the record doc and the staging notes below. 44 is
// oga::Command::WalkToggle, the same number the apps send; 6 is
// Command::DumpState.
//
// The bench drives by STATE, not by speech: the announcement queue lags
// hundreds of frames behind a long Guide line, and a soldier patrols
// the road south of the house — a Link left standing on it while the
// bench waits for a stop line to arrive is dead before it lands
// (measured). Health, position and area come from DumpState samples
// every 30 frames; speech is read afterwards, for the verdicts.
static std::vector<std::string> g_log;
static void OnLog(const char* t, void*) {
    if (t && *t) { g_log.push_back(t); std::printf("LOG: %s\n", t); }
}

static bool AwSaid(size_t mark, const char* needle)
{
    for (size_t i = mark; i < g_say.size(); i++)
        if (g_say[i].find(needle) != std::string::npos) return true;
    return false;
}

/* Run frames until one of the needles is spoken (returns its index) or
 * the budget runs out (returns -1). Safe places only: this waits on the
 * speech queue, which lags. */
static int AwRunUntil(PokeCore* core, size_t mark, const char* const* needles,
                      int count, int budget)
{
    for (int f = 0; f < budget; f += 50) {
        for (int i = 0; i < 50; i++) poke_frame(core);
        for (size_t s = mark; s < g_say.size(); s++)
            for (int n = 0; n < count; n++)
                if (g_say[s].find(needles[n]) != std::string::npos) return n;
    }
    return -1;
}

/* One DumpState sample: position, screen, health (eighths) and area,
 * parsed from the dump's single crc line. Any out-param may be null. */
static bool AwPos(PokeCore* core, int* x, int* y, int* screen,
                  int* health, int* area)
{
    g_log.clear();
    poke_command(core, 6);   /* DumpState (raw 6 in the Command enum) */
    for (const std::string& s : g_log) {
        if (s.rfind("crc:", 0) != 0) continue;
        auto grab = [&](const char* key, int* out) {
            const char* p = std::strstr(s.c_str(), key);
            if (p && out) std::sscanf(p + std::strlen(key), "%d", out);
        };
        if (x) *x = -1;
        if (y) *y = -1;
        grab(" x=", x); grab(" y=", y); grab(" screen=", screen);
        grab("health=", health); grab(" area=", area);
        return x ? (*x >= 0) : true;
    }
    std::printf("pos-fail lines=%d\n", (int) g_log.size());
    return false;
}

static bool AwGuideTo(PokeCore* core, const char* name)
{
    for (int p = 0; p < 9; p++) {
        size_t mark = g_say.size();
        poke_command(core, 41);
        for (int f = 0; f < 450; f += 25) {
            for (int i = 0; i < 25; i++) poke_frame(core);
            for (size_t i = mark; i < g_say.size(); i++)
                if (g_say[i].rfind("Guide: ", 0) == 0 &&
                    g_say[i].find(name) != std::string::npos) return true;
        }
    }
    return false;
}

/* Ride one WalkToggle press by state. Watches samples every 30 frames:
 *   1  = displaced at least 80px from the press point (under way)
 *   2  = the target area was reached
 *   0  = the walk ended (health fell: a hurt stop; or position froze
 *        for 150 frames: a blocked stop, a ledge gate, or a refusal)
 *  -1  = the sample budget ran out
 * When stopWhenFrozen is set, a freeze returns 3 instead of 0 so the
 * caller can ask speech which stop it was (the ledge gate leaves the
 * walk armed, not ended). */
static int AwRide(PokeCore* core, int targetArea, int budget, bool stopWhenFrozen,
                  bool displacementEnds)
{
    int bx = 0, by = 0, bs = 0, bh = -1, ba = 0;
    AwPos(core, &bx, &by, &bs, &bh, &ba);
    if (targetArea >= 0 && ba == targetArea) return 2;
    poke_command(core, 44);
    int lastH = bh, frozen = 0, px = bx, py = by;
    for (int f = 0; f < budget; f += 30) {
        for (int i = 0; i < 30; i++) poke_frame(core);
        int nx = 0, ny = 0, ns = 0, nh = -1, na = 0;
        if (!AwPos(core, &nx, &ny, &ns, &nh, &na)) continue;
        if (targetArea >= 0 && na == targetArea) return 2;
        if (nh >= 0 && lastH >= 0 && nh < lastH) return 0;
        if (nh >= 0) lastH = nh;
        if (displacementEnds &&
            (std::abs(nx - bx) + std::abs(ny - by)) >= 80) return 1;
        if (nx == px && ny == py) {
            frozen += 30;
            /* 600, not 150: the walker's own recovery (stall,
             * exclude the tile, re-plan — up to three times, then a
             * route re-plan) stands still for ~500 frames while
             * still very much alive, and a ride that calls that
             * "ended" toggles a working walk off with its next
             * press. A walk that truly ended stays frozen past any
             * threshold, so patience costs only time. */
            if (frozen >= 600) return stopWhenFrozen ? 3 : 0;
        } else {
            frozen = 0;
        }
        px = nx; py = ny;
    }
    return -1;
}

static int AutoWalkStageA(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    poke_set_log_callback(core, OnLog, nullptr);
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

    poke_set_alttp_auto_walk(core, true);

    /* The geography drives the bench's shape, all of it measured —
     * the patrols by health trails across many runs, the steering
     * geometry by an offline component analysis of the shipped
     * graph's own tile data, and the walker's behaviour at seams
     * and bodies by its own diagnostics. A soldier patrols the road
     * in front of the house and hits a standing Link there every
     * ~150 frames, so nothing waits on that road: crossings are
     * driven on foot, waits happen on quiet screens, and the walker
     * is handed only ground it has been measured to hold. Stage A
     * rides one leg east to Link's House (the patrol is a moving
     * target; the walk's persistence gains ground between hits).
     * Stage B reaches the ledge screen through the sanctuary
     * screens — the one steer-connected route into the village's
     * area from the west that the south approach's cliff traps do
     * not guard — and the interrupt is demonstrated on its quiet
     * first leg. Everything is state-driven; speech is read only
     * for verdicts, where the queue has landed. Health is the
     * budget: 24 eighths, a hit costs 4, starting refuses at 8. */

    /* A closed-loop drive to a point, used for every bench move
     * that is not the walker's own. */
    auto driveTo = [&](int tx, int ty, int ticks, int stopScreen) {
        for (int tick = 0; tick < ticks; tick++) {
            int cx = 0, cy = 0, cs = 0, ch = -1;
            if (!AwPos(core, &cx, &cy, &cs, &ch, nullptr)) break;
            if (stopScreen >= 0 && cs == stopScreen) break;
            if (std::abs(cx - tx) <= 12 && std::abs(cy - ty) <= 12) break;
            if (ch >= 0 && ch <= 4) break;
            poke_set_button(core, POKE_BTN_LEFT, cx > tx + 6);
            poke_set_button(core, POKE_BTN_RIGHT, cx < tx - 6);
            poke_set_button(core, POKE_BTN_UP, cy > ty + 6);
            poke_set_button(core, POKE_BTN_DOWN, cy < ty - 6);
            for (int f = 0; f < 30; f++) poke_frame(core);
        }
        poke_set_button(core, POKE_BTN_LEFT, false);
        poke_set_button(core, POKE_BTN_RIGHT, false);
        poke_set_button(core, POKE_BTN_UP, false);
        poke_set_button(core, POKE_BTN_DOWN, false);
    };
    auto report = [&](const char* tag) {
        int ex = 0, ey = 0, es = 0, eh = -1;
        AwPos(core, &ex, &ey, &es, &eh, nullptr);
        std::printf("%s=(%d,%d) screen=%d health=%d\n", tag, ex, ey, es, eh);
    };

    /* Aim the Guide at Link's House from the swamp's north strip,
     * the one spot measured safe to stand on for ~1000 frames (the
     * road's edge and the field both have a patrol that arrives
     * within that span — measured, twice). Seven presses around
     * the cycle from this save's objective, at a fixed cadence
     * (the cycle advances when each command is processed). Then
     * back north and across the patrol's screen on foot, and stage
     * A's walk starts at the road's west edge, so its one leg runs
     * straight east along the road (from the field above, the
     * shortest path first routes west into the posted body's
     * ground — measured). */
    for (int i = 0; i < 150; i++) { poke_set_button(core, POKE_BTN_DOWN, true); poke_frame(core); }
    poke_set_button(core, POKE_BTN_DOWN, false);
    for (int i = 0; i < 30; i++) poke_frame(core);
    report("south-to");
    for (int p = 0; p < 7; p++) {
        poke_command(core, 41);
        for (int f = 0; f < 100; f++) poke_frame(core);
    }
    for (int f = 0; f < 300; f++) poke_frame(core);
    for (int i = 0; i < 150; i++) { poke_set_button(core, POKE_BTN_UP, true); poke_frame(core); }
    poke_set_button(core, POKE_BTN_UP, false);
    driveTo(2000, 2936, 30, 43);
    report("crossed-to");
    size_t sayMark = g_say.size();

    /* Stage A: ride east to the house; arrival is its area. */
    bool arrived = false;
    int retries = 0;
    for (int attempt = 0; attempt < 12 && !arrived; attempt++) {
        int hh = -1;
        AwPos(core, nullptr, nullptr, nullptr, &hh, nullptr);
        if (hh >= 0 && hh <= 8) {
            std::printf("arrive: health %d, no further starts possible\n", hh);
            break;
        }
        int r = AwRide(core, 44, 12000, false, false);
        std::printf("arrive-attempt=%d r=%d\n", attempt, r);
        if (r == 2) arrived = true;
        else if (r == 0) retries++;
        else if (r < 0) break;
    }
    std::printf("walk-arrived=%s retries=%d\n", arrived ? "YES" : "NO", retries);
    for (int i = 0; i < 400; i++) poke_frame(core);
    bool guided = AwSaid(0, "Guide: Link's House.");
    std::printf("guide-house=%s\n", guided ? "YES" : "NO");
    bool started = AwSaid(sayMark, "Auto walk: Link's House.");
    std::printf("walk-start=%s\n", started ? "YES" : "NO");
    int xa = 0, ya = 0, sa = 0, xb = 0, yb = 0, sb = 0;
    bool holds = false;
    if (arrived && AwPos(core, &xa, &ya, &sa, nullptr, nullptr)) {
        for (int i = 0; i < 300; i++) poke_frame(core);
        holds = AwPos(core, &xb, &yb, &sb, nullptr, nullptr) && xa == xb && ya == yb;
    }
    std::printf("walk-holds-at-arrival=%s\n", holds ? "YES" : "NO");
    bool arrivalLine = AwSaid(sayMark, "Outside, Link's House.");
    std::printf("arrival-line=%s\n", arrivalLine ? "YES" : "NO");
    bool okA = guided && started && arrived && holds && arrivalLine;
    std::printf("stage-a-proof=%s\n", okA ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return okA ? 0 : 1;
}

static int AutoWalkStageB(const char* rom, const char* savedir)
{
    PokeCore* core = poke_create();
    if (!core) { std::printf("FAIL: no core\n"); return 1; }
    poke_set_speech_callback(core, OnSpeak, nullptr);
    poke_set_log_callback(core, OnLog, nullptr);
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

    poke_set_alttp_auto_walk(core, true);

    /* A closed-loop drive to a point, used for every bench move
     * that is not the walker's own. */
    auto driveTo = [&](int tx, int ty, int ticks, int stopScreen) {
        for (int tick = 0; tick < ticks; tick++) {
            int cx = 0, cy = 0, cs = 0, ch = -1;
            if (!AwPos(core, &cx, &cy, &cs, &ch, nullptr)) break;
            if (stopScreen >= 0 && cs == stopScreen) break;
            if (std::abs(cx - tx) <= 12 && std::abs(cy - ty) <= 12) break;
            if (ch >= 0 && ch <= 4) break;
            poke_set_button(core, POKE_BTN_LEFT, cx > tx + 6);
            poke_set_button(core, POKE_BTN_RIGHT, cx < tx - 6);
            poke_set_button(core, POKE_BTN_UP, cy > ty + 6);
            poke_set_button(core, POKE_BTN_DOWN, cy < ty - 6);
            for (int f = 0; f < 30; f++) poke_frame(core);
        }
        poke_set_button(core, POKE_BTN_LEFT, false);
        poke_set_button(core, POKE_BTN_RIGHT, false);
        poke_set_button(core, POKE_BTN_UP, false);
        poke_set_button(core, POKE_BTN_DOWN, false);
    };
    auto report = [&](const char* tag) {
        int ex = 0, ey = 0, es = 0, eh = -1;
        AwPos(core, &ex, &ey, &es, &eh, nullptr);
        std::printf("%s=(%d,%d) screen=%d health=%d\n", tag, ex, ey, es, eh);
    };


    /* Stage B's approach. The Guide is cycled to the castle on
     * the swamp strip (the proven standing spot), and the castle
     * walk starts at the doorstep itself: one leg north, through
     * the road junction's guard — the advance below rides it one
     * press at a time with retreats between attempts, because an
     * immediate re-press after a hurt stop steps straight back
     * into the body that hit Link (measured at this doorstep).
     * From the
     * castle, the sanctuary transport below takes the village
     * route that the steer graph verifies. */
    for (int i = 0; i < 150; i++) { poke_set_button(core, POKE_BTN_DOWN, true); poke_frame(core); }
    poke_set_button(core, POKE_BTN_DOWN, false);
    for (int i = 0; i < 30; i++) poke_frame(core);
    report("south-to");
    for (int p = 0; p < 9; p++) {
        poke_command(core, 41);
        for (int f = 0; f < 100; f++) poke_frame(core);
    }
    for (int i = 0; i < 150; i++) { poke_set_button(core, POKE_BTN_UP, true); poke_frame(core); }
    poke_set_button(core, POKE_BTN_UP, false);
    report("doorstep-at");

    int branch = 0;
    /* The advance rides one press at a time, and a hurt stop ends
     * the attempt at once: the body that hit Link is still directly
     * ahead of him, and an immediate re-press steps straight back
     * into contact (measured at the doorstep: four hits in ~100
     * frames, 24 -> 8, then death standing at the refusal
     * threshold). Between attempts the bench retreats to the
     * swamp strip south of the road — the spot stage B's Guide
     * cycling already proved safe to stand on — waits out a
     * patrol phase, and walks back to the doorstep. */
    for (int attempt = 0; attempt < 5 && branch == 0; attempt++) {
        int hh = -1;
        AwPos(core, nullptr, nullptr, nullptr, &hh, nullptr);
        if (hh >= 0 && hh <= 8) break;
        poke_command(core, 44);
        int px = 0, py = 0, ps = 0, ph = -1, frozen = 0;
        AwPos(core, &px, &py, &ps, &ph, nullptr);
        for (int tick = 0; tick < 300 && branch == 0; tick++) {
            for (int f = 0; f < 30; f++) poke_frame(core);
            int nx = 0, ny = 0, ns = 0, nh = -1, na = 0;
            if (!AwPos(core, &nx, &ny, &ns, &nh, &na)) break;
            if (na == 27) { branch = 2; break; }
            if (nh >= 0 && ph >= 0 && nh < ph) break;   /* hurt stop */
            if (nx == px && ny == py) { if (++frozen >= 8) break; }
            else frozen = 0;
            px = nx; py = ny; ps = ns;
            if (nh >= 0) ph = nh;
        }
        std::printf("castle-advance: attempt=%d branch=%d\n", attempt, branch);
        if (branch == 0 && attempt < 4) {
            driveTo(2256, 3080, 40, -1);
            for (int f = 0; f < 300; f++) poke_frame(core);
            driveTo(2256, 2944, 40, -1);
            report("retry-at");
        }
    }
    report("main-at");

    /* The castle advance ends the moment area 27 is entered — on
     * the entrance steps, whose tiles belong to no steering
     * component (offline graph analysis: comp -1), so the start
     * gate refuses there ("Auto walk can't start here.", measured:
     * Link frozen at (2376,2528) through nine presses, health
     * untouched). The graph's Kakariko route does not leave from
     * the steps at all: it exits the castle grounds west from the
     * grounds screen's middle third. So Link is driven across the
     * grounds to that west component before the Kakariko sequence. */
    driveTo(2100, 2300, 80, -1);
    report("grounds-at");

    /* Aim at Kakariko and ride the sanctuary route: north past the
     * castle, west, north, west, and south into the village's area
     * (its north screen). The interrupt is demonstrated on the
     * first, quiet leg. */
    bool guidedK = AwGuideTo(core, "Kakariko Village");
    std::printf("guide-kakariko=%s\n", guidedK ? "YES" : "NO");

    /* The grounds are patchy steering ground: both the entrance
     * steps and the west fence line refuse the start gate. Search
     * a grid of grounds spots for one the walker accepts, judged
     * by STATE (the speech queue lags hundreds of frames behind a
     * start line): a press that moves Link is a start, cancelled
     * at once by a real player press; a frozen press is a refusal
     * or an instant wedge. The graph puts the Kakariko route's
     * west exit in the grounds screen's west component, around
     * tile (2,11) — the spots cluster there, with the south edge
     * (measured to start, then wedge on the entrance tiles) as a
     * control. */
    {
        static const int kSpots[][2] = {
            {2600, 2400}, {2800, 2400}, {2600, 2200}, {2900, 2300},
            {2800, 2550}, {2500, 2550}, {2376, 2547}, {2088, 2232},
        };
        bool grounded = false;
        for (const auto& sp : kSpots) {
            driveTo(sp[0], sp[1], 60, -1);
            int bx = 0, by = 0;
            AwPos(core, &bx, &by, nullptr, nullptr, nullptr);
            poke_command(core, 44);
            bool moved = false;
            for (int t = 0; t < 12 && !moved; t++) {
                for (int f = 0; f < 30; f++) poke_frame(core);
                int nx = 0, ny = 0;
                if (AwPos(core, &nx, &ny, nullptr, nullptr, nullptr) &&
                    std::abs(nx - bx) + std::abs(ny - by) >= 20) moved = true;
            }
            if (moved) {
                for (int i = 0; i < 6; i++) { poke_set_button(core, POKE_BTN_RIGHT, true); poke_frame(core); }
                poke_set_button(core, POKE_BTN_RIGHT, false);
                for (int f = 0; f < 60; f++) poke_frame(core);
                grounded = true;
                report("grounded-at");
                break;
            }
        }
        std::printf("walk-grounded=%s\n", grounded ? "YES" : "NO");
    }
    size_t sayMarkB = g_say.size();
    bool underWay = false, interrupt = false;
    int startsAtPress = 0;
    {
        poke_command(core, 44);
        int px = 0, py = 0, ps = 0, ph = -1, frozen = 0, restarts = 0;
        int ax = 0, ay = 0;
        AwPos(core, &px, &py, &ps, &ph, nullptr);
        ax = px; ay = py;
        bool live = false;
        for (int tick = 0; tick < 200 && !underWay; tick++) {
            for (int f = 0; f < 30; f++) poke_frame(core);
            int nx = 0, ny = 0, ns = 0, nh = -1, na = 0;
            if (!AwPos(core, &nx, &ny, &ns, &nh, &na)) break;
            bool moved = (nx != px || ny != py);
            if (na == 24) { underWay = true; live = true; break; }
            if (nh >= 0 && ph >= 0 && nh < ph) live = false;
            if (moved) { live = true; frozen = 0; }
            else if (++frozen >= 8) live = false;
            if (!live && restarts < 12) {
                poke_command(core, 44);
                restarts++;
                frozen = 0;
            }
            int disp = std::abs(nx - ax) + std::abs(ny - ay);
            if (live && disp >= 250) underWay = true;
            px = nx; py = ny; ps = ns;
            if (nh >= 0) ph = nh;
        }
        std::printf("advance: screen=%d restarts=%d underway=%d\n",
                    ps, restarts, (int) underWay);
    }
    if (underWay) {
        /* Player presses win instantly and silently: inject a real
         * player press through the player entry point and confirm by
         * position — a walking Link covers ~160px between the two
         * samples, a cancelled one does not move at all. Health must
         * be unchanged across the window, so a knockback cannot fake
         * the freeze. The silence verdict is computed after the
         * transport ride, when every line has landed: the ride's
         * start line is the startsAtPress-th start line, and a stop
         * belonging to that walk would be delivered after it and
         * before the next ride's start line. */
        startsAtPress = 0;
        for (size_t i = 0; i < g_say.size(); i++)
            if (g_say[i] == "Auto walk: Kakariko Village.") startsAtPress++;
        int h0 = -1;
        AwPos(core, nullptr, nullptr, nullptr, &h0, nullptr);
        for (int i = 0; i < 6; i++) { poke_set_button(core, POKE_BTN_RIGHT, true); poke_frame(core); }
        poke_set_button(core, POKE_BTN_RIGHT, false);
        for (int i = 0; i < 120; i++) poke_frame(core);
        int x2 = 0, y2 = 0, s2 = 0, h2 = -1;
        bool have2 = AwPos(core, &x2, &y2, &s2, &h2, nullptr);
        for (int i = 0; i < 120; i++) poke_frame(core);
        int x3 = 0, y3 = 0, s3 = 0, h3 = -1;
        bool have3 = AwPos(core, &x3, &y3, &s3, &h3, nullptr);
        interrupt = have2 && have3 && x2 == x3 && y2 == y3 &&
                    h2 == h0 && h3 == h0;
        std::printf("interrupt-check: starts=%d frozen=%d health=%d->%d->%d\n",
                    startsAtPress, (int) interrupt, h0, h2, h3);
    }
    std::printf("walk-moving=%s\n", underWay ? "YES" : "NO");
    std::printf("interrupt-cancel=%s\n", interrupt ? "YES" : "NO");

    bool transported = false;
    for (int attempt = 0; attempt < 10 && !transported; attempt++) {
        int hh = -1;
        AwPos(core, nullptr, nullptr, nullptr, &hh, nullptr);
        if (hh >= 0 && hh <= 8) break;
        /* Ride one press by state, with a speech-aware freeze: a
         * freeze with a stop or refusal line landed means the walk
         * is over (safe to re-press); a freeze with no line may be
         * the walker's own stall recovery, so it gets the long
         * patience AwRide uses. The plain 600-frame patience is
         * fatal on this road — a refused press leaves Link standing
         * in the patrol's path for all of it (measured: a full
         * health bar ground down standing still). */
        size_t pressMark = g_say.size();
        int bx = 0, by = 0, bh = -1, ba = 0;
        AwPos(core, &bx, &by, nullptr, &bh, &ba);
        poke_command(core, 44);
        int lastH = bh, frozen = 0, px = bx, py = by, r = -1;
        for (int f = 0; f < 12000 && r < 0; f += 30) {
            for (int k = 0; k < 30; k++) poke_frame(core);
            int nx = 0, ny = 0, nh = -1, na = 0;
            if (!AwPos(core, &nx, &ny, nullptr, &nh, &na)) continue;
            if (na == 24) r = 2;
            else if (nh >= 0 && lastH >= 0 && nh < lastH) r = 0;
            else if (nx == px && ny == py) {
                frozen += 30;
                bool lineLanded = AwSaid(pressMark, "Auto walk stopped") ||
                                  AwSaid(pressMark, "Auto walk can't start") ||
                                  AwSaid(pressMark, "Auto walk: too hurt") ||
                                  AwSaid(pressMark, "Auto walk off");
                if (frozen >= 900 || (frozen >= 240 && lineLanded)) r = 0;
            } else frozen = 0;
            if (nh >= 0) lastH = nh;
            px = nx; py = ny;
        }
        if (r < 0) r = -1;
        std::printf("transport-attempt=%d r=%d\n", attempt, r);
        if (r == 2) transported = true;
        else if (r < 0) break;
        else if (attempt < 9) {
            /* Between attempts: step south off the road to the
             * swamp strip, wait out a patrol phase, and re-press
             * from a fresh spot west of where the walk died. */
            int cx = 0, cy = 0;
            if (AwPos(core, &cx, &cy, nullptr, nullptr, nullptr)) {
                driveTo(cx, 3080, 30, -1);
                for (int f = 0; f < 300; f++) poke_frame(core);
                driveTo(cx - 60, cy, 30, -1);
            }
        }
    }
    std::printf("transport-arrived=%s\n", transported ? "YES" : "NO");
    for (int i = 0; i < 400; i++) poke_frame(core);
    bool startedB = AwSaid(sayMarkB, "Auto walk: Kakariko Village.");
    std::printf("walk-start-b=%s\n", startedB ? "YES" : "NO");
    bool silent = false;
    {
        int seen = 0, idx = -1;
        for (int i = 0; i < (int) g_say.size(); i++)
            if (g_say[i] == "Auto walk: Kakariko Village." && ++seen == startsAtPress)
                idx = i;
        if (idx >= 0) {
            silent = true;
            for (int i = idx + 1; i < (int) g_say.size(); i++) {
                if (g_say[i] == "Auto walk: Kakariko Village.") break;
                if (g_say[i].find("Auto walk stopped") != std::string::npos ||
                    g_say[i].find("Auto walk off") != std::string::npos)
                    silent = false;
            }
        }
    }
    std::printf("interrupt-silent=%s\n", silent ? "YES" : "NO");

    /* Settle into the village proper, where standing is safe. */
    {
        int ss = 0;
        AwPos(core, nullptr, nullptr, &ss, nullptr, nullptr);
        if (ss == 24 || ss == 25) {
            driveTo(890, 2200, 40, 33);
            report("town-at");
        }
    }

    /* Stage B: the ride north and east to the castle must stop
     * before the ledge. Guide cycling happens in town. The ride
     * watches for the freeze that is not an ending: at the ledge
     * gate the walk stays armed, so speech is asked which stop it
     * was, and a second press confirms the hop. */
    bool guidedB = AwGuideTo(core, "Hyrule Castle");
    std::printf("guide-castle=%s\n", guidedB ? "YES" : "NO");
    bool costlyStop = false, costlyArrived = false;
    int costlyScreen = -1;
    for (int attempt = 0; attempt < 8 && !costlyArrived; attempt++) {
        int r = AwRide(core, 27, 12000, true, false);
        std::printf("castle-attempt=%d r=%d\n", attempt, r);
        if (r == 2) { costlyArrived = true; break; }
        if (r == 3) {
            size_t m2 = g_say.size();
            const char* ln[] = { "Ledge ahead. Press auto walk again to hop down." };
            if (AwRunUntil(core, m2, ln, 1, 600) == 0) {
                costlyStop = true;
                int cxx = 0, cyy = 0;
                AwPos(core, &cxx, &cyy, &costlyScreen, nullptr, nullptr);
                std::printf("costly-stop=%s screen=%d\n",
                            costlyStop ? "YES" : "NO", costlyScreen);
            }
            continue;
        }
        if (r < 0) break;
    }
    if (!costlyStop) std::printf("costly-stop=NO screen=%d\n", costlyScreen);
    std::printf("costly-arrived=%s\n", costlyArrived ? "YES" : "NO");
    bool okB = guidedK && startedB && underWay && interrupt && silent &&
               transported && guidedB && costlyStop && costlyArrived;
    std::printf("stage-b-proof=%s\n", okB ? "YES" : "NO");
    poke_stop(core);
    poke_destroy(core);
    return okB ? 0 : 1;
}

static int AutoWalk(const char* rom, const char* savedir)
{
    int a = AutoWalkStageA(rom, savedir);
    int b = AutoWalkStageB(rom, savedir);
    bool ok = (a == 0 && b == 0);
    std::printf("autowalk-proof=%s\n", ok ? "YES" : "NO");
    return ok ? 0 : 1;
}










int main(int argc, char** argv)
{
    if (argc >= 4 && std::strcmp(argv[1], "--autowalk") == 0)
        return AutoWalk(argv[2], argv[3]);
    if (argc >= 4 && std::strcmp(argv[1], "--walk") == 0)
        return Walk(argv[2], argv[3]);
    if (argc >= 4 && std::strcmp(argv[1], "--darkobj") == 0)
        return DarkObj(argv[2], argv[3]);
    if (argc >= 5 && std::strcmp(argv[1], "--dungeon") == 0)
        return ProofDungeon(argv[2], argv[3], argv[4]);
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
