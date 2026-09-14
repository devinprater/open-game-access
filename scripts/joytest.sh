#!/usr/bin/env bash
# joytest.sh — prove joypad.set{} reaches the emulated console's keypad.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1

bash "$ROOT/scripts/build-host.sh" || exit 1

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/joytest Core/joytest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/joytest ] || { echo "!! joytest did not link"; exit 1; }

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
echo
timeout 200 ./Vendor/joytest "$HOME/hosttest-data/black.nds" 2>&1 | tail -30
echo "EXIT=${PIPESTATUS[0]}"
