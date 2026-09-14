/*
 * probe.cpp — a scripted, snapshot-capable NDS probe for reverse engineering.
 *
 * Generic on purpose: this is the instrument the game adapters are built with.
 * It boots a ROM, follows a PLAN of timed button presses, and takes two kinds of
 * checkpoint:
 *
 *   SHOT  <frame> <file.ppm>   what the screen looked like
 *   SNAP  <frame> <file.ram>   a full copy of Main RAM + the ARM9 PC
 *
 * The pair is what makes findings trustworthy: a RAM diff says NOTHING without a
 * screenshot proving what the game was doing between the two snapshots, and a
 * screenshot alone cannot tell you where a value lives. Diff the snapshots, then
 * look at the screenshots that bracket them.
 *
 * PLAN format (one directive per line, '#' comments):
 *   KEY  <frame> <BUTTON> <0|1>   press/release a DS button
 *   HOT  <frame> <letter>  <0|1>  press/release an accessibility hotkey
 *   TOUCH <frame> <x> <y> <0|1>   touch panel
 *   SHOT <frame> <path>
 *    SNAP <frame> <path>
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

struct Event {
    long frame;
    enum Kind { KEY, HOT, TOUCH, SHOT, SNAP } kind;
    char arg[512];
    int button = 0, down = 0, x = 0, y = 0;
};

static int ButtonId(const char* n)
{
    static const char* names[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L","X","Y"};
    for (int i = 0; i < 12; i++) if (strcasecmp(n, names[i]) == 0) return i;
    return -1;
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <frames> [plan.txt]\n", argv[0]); return 2; }
    const char* rom = argv[1];
    long frames = atol(argv[2]);
    const char* planPath = (argc > 3) ? argv[3] : nullptr;

    std::vector<Event> evs;
    if (planPath) {
        FILE* f = fopen(planPath, "r");
        if (!f) { fprintf(stderr, "!! no plan file %s\n", planPath); return 2; }
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            char* c = strchr(line, '#'); if (c) *c = 0;
            char cmd[32] = {0};
            Event e; memset(&e, 0, sizeof(e));
            if (sscanf(line, "%31s %ld", cmd, &e.frame) != 2) continue;
            if (!strcasecmp(cmd, "KEY")) {
                char btn[32] = {0};
                if (sscanf(line, "%*s %ld %31s %d", &e.frame, btn, &e.down) != 3) continue;
                e.kind = Event::KEY; e.button = ButtonId(btn);
                if (e.button < 0) { fprintf(stderr, "!! unknown button %s\n", btn); continue; }
            } else if (!strcasecmp(cmd, "HOT")) {
                char k[8] = {0};
                if (sscanf(line, "%*s %ld %7s %d", &e.frame, k, &e.down) != 3) continue;
                e.kind = Event::HOT; strncpy(e.arg, k, sizeof(e.arg) - 1);
            } else if (!strcasecmp(cmd, "TOUCH")) {
                if (sscanf(line, "%*s %ld %d %d %d", &e.frame, &e.x, &e.y, &e.down) != 4) continue;
                e.kind = Event::TOUCH;
            } else if (!strcasecmp(cmd, "SHOT") || !strcasecmp(cmd, "SNAP")) {
                char path[512] = {0};
                if (sscanf(line, "%*s %ld %511s", &e.frame, path) != 2) continue;
                e.kind = !strcasecmp(cmd, "SHOT") ? Event::SHOT : Event::SNAP;
                strncpy(e.arg, path, sizeof(e.arg) - 1);
            } else continue;
            evs.push_back(e);
        }
        fclose(f);
        printf("== plan: %zu directives\n", evs.size());
    }

    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);
    {
        static std::string s;
        const char* shim = getenv("PA_SHIM");
        if (shim) { FILE* f = fopen(shim,"rb"); if (f) { char b[65536]; size_t n; while ((n=fread(b,1,sizeof(b),f))>0) s.append(b,n); fclose(f);} }
        // A yielder plus the two commands the adapters will need first.
        s += "\nlocal n=0\nwhile true do n=n+1; emu.frameadvance() end\n";
        poke_set_script(core, s.c_str());
    }
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    const uint32_t RAM_BASE = 0x02000000, RAM_SIZE = 0x400000;
    size_t ei = 0;

    for (long f = 0; f < frames; f++)
    {
        while (ei < evs.size() && evs[ei].frame == f)
        {
            Event& e = evs[ei];
            switch (e.kind) {
            case Event::KEY:   poke_set_button(core, e.button, e.down != 0); break;
            case Event::HOT:   poke_set_hotkey(core, e.arg, e.down != 0); break;
            case Event::TOUCH: poke_touch(core, e.x, e.y, e.down != 0); break;
            case Event::SHOT: {
                for (int screen = 0; screen < 2; screen++) {
                    int w = 0, h = 0;
                    poke_framebuffer(core, screen, &w, &h);
                    const uint8_t* px = poke_framebuffer_ptr(core, screen);
                    if (!px) continue;
                    char path[600];
                    snprintf(path, sizeof(path), "%s", e.arg);
                    if (screen == 1) snprintf(path, sizeof(path), "%s", e.arg); // bottom appended below
                    if (screen == 0) {
                        snprintf(path, sizeof(path), "%s", e.arg);
                    } else {
                        // second file: insert "-bottom" before the extension
                        std::string s2(e.arg);
                        size_t dot = s2.rfind('.');
                        if (dot == std::string::npos) s2 += "-bottom.ppm";
                        else s2 = s2.substr(0, dot) + "-bottom" + s2.substr(dot);
                        snprintf(path, sizeof(path), "%s", s2.c_str());
                    }
                    FILE* o = fopen(path, "wb");
                    if (!o) { printf("[shot] cannot write %s\n", path); break; }
                    fprintf(o, "P6\n%d %d\n255\n", w, h);
                    for (int i = 0; i < w * h; i++) fwrite(px + i * 4, 1, 3, o);
                    fclose(o);
                    printf("[shot] f=%ld %s (%dx%d)\n", f, path, w, h);
                }
                break;
            }
            case Event::SNAP: {
                uint8_t* ram = nds->MainRAM;
                FILE* o = fopen(e.arg, "wb");
                if (!o) { printf("[snap] cannot write %s\n", e.arg); break; }
                fwrite(ram, 1, RAM_SIZE, o);
                fclose(o);
                printf("[snap] f=%ld %s ARM9PC=%08X KeyInput=%03X\n",
                       f, e.arg, nds->ARM9.R[15], (unsigned)(nds->KeyInput & 0xFFF));
                break;
            }
            }
            ei++;
        }
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
    }

    printf("== done frames=%llu\n", poke_frames_completed(core));
    poke_destroy(core);
    return 0;
}
