#!/usr/bin/env bash
# realrun.sh — run the REAL accessibility script (shim + main.lua combined)
# against the fixed core, and capture every spoken line.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/fwtest.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fwtest Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -5
echo "linked: $([ -x Vendor/fwtest ] && echo yes || echo NO)"

echo "=== the real combined script ==="
ls -la "$HOME/hosttest-data/script.lua"

echo
echo "################ REAL SCRIPT + FIRMWARE + BIOS, 40000 frames ################"
PA_SCRIPT="/home/devin/hosttest-data/script.lua" \
timeout 400 ./Vendor/fwtest "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 40000 2>&1 \
  | grep -vE '^\s*$' | tail -40
