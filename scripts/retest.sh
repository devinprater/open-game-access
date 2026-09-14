#!/usr/bin/env bash
# retest.sh — rebuild pokecore with the JIT fix and re-run the accessibility test.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pokecore.cpp" Core/

rm -f Vendor/hostobj/pokecore.o
g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$HOME/src/melonds-lua/src" -I"$HOME/src/lua-5.4.7/src" \
  -I"$HOME/src/melonds-lua/src/teakra/include" -std=c++17 \
  -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -5
[ -f Vendor/hostobj/pokecore.o ] || { echo "compile failed"; exit 1; }
echo "pokecore.o rebuilt: $(date -r Vendor/hostobj/pokecore.o '+%H:%M:%S')"

g++ -O1 -g -ISources/CPokeCore/include -o Vendor/timingtest Core/timingtest.c \
    Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 | grep -E '\berror\b' | head -5
echo "timingtest linked"

echo
echo "###### 60 frames, NO script (pure emulation) ######"
timeout 120 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" - 60 > "$HOME/retest-noscript.log" 2>&1
echo "exit=$?"
grep -E 'frames in|stopped|starting' "$HOME/retest-noscript.log" | tail -3

echo
echo "###### 600 frames, WITH the accessibility script ######"
timeout 240 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/script.lua" 600 > "$HOME/retest-script.log" 2>&1
echo "exit=$?"
grep -E 'frames in|stopped|SPEAK' "$HOME/retest-script.log" | tail -8
