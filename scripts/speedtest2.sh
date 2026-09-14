#!/usr/bin/env bash
# speedtest2.sh — is the interpreter slow because of logging, or optimisation?
set -uo pipefail
cd "$HOME/pokemon-access-ios"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/speedtest2.c" Core/

ROM="$HOME/hosttest-data/black.nds"
NOOP="$HOME/hosttest-data/noop.lua"

echo "###### A: -O1 WITH log callback ######"
g++ -O1 -g -ISources/CPokeCore/include -o Vendor/speedtest2 Core/speedtest2.c \
    Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 | grep -E '\berror\b' | head -3
timeout 200 ./Vendor/speedtest2 "$ROM" "$NOOP" 5 > "$HOME/sp-A.log" 2>&1
echo "exit=$?"; grep -E '== ' "$HOME/sp-A.log" | tail -3

echo
echo "###### B: -O1, LOG DISABLED ######"
PA_NOLOG=1 timeout 200 ./Vendor/speedtest2 "$ROM" "$NOOP" 5 > "$HOME/sp-B.log" 2>&1
echo "exit=$?"; grep -E '== ' "$HOME/sp-B.log" | tail -3
