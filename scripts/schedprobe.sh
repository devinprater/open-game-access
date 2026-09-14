#!/usr/bin/env bash
# schedprobe.sh
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/schedprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/schedprobe Core/schedprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/schedprobe ] && echo yes || echo NO)"

export PA_SCRIPT="$HOME/hosttest-data/noop.lua"
echo
echo "########## BLACK ##########"
timeout 150 ./Vendor/schedprobe "$HOME/hosttest-data/black.nds" 6000 2>&1 | tail -18
echo
echo "########## DIAMOND ##########"
timeout 150 ./Vendor/schedprobe "$HOME/roms/diamond.nds" 6000 2>&1 | tail -18
