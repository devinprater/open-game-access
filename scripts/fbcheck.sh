#!/usr/bin/env bash
# fbcheck.sh — is the framebuffer read correct, or is the screen genuinely blank?
#
# hosttest reports distinct colours per screen; "1" means flat. Either the
# software renderer never fills the framebuffer (so nothing is drawn) or the
# read is wrong. This runs a longer stretch and prints the actual pixel values,
# so a genuinely-black screen is distinguishable from a broken pointer.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cat > /tmp/fbcheck.c <<'EOF'
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); fflush(stdout); }
static void on_log(const char* t, void* u) { (void)t;(void)u; }
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }
    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f = fopen(shim,"rb"); if (f) { n = fread(script,1,sizeof(script)-64,f); fclose(f);} }
    script[n] = 0;
    strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }

    long frames = atol(argv[2]);
    for (long f = 0; f < frames; f++)
    {
        if (f == 1500 || f == 1530) poke_set_button(core, 3 /*START*/, true);
        if (f == 1508 || f == 1538) poke_set_button(core, 3, false);
        if (!poke_frame(core)) break;
        if (f % 2000 == 0)
        {
            int w=0,h=0;
            poke_framebuffer(core, 0, &w, &h);
            const unsigned char* p = poke_framebuffer_ptr(core, 0);
            unsigned hist[8] = {0};
            int nsamp = 0;
            if (p) for (int i = 0; i < 256*192; i += 37) {
                unsigned v = (p[i*4]<<16)|(p[i*4+1]<<8)|p[i*4+2];
                if (v == 0) hist[0]++;
                else if (v == 0xFFFFFF) hist[1]++;
                else hist[2]++;
                nsamp++;
            }
            printf("f=%-6ld %dx%d  black=%u white=%u other=%u  first px=%02X%02X%02X\n",
                   f, w, h, hist[0], hist[1], hist[2],
                   p?p[0]:0, p?p[1]:0, p?p[2]:0);
            fflush(stdout);
        }
    }
    printf("done\n"); fflush(stdout);
    poke_destroy(core);
    return 0;
}
EOF

cp /tmp/fbcheck.c Core/fbcheck.c
g++ -O2 -g -ISources/CPokeCore/include -o Vendor/fbcheck Core/fbcheck.c \
    Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 | grep -E '\berror\b|undefined' | head -5
echo "linked: $([ -x Vendor/fbcheck ] && echo yes || echo NO)"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
timeout 300 ./Vendor/fbcheck "$HOME/hosttest-data/black.nds" 40000 2>&1 | tail -30
