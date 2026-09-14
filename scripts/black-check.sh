#!/usr/bin/env bash
# black-check.sh — focused re-run of the Black path after the IR fix, capturing
# the full speech trail (the observable the accessibility feature is judged on).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simboot Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -5

cat Sources/OpenGameAccess/Resources/bizhawk_compat.lua > /tmp/combined.lua
printf '\n' >> /tmp/combined.lua
cat Sources/OpenGameAccess/Resources/main.lua >> /tmp/combined.lua
export PA_SCRIPT=/tmp/combined.lua

echo "===== POKEMON BLACK, 30000 frames, full speech trail ====="
timeout 700 ./Vendor/simboot "$HOME/hosttest-data/black.nds" - - - 30000 2>&1 > /tmp/black.log
echo "exit=$?"
echo "--- every spoken line ---"
grep '\[SPEAK\]' /tmp/black.log
echo "--- frame progress / rendering ---"
grep -E '^f=|^== ' /tmp/black.log
