#!/usr/bin/env bash
# crashprobe.sh — has the guest crashed into an exception vector?
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/crashprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/crashprobe Core/crashprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/crashprobe ] && echo yes || echo NO)"

echo
echo "########## BLACK ##########"
timeout 200 ./Vendor/crashprobe "$HOME/hosttest-data/black.nds" 3000 2>&1 | tail -30
