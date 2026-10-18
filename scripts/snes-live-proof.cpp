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

int main(int argc, char** argv)
{
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
