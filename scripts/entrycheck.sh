#!/usr/bin/env bash
# entrycheck.sh — is the ROM header parsed with the right offset?
# Suspect: NDSCart::ParseROM is given the raw file, and if the file has a
# 0x1000-byte header/prefix (or is being read from a path with an odd size),
# the header fields land on the wrong bytes and ARM9EntryAddress reads as ~0.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

cat > /tmp/entryprobe.cpp <<'EOF'
#include "pokecore.h"
#include "NDS.h"
#include "NDSCart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, argv[1], NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    melonDS::NDS* nds = poke_debug_nds(core);
    auto* cart = nds->GetNDSCart();
    if (!cart) { printf("no cart\n"); return 1; }
    const auto& h = cart->GetHeader();
    printf("Header:\n");
    printf("  GameCode           = %.4s\n", h.GameCode);
    printf("  ARM9EntryAddress   = %08X\n", h.ARM9EntryAddress);
    printf("  ARM7EntryAddress   = %08X\n", h.ARM7EntryAddress);
    printf("  ARM9ROMOffset      = %08X\n", h.ARM9ROMOffset);
    printf("  ARM7ROMOffset      = %08X\n", h.ARM7ROMOffset);
    printf("  ROMLength          = %08X\n", h.ROMLength);
    printf("  UnitCode           = %02X\n", h.UnitCode);
    printf("\nCPU state right after load:\n");
    printf("  ARM9.R[15] (PC)    = %08X\n", nds->ARM9.R[15]);
    printf("  ARM7.R[15] (PC)    = %08X\n", nds->ARM7.R[15]);
    printf("  ARM9.R[12]         = %08X\n", nds->ARM9.R[12]);
    printf("  ARM7.R[12]         = %08X\n", nds->ARM7.R[12]);
    poke_destroy(core);
    return 0;
}
EOF

cp /tmp/entryprobe.cpp Core/entryprobe.cpp
SRC="$HOME/src/melonds-lua/src"
g++ -O1 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/entryprobe Core/entryprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined' | head -5
echo "linked: $([ -x Vendor/entryprobe ] && echo yes || echo NO)"
echo
./Vendor/entryprobe "$HOME/hosttest-data/black.nds" 2>&1 | head -25
