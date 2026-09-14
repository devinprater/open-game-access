/*
 * screen.cpp — headless feasibility screening for a ROM.
 *
 * Accessibility-mod difficulty is dominated by three measurable facts, not by
 * genre labels:
 *
 *   1. DOES IT RUN AND RENDER? A game that never leaves the boot path cannot be
 *      modded at all. (distinct colours + VRAMCNT + frames completed)
 *   2. IS THE TEXT PLAIN DATA IN RAM, OR GLYPHS DRAWN TO THE SCREEN? A reader is
 *      cheap when dialogue sits in RAM as ASCII/UTF-16 and expensive when the
 *      game rasterizes its own font. Measured by scanning MainRAM for printable
 *      runs and for UTF-16 runs.
 *   3. DOES IT READ THE KEYPAD? Touch-only games need synthetic touch instead of
 *      buttons, which is a different (and weaker) path in this core.
 *
 * Output is one line of JSON per ROM so a driver can aggregate.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

melonDS::NDS* poke_debug_nds(PokeCore* core);
unsigned long long poke_frames_completed(PokeCore* core);

struct Counts { long asciiRuns; long asciiChars; long utf16Runs; long utf16Chars; };

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 6000;

    PokeCore* core = poke_create();
    if (!core) { printf("{\"error\":\"no core\"}\n"); return 1; }
    if (!poke_load_rom(core, rom, NULL)) {
        printf("{\"error\":\"%s\"}\n", poke_last_error(core));
        return 1;
    }
    melonDS::NDS* nds = poke_debug_nds(core);

    long framesChanged = 0;
    unsigned long lastHash = 0;

    /* ⛔ THE CORE MUST BE STARTED, or poke_frame() returns false immediately and
     * every measurement reads as "nothing happened" — which is exactly what the
     * first version of this harness reported for all 17 ROMs.
     *
     * ⛔ AND `emu` COMES FROM THE SHIM, not from the core: a "minimal" inline
     * script that calls emu.frameadvance() dies with "attempt to index a nil
     * value (global 'emu')" because pokecore installs `memory`/`input`/`hermes_tts`
     * but the BizHawk surface lives in bizhawk_compat.lua. So load the real shim,
     * then a tiny yielder — this screening measures the GAME, not the readers. */
    {
        static std::string script;
        const char* shimPath = getenv("PA_SHIM");
        if (shimPath) {
            FILE* f = fopen(shimPath, "rb");
            if (f) {
                char buf[65536];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) script.append(buf, n);
                fclose(f);
            }
        }
        script += "\nlocal n = 0\nwhile true do n = n + 1; emu.frameadvance() end\n";
        poke_set_script(core, script.c_str());
    }
    if (!poke_start(core)) {
        printf("{\"error\":\"start failed: %s\"}\n", poke_last_error(core));
        return 1;
    }

    for (long f = 0; f < frames; f++)
    {
        /* Poke the pad the way a player would, so input-gated games advance. */
        if (f == 300)  poke_set_button(core, POKE_BTN_A, true);
        if (f == 318)  poke_set_button(core, POKE_BTN_A, false);
        if (f == 900)  poke_set_button(core, POKE_BTN_START, true);
        if (f == 918)  poke_set_button(core, POKE_BTN_START, false);
        if (f == 1500) poke_set_button(core, POKE_BTN_A, true);
        if (f == 1518) poke_set_button(core, POKE_BTN_A, false);
        if (f == 2500) poke_set_button(core, POKE_BTN_A, true);
        if (f == 2518) poke_set_button(core, POKE_BTN_A, false);
        /* Also a touch, for touch-only titles. */
        if (f == 2000) poke_touch(core, 128, 96, true);
        if (f == 2020) poke_touch(core, 128, 96, false);
        if (!poke_frame(core)) break;

        /* Sample the top screen periodically: a screen whose pixels never change
         * is a still image (or a hang), not a live game. This separates "boots and
         * draws something" from "is actually running and animating". */
        if (f % 500 == 0) {
            int w2 = 0, h2 = 0;
            poke_framebuffer(core, 0, &w2, &h2);
            const uint8_t* px = poke_framebuffer_ptr(core, 0);
            if (px) {
                unsigned long h = 1469598103934665603UL;
                for (int k = 0; k < 256 * 192; k++) { h ^= px[k * 4]; h *= 1099511628211UL; }
                if (h != lastHash) { framesChanged++; lastHash = h; }
            }
        }
    }

    int w = 0, h = 0;
    poke_framebuffer(core, 0, &w, &h);
    const uint8_t* top = poke_framebuffer_ptr(core, 0);
    poke_framebuffer(core, 1, &w, &h);
    const uint8_t* bot = poke_framebuffer_ptr(core, 1);

    auto distinct = [](const uint8_t* px) -> int {
        if (!px) return -1;
        static unsigned seen[4096];
        int n = 0;
        for (int i = 0; i < 256 * 192; i += 17) {
            unsigned v = (unsigned)px[i*4] << 16 | (unsigned)px[i*4+1] << 8 | (unsigned)px[i*4+2];
            int fnd = 0;
            for (int k = 0; k < n; k++) if (seen[k] == v) { fnd = 1; break; }
            if (!fnd && n < 4096) seen[n++] = v;
        }
        return n;
    };

    /* ---- text scan over MainRAM (4 MiB at 0x02000000) ---- */
    const uint8_t* ram = nds->MainRAM;
    const size_t RAMSZ = 4u * 1024 * 1024;
    long asciiRuns = 0, asciiChars = 0, utf16Runs = 0, utf16Chars = 0;
    int bestAscii = 0, bestUtf16 = 0;
    /* PROSE metrics: the longest run is usually tile/junk data ("DDDDDD...").
     * Real dialogue is >= 12 chars, contains spaces, and is mostly letters. That
     * shape is the single best predictor of how cheap a reader will be. */
    long proseRuns = 0;
    int bestProse = 0;
    char sampleAscii[160] = {0}, sampleUtf16[160] = {0}, sampleProse[200] = {0};

    auto printable = [](uint8_t b) {
        return (b >= 0x20 && b < 0x7F) || b == '\n';
    };
    for (size_t i = 0; i < RAMSZ; ) {
        size_t j = i;
        while (j < RAMSZ && printable(ram[j])) j++;
        size_t runlen = j - i;
        if (runlen >= 6) {
            asciiRuns++; asciiChars += (long)runlen;
            if ((int)runlen > bestAscii) {
                bestAscii = (int)runlen;
                size_t take = runlen < 150 ? runlen : 150;
                memcpy(sampleAscii, ram + i, take); sampleAscii[take] = 0;
                for (size_t k = 0; k < take; k++) if (sampleAscii[k] == '\n') sampleAscii[k] = ' ';
            }
            /* Prose-shaped? needs a space and be mostly letters/spaces. */
            if (runlen >= 12) {
                int spaces = 0, letters = 0;
                for (size_t k = i; k < j; k++) {
                    uint8_t b = ram[k];
                    if (b == ' ') spaces++;
                    else if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) letters++;
                }
                if (spaces >= 1 && (letters + spaces) * 10 >= (int) runlen * 7) {
                    proseRuns++;
                    if ((int)runlen > bestProse) {
                        bestProse = (int)runlen;
                        size_t take = runlen < 180 ? runlen : 180;
                        memcpy(sampleProse, ram + i, take); sampleProse[take] = 0;
                        for (size_t k = 0; k < take; k++)
                            if (sampleProse[k] == '\n' || (unsigned char) sampleProse[k] < 0x20)
                                sampleProse[k] = ' ';
                    }
                }
            }
        }
        i = (runlen >= 6) ? j : i + 1;
    }
    /* UTF-16LE: printable ASCII low byte, zero high byte, >= 6 units. */
    for (size_t i = 0; i + 1 < RAMSZ; ) {
        size_t j = i;
        while (j + 1 < RAMSZ && printable(ram[j]) && ram[j+1] == 0) j += 2;
        size_t units = (j - i) / 2;
        if (units >= 6) {
            utf16Runs++; utf16Chars += (long)units;
            if ((int)units > bestUtf16) {
                bestUtf16 = (int)units;
                size_t take = units < 70 ? units : 70;
                size_t k = 0;
                for (size_t u = 0; u < take; u++) sampleUtf16[k++] = (char)ram[i + u*2];
                sampleUtf16[k] = 0;
            }
        }
        i = (units >= 6) ? j : i + 2;
    }

    /* JSON-escape the samples (only quotes/backslashes/control can appear). */
    auto esc = [](const char* s, char* out, size_t cap) {
        size_t o = 0;
        for (size_t i = 0; s[i] && o + 2 < cap; i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
            else if ((unsigned char)c < 0x20) { out[o++] = ' '; }
            else out[o++] = c;
        }
        out[o] = 0;
    };
    char ea[400], eu[400], ep[500];
    esc(sampleAscii, ea, sizeof(ea));
    esc(sampleUtf16, eu, sizeof(eu));
    esc(sampleProse, ep, sizeof(ep));

    printf("{\"frames\":%llu,\"keyinput\":%u,"
           "\"top_colours\":%d,\"bot_colours\":%d,\"vramcnt_a\":%u,"
           "\"frames_changed\":%d,"
           "\"ascii_runs\":%ld,\"ascii_chars\":%ld,\"best_ascii\":%d,\"sample_ascii\":\"%s\","
           "\"prose_runs\":%ld,\"best_prose\":%d,\"sample_prose\":\"%s\","
           "\"utf16_runs\":%ld,\"utf16_chars\":%ld,\"best_utf16\":%d,\"sample_utf16\":\"%s\"}\n",
           poke_frames_completed(core), (unsigned)(nds->KeyInput & 0xFFF),
           distinct(top), distinct(bot), (unsigned)nds->GPU.VRAMCNT[0],
           framesChanged,
           asciiRuns, asciiChars, bestAscii, ea,
           proseRuns, bestProse, ep,
           utf16Runs, utf16Chars, bestUtf16, eu);

    poke_destroy(core);
    return 0;
}
