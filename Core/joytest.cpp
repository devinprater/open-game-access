/*
 * joytest.cpp — does joypad.set{} actually reach the emulated console's keypad?
 *
 * The bug this guards: melonDS-lua never had a per-frame button override, so
 * main.lua's controller-mod layer (hold the modifier -> the pad drives the
 * accessibility list and the GAME RECEIVES NOTHING) was dead on mobile. A no-op
 * stub makes the script still run and still speak, so the failure is invisible
 * from the script's output — the only honest check is to read the CONSOLE's own
 * key register while an override is in force.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);

static int g_fail = 0;

/* The emulated console's own view of the pad. Bit = 1 means RELEASED. */
static unsigned char BitFor(const char* dsName)
{
    static const char* names[] = {"A","B","Select","Start","Right","Left","Up","Down","R","L","X","Y"};
    for (int i = 0; i < 12; i++) if (strcmp(names[i], dsName) == 0) return (unsigned char) i;
    return 255;
}

static void check(const char* what, bool got, bool want)
{
    bool ok = (got == want);
    if (!ok) g_fail++;
    printf("%s %-58s got=%d want=%d\n", ok ? "  ok " : "FAIL", what, (int) got, (int) want);
}

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    PokeCore* core = poke_create();
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail\n"); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);

    /* A script that only exercises the override API, so the test measures the
     * BINDING and not main.lua's mood. */
    const char* script =
        "local frames = 0\n"
        "while true do\n"
        "  frames = frames + 1\n"
        "  if frames == 3 then\n"
        "    joypad.set({ A = false, Up = false })\n"           /* block A and Up */
        "  elseif frames == 4 then\n"
        "    joypad.set({ A = true })\n"                        /* force A DOWN, release Up */
        "  elseif frames == 5 then\n"
        "    joypad.set({})\n"                                  /* clear every override */
        "  end\n"
        "  emu.frameadvance()\n"
        "end\n";

    static char buf[1 << 20];
    const char* shimPath = getenv("PA_SHIM");
    size_t n = 0;
    if (shimPath) { FILE* f = fopen(shimPath, "rb"); if (f) { n = fread(buf, 1, sizeof(buf) - 4096, f); fclose(f); } }
    buf[n] = 0;
    /* shim first, then the probe script — the same order the app concatenates. */
    strcat(buf, "\n");
    strncat(buf, script, sizeof(buf) - strlen(buf) - 1);

    poke_set_script(core, buf);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    /* Hold A and Up physically, the whole time. */
    poke_set_button(core, POKE_BTN_A, true);
    poke_set_button(core, POKE_BTN_UP, true);

    printf("== reading the CONSOLE's keypad (bit 1 = released). Physically holding A + Up.\n\n");
    for (int f = 1; f <= 7; f++)
    {
        poke_frame(core);
        unsigned releasedBits = nds->KeyInput & 0xFFF;
        bool A_up  = (releasedBits >> BitFor("A"))  & 1;
        bool Up_up = (releasedBits >> BitFor("Up")) & 1;
        printf("f=%d  KeyInput=%03X   A=%s  Up=%s\n", f, releasedBits,
               A_up ? "released" : "PRESSED", Up_up ? "released" : "PRESSED");
    }
    printf("\n");

    /* After 7 frames the script has cleared its overrides (frame 5), so the
     * physical pad must show through again: both A and Up PRESSED. */
    unsigned rel = nds->KeyInput & 0xFFF;
    check("override cleared -> physical A is PRESSED again", !((rel >> BitFor("A")) & 1), true);
    check("override cleared -> physical Up is PRESSED again", !((rel >> BitFor("Up")) & 1), true);

    printf("\n%s (%d failures)\n", g_fail ? "OVERRIDE NOT WORKING" : "override works", g_fail);
    poke_destroy(core);
    return g_fail ? 1 : 0;
}
