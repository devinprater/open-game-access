#!/usr/bin/env bash
# vramprobe.sh — one decisive run: has the game written any graphics data?
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/vramprobe.cpp" Core/

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/vramprobe Core/vramprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/vramprobe ] && echo yes || echo NO)"

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 200 ./Vendor/vramprobe "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 9000 2>&1 | tail -30
