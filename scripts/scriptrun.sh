#!/usr/bin/env bash
# scriptrun.sh — the real accessibility script against an in-game Black.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/scriptrun.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/scriptrun Core/scriptrun.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/scriptrun ] && echo yes || echo NO)"

# the real combined script (shim + main.lua)
ls -la "$HOME/hosttest-data/script.lua"

echo
echo "########## REAL SCRIPT + in-game Black, 90000 frames ##########"
timeout 800 ./Vendor/scriptrun "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/script.lua" 90000 2>&1 | tail -40
