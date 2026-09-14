#!/usr/bin/env bash
# directboottest.sh — Android's working path is DIRECT BOOT (FreeBIOS, no
# firmware). The second Reset() is brand new, and the earlier blank run predates
# it. So: direct boot + BOTH resets has not actually been tested yet.
# This also runs the REAL accessibility script.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/fwtest.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fwtest Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -5
echo "linked: $([ -x Vendor/fwtest ] && echo yes || echo NO)"

echo
echo "########## DIRECT BOOT (no bios/firmware) + real script, 40000 frames ##########"
PA_SCRIPT="/home/devin/hosttest-data/script.lua" \
timeout 400 ./Vendor/fwtest "$HOME/hosttest-data/black.nds" "" "" "" 40000 2>&1 \
  | grep -vE '^\s*$' | tail -30
