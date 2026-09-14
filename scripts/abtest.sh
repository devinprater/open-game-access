#!/usr/bin/env bash
# abtest.sh — the A/B that separates the two hypotheses.
#
#   A. core+ROM alone (minimal script)      → does the game itself boot & render?
#   B. core+ROM+the accessibility script    → does the script break it?
#
# If A renders and B does not, the script/shim is corrupting the emulator. If
# neither renders, the bug is in the core wiring.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/vramprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/vramprobe Core/vramprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -5
echo "linked: $([ -x Vendor/vramprobe ] && echo yes || echo NO)"

B="$HOME/hosttest-data/black.nds"
S9="$HOME/ds-bios/bios9.bin"; S7="$HOME/ds-bios/bios7.bin"; FW="$HOME/ds-bios/firmware.bin"

echo
echo "################ A: MINIMAL SCRIPT (no accessibility script) ################"
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
PA_NOSCRIPT=1 timeout 150 ./Vendor/vramprobe "$B" "$S9" "$S7" "$FW" 9000 2>&1 | tail -20
