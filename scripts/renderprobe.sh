#!/usr/bin/env bash
# renderprobe.sh — does the game render now?
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/renderprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/renderprobe Core/renderprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/renderprobe ] && echo yes || echo NO)"
echo
echo "########## BLACK, 20000 frames ##########"
timeout 300 ./Vendor/renderprobe "$HOME/hosttest-data/black.nds" 20000 black 2>&1 | tail -16
echo
echo "########## DIAMOND, 20000 frames ##########"
timeout 300 ./Vendor/renderprobe "$HOME/roms/diamond.nds" 20000 diamond 2>&1 | tail -16
