#!/usr/bin/env bash
# wherestuck.sh — is the game waiting, or genuinely stuck?
# Samples the guest PCs across a long run: if the ARM9 sits in one small region
# forever, the game is in a wait loop and never progresses; if it roams, it is
# working through code and the white screen is a draw problem.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cat > /tmp/wherestuck.c <<'EOF'
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <string.h>
melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t;(void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); fflush(stdout); }
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }
    static char script[200000];
    const char* shim = getenv("PA_SHIM");
    size_t n = 0;
    if (shim) { FILE* f=fopen(shim,"rb"); if(f){n=fread(script,1,sizeof(script)-64,f);fclose(f);} }
    script[n]=0; strcat(script, "\nwhile true do emu.frameadvance() end\n");
    poke_set_script(core, script);
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);

    long total = atol(argv[2]);
    unsigned last9 = 0, last7 = 0;
    long changes9 = 0, changes7 = 0;
    for (long f = 0; f < total; f++)
    {
        if (f == 600) poke_set_button(core, 3, true);
        if (f == 620) poke_set_button(core, 3, false);
        if (!poke_frame(core)) { printf("stopped at %ld\n", f); break; }
        unsigned a = nds->ARM9.R[15], b = nds->ARM7.R[15];
        if (a != last9) { changes9++; last9 = a; }
        if (b != last7) { changes7++; last7 = b; }
        if (f % 2000 == 0)
        {
            printf("f=%-7ld ARM9=%08X (changes=%ld)  ARM7=%08X (changes=%ld)\n",
                   f, a, changes9, b, changes7);
            fflush(stdout);
        }
    }
    printf("final ARM9=%08X ARM7=%08X  changes9=%ld changes7=%ld\n", last9, last7, changes9, changes7);
    poke_destroy(core);
    return 0;
}
EOF
cp /tmp/wherestuck.c Core/wherestuck.c
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/wherestuck Core/wherestuck.c Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined' | head -5
echo "linked: $([ -x Vendor/wherestuck ] && echo yes || echo NO)"
export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
timeout 200 ./Vendor/wherestuck "$HOME/hosttest-data/black.nds" 30000 2>&1 | head -25
