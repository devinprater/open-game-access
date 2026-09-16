#!/usr/bin/env bash
# blackprobe.sh — Black with real firmware+BIOS, long run.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/blackprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/blackprobe Core/blackprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/blackprobe ] && echo yes || echo NO)"
echo
echo "########## BLACK with real firmware+BIOS, 150000 frames ##########"
timeout 900 ./Vendor/blackprobe "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 150000 2>&1 | tail -25
