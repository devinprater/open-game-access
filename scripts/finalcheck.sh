#!/usr/bin/env bash
# finalcheck.sh
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/finalcheck.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/finalcheck Core/finalcheck.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -6
echo "linked: $([ -x Vendor/finalcheck ] && echo yes || echo NO)"
echo
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 150 ./Vendor/finalcheck "$HOME/hosttest-data/black.nds" 2>&1 | head -25
