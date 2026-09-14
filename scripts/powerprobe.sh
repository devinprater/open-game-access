#!/usr/bin/env bash
# powerprobe.sh — is ScreensEnabled the gate? Force the LCD on and see.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/powerprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/powerprobe Core/powerprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/powerprobe ] && echo yes || echo NO)"

echo
echo "########## BLACK ##########"
timeout 200 ./Vendor/powerprobe "$HOME/hosttest-data/black.nds" 2>&1 | tail -10
echo
echo "########## DIAMOND ##########"
timeout 200 ./Vendor/powerprobe "$HOME/roms/diamond.nds" 2>&1 | tail -10
