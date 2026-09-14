#!/usr/bin/env bash
# purecore.sh — the cleanest isolation: pure core, zero Lua.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/purecore.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/purecore Core/purecore.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/purecore ] && echo yes || echo NO)"

echo
echo "########## BLACK, NO LUA AT ALL ##########"
timeout 200 ./Vendor/purecore "$HOME/hosttest-data/black.nds" 6000 2>&1 | tail -12
echo
echo "########## DIAMOND, NO LUA AT ALL ##########"
timeout 200 ./Vendor/purecore "$HOME/roms/diamond.nds" 6000 2>&1 | tail -12
