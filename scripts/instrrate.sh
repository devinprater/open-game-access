#!/usr/bin/env bash
# instrrate.sh — how fast does the interpreter actually run?
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/instrrate.cpp" Core/
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pokecore.cpp" Core/

g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -5

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/instrrate Core/instrrate.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/instrrate ] && echo yes || echo NO)"

echo
echo "=== 30s of sampling ==="
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 60 ./Vendor/instrrate "$HOME/hosttest-data/black.nds" > "$HOME/instrrate.log" 2>&1
echo "exit=$? (124=timeout)"
head -20 "$HOME/instrrate.log"
