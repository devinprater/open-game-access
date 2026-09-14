#!/usr/bin/env bash
# timingprobe.sh — check the CPU memory timings.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/timingprobe.cpp" Core/
g++ -O1 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/timingprobe Core/timingprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/timingprobe ] && echo yes || echo NO)"
echo
./Vendor/timingprobe "$HOME/hosttest-data/black.nds" 2>&1 | head -30
