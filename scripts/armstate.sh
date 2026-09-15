#!/usr/bin/env bash
# armstate.sh — build and run the in-process CPU-state diagnostic.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/open-game-access/Core/armstate.cpp" Core/

g++ -O1 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/armstate Core/armstate.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/armstate ] && echo yes || echo NO)"

echo
echo "=== running (30s budget) ==="
export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
timeout 40 ./Vendor/armstate "$HOME/hosttest-data/black.nds" 40 > "$HOME/armstate.log" 2>&1
echo "exit=$? (124=timeout)"
head -50 "$HOME/armstate.log"
