#!/usr/bin/env bash
# cpustate.sh — dump the ARM9/ARM7 PC right after poke_start, then again after
# a few seconds of running. Repeated samples of the SAME code location mean an
# idle/wait loop; samples scattered across 0x2xxxxxxx mean the core is lost.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cat > /tmp/cpustate.c <<'EOF'
#include "pokecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); fflush(stdout); }
static void on_log(const char* t, void* u) { (void)t;(void)u; }
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail\n"); return 1; }
    poke_set_script(core, "while true do emu.frameadvance() end\n");
    if (!poke_start(core)) { printf("start fail: %s\n", poke_last_error(core)); return 1; }
    printf("started\n"); fflush(stdout);

    /* Run frames in a separate loop; report the guest PCs periodically by
       asking the core for them through the memory-domain API is not exposed,
       so instead run a bounded number of frames and report progress. */
    for (long i = 0; i < 100000; i++)
    {
        if (!poke_frame(core)) { printf("stopped at %ld\n", i); break; }
        if (i % 200 == 0) { printf("frames=%ld\n", i); fflush(stdout); }
    }
    poke_destroy(core);
    return 0;
}
EOF

cp /tmp/cpustate.c Core/cpustate.c
g++ -O1 -g -ISources/CPokeCore/include -o Vendor/cpustate Core/cpustate.c \
    Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 | grep -E '\berror\b' | head -3

timeout 60 ./Vendor/cpustate "$HOME/hosttest-data/black.nds" > "$HOME/cpustate.log" 2>&1 &
HPID=$!
sleep 25
if kill -0 "$HPID" 2>/dev/null; then
  echo "=== still in frame 0 after 25s; sampling guest PC via gdb ==="
  timeout 25 gdb -batch -p "$HPID" \
    -ex 'set $a=(melonDS::ARMv4*)0' \
    -ex 'printf "R15=%08x\n", ((unsigned*)0)->x' 2>/dev/null | grep -E 'R15|error' | head -3
  kill "$HPID" 2>/dev/null
else
  echo "=== finished ==="
fi
cat "$HOME/cpustate.log" | head -20
