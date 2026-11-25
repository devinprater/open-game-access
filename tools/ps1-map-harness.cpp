/* ps1-map-harness.cpp — drive a PlayStation disc headless to measure a reader's memory map.
 *
 * Not shipped in any app. It links the same PCSX ReARMed objects that scripts/ps1-core-test.sh
 * builds (scripts/ps1-map-harness.sh does the building) and plays a script of pad presses,
 * saving full 2 MiB RAM dumps and screenshots at named points. The dumps are then diffed by
 * tools/ps1-ramdiff.py. It never copies or writes the disc; give it the path to your own.
 *
 *   ps1-map-harness DISC.cue BIOSDIR OUTDIR SCRIPT
 *
 * Script lines (one per line, # comments):
 *   run N              run N frames
 *   tap BTN [N [H]]    hold BTN (a+b allowed) for H frames (default 12; a d-pad auto-repeats in a
 *                      list if held longer than about 4), release, then run N frames (default 8)
 *   hold BTN N         hold BTN for N frames, release
 *   dump NAME          write OUTDIR/NAME.ram (2 MiB main RAM)
 *   shot NAME          write OUTDIR/NAME.ppm (the video frame)
 *   save NAME / load NAME   whole-machine state in OUTDIR/NAME.state
 *   say TEXT           print TEXT to stdout (progress marker)
 *   adapter on         attach the Mortal Kombat Trilogy reader to the live RAM; from then on every
 *                      frame also runs the reader, and each line it speaks is printed with its frame
 *   cue                print the reader's cue snapshot (beacon distance, beeps) at this frame
 */

#include "ps1_core.h"
#include "adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// The registry lists the Nintendo readers too; stand-ins, as the adapter host tests do.
namespace oga {
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
extern const Adapter kMortalKombatTrilogy;
static void s_frm() {}
static bool s_att(const Host*) { return false; }
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11", "FE", "YFEE", s_att, s_frm, s_cmd, s_rdy, s_det};
const Adapter kGameBoyAdvance         = {"gba", "GBA", "", s_att, s_frm, s_cmd, s_rdy, s_det};
}

static Ps1Core *g_core = nullptr;
static unsigned long long g_frame = 0;
static bool g_adapterOn = false;
static uint8_t hr8 (void*, uint32_t a) { uint32_t v = 0; return ps1_read(g_core, a, 1, &v) ? (uint8_t) v : 0; }
static uint16_t hr16(void*, uint32_t a) { uint32_t v = 0; return ps1_read(g_core, a, 2, &v) ? (uint16_t) v : 0; }
static uint32_t hr32(void*, uint32_t a) { uint32_t v = 0; return ps1_read(g_core, a, 4, &v) ? v : 0; }
static void hspeak(void*, const char *t, bool) { printf("[%llu] SAY: %s\n", g_frame, t); fflush(stdout); }
static void hlog(void*, const char *t) { printf("[%llu] LOG: %s\n", g_frame, t); fflush(stdout); }


static int buttonId(const std::string &n)
{
    static const struct { const char *name; int id; } t[] = {
        {"cross", PS1_BTN_CROSS},   {"x", PS1_BTN_CROSS},      {"circle", PS1_BTN_CIRCLE},
        {"triangle", PS1_BTN_TRIANGLE}, {"square", PS1_BTN_SQUARE}, {"l1", PS1_BTN_L1},
        {"r1", PS1_BTN_R1},         {"l2", PS1_BTN_L2},        {"r2", PS1_BTN_R2},
        {"select", PS1_BTN_SELECT}, {"start", PS1_BTN_START},  {"up", PS1_BTN_UP},
        {"down", PS1_BTN_DOWN},     {"left", PS1_BTN_LEFT},    {"right", PS1_BTN_RIGHT},
    };
    for (auto &e : t) if (n == e.name) return e.id;
    return -1;
}

static std::vector<int> parseButtons(const std::string &s)
{
    std::vector<int> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('+', i);
        if (j == std::string::npos) j = s.size();
        int id = buttonId(s.substr(i, j - i));
        if (id < 0) { fprintf(stderr, "unknown button '%s'\n", s.substr(i, j - i).c_str()); exit(2); }
        out.push_back(id);
        i = j + 1;
    }
    return out;
}

static void frames(Ps1Core *c, int n)
{
    static int16_t audio[8192];
    for (int i = 0; i < n; i++) {
        if (!ps1_frame(c)) { fprintf(stderr, "frame failed: %s\n", ps1_last_error(c)); exit(3); }
        ps1_read_audio(c, audio, 8192);
        g_frame++;
        if (g_adapterOn) oga::kMortalKombatTrilogy.on_frame();
    }
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s DISC BIOSDIR OUTDIR SCRIPT\n", argv[0]); return 2; }
    const std::string out = argv[3];
    Ps1Core *c = ps1_create();
    if (!c) return 3;
    g_core = c;
    ps1_set_bios_dir(c, argv[2]);
    char code[16] = {0};
    if (!ps1_load_disc(c, argv[1], nullptr, code)) {
        fprintf(stderr, "load failed: %s\n", ps1_last_error(c));
        return 3;
    }
    printf("serial=%s hle=%d fps=%.2f\n", code, (int) ps1_using_hle_bios(c), ps1_frame_rate(c));
    if (!ps1_start(c)) { fprintf(stderr, "start failed: %s\n", ps1_last_error(c)); return 3; }

    FILE *sf = fopen(argv[4], "r");
    if (!sf) { fprintf(stderr, "no script\n"); return 2; }
    char line[512];
    while (fgets(line, sizeof line, sf)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, d[64] = {0};
        int n = sscanf(l.c_str(), "%31s %255s %63s %63s", cmd, a, b, d);
        std::string C = cmd;
        if (C == "run" && n >= 2) frames(c, atoi(a));
        else if ((C == "tap" || C == "hold") && n >= 2) {
            std::vector<int> bs = parseButtons(a);
            for (int id : bs) ps1_set_button(c, id, true);
            frames(c, C == "tap" ? (n >= 4 ? atoi(d) : 12) : (n >= 3 ? atoi(b) : 6));
            for (int id : bs) ps1_set_button(c, id, false);
            if (C == "tap") frames(c, n >= 3 ? atoi(b) : 8);
        } else if (C == "dump" && n >= 2) {
            std::string p = out + "/" + a + ".ram";
            FILE *f = fopen(p.c_str(), "wb");
            if (!f) { fprintf(stderr, "cannot write %s\n", p.c_str()); return 3; }
            std::vector<uint8_t> buf(PS1_RAM_SIZE);
            for (uint32_t i = 0; i < PS1_RAM_SIZE; i += 4) {
                uint32_t v = 0;
                ps1_read(c, 0x80000000u + i, 4, &v);
                memcpy(&buf[i], &v, 4);
            }
            fwrite(buf.data(), 1, buf.size(), f);
            fclose(f);
        } else if (C == "shot" && n >= 2) {
            int w = 0, h = 0;
            if (ps1_framebuffer(c, &w, &h)) {
                const uint8_t *px = ps1_framebuffer_ptr(c);
                std::string p = out + "/" + a + ".ppm";
                FILE *f = fopen(p.c_str(), "wb");
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int i = 0; i < w * h; i++) fwrite(px + i * 4, 1, 3, f);
                fclose(f);
            }
        } else if (C == "save" && n >= 2) {
            ps1_save_state(c, (out + "/" + a + ".state").c_str());
        } else if (C == "load" && n >= 2) {
            if (!ps1_load_state(c, (out + "/" + a + ".state").c_str())) fprintf(stderr, "load state failed\n");
        } else if (C == "adapter") {
            static oga::Host H = {};
            H.read8 = hr8; H.read16 = hr16; H.read32 = hr32; H.speak = hspeak; H.log = hlog;
            oga::kMortalKombatTrilogy.attach(&H);
            g_adapterOn = true;
        } else if (C == "cmd" && n >= 2) {
            // the reader's own keys: where (Where Am I), status, menu, dump
            oga::Command k = oga::Command::WhereAmI;
            if (!strcmp(a, "status")) k = oga::Command::Status;
            else if (!strcmp(a, "menu")) k = oga::Command::MenuState;
            else if (!strcmp(a, "dump")) k = oga::Command::DumpState;
            oga::kMortalKombatTrilogy.command(k);
        } else if (C == "cue") {
            oga::CueSnapshot cs;
            bool ok = oga::kMortalKombatTrilogy.cue_snapshot && oga::kMortalKombatTrilogy.cue_snapshot(&cs);
            printf("[%llu] CUE: %s battle=%d locked=%d dist=%.1f foe_beep=%d own_beep=%d\n", g_frame, ok ? "ok" : "none",
                   ok ? cs.battle : 0, ok ? cs.locked : 0, ok ? cs.dist : 0.0f, ok ? cs.foe_beep : 0, ok ? cs.own_beep : 0);
        } else if (C == "say") {
            printf("%s\n", l.c_str() + 4);
            fflush(stdout);
        } else {
            fprintf(stderr, "bad script line: %s\n", l.c_str());
            return 2;
        }
    }
    fclose(sf);
    ps1_stop(c);
    ps1_destroy(c);
    return 0;
}
