#!/usr/bin/env bash
# pcprofile.sh — distinct-PC histogram over ~6 s of frame 0.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pcprofile.cpp" Core/

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/pcprofile Core/pcprofile.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -6
echo "linked: $([ -x Vendor/pcprofile ] && echo yes || echo NO)"

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 60 ./Vendor/pcprofile "$HOME/hosttest-data/black.nds" 2>&1 | tail -8
