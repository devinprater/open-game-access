#!/usr/bin/env bash
# timetest.sh — build the timing harness and run it with and without the script.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/timingtest.c" Core/ 2>/dev/null || true

g++ -O1 -g -ISources/CPokeCore/include -o Vendor/timingtest Core/timingtest.c \
    Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 | grep -vE 'warn_unused|fread' | head -5
echo "LINK=$?"

echo
echo "############ WITHOUT script (pure emulation) ############"
timeout 120 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "" 600 2>&1 | tail -12

echo
echo "############ WITH script ############"
timeout 180 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/script.lua" 600 2>&1 | tail -12
