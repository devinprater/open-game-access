#!/usr/bin/env bash
# fixtest.sh — rebuild with the Reset() fix and measure real speed.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pokecore.cpp" Core/

# 1. rebuild pokecore
g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -5
echo "pokecore rebuilt"

# 2. rebuild the probes
for T in timingprobe instrrate hosttest; do
  SRC_F="Core/$T.c"; [ -f "Core/$T.cpp" ] && SRC_F="Core/$T.cpp"
  g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
    -o "Vendor/$T" "$SRC_F" Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
    | grep -E '\berror\b|undefined reference' | head -4
done
echo "probes rebuilt"

echo
echo "###### timings after the fix ######"
./Vendor/timingprobe "$HOME/hosttest-data/black.nds" 2>&1 | head -16

echo
echo "###### speed ######"
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 90 ./Vendor/instrrate "$HOME/hosttest-data/black.nds" 2>&1 | tail -8
