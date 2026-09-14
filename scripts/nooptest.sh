#!/usr/bin/env bash
# nooptest.sh — decisive isolation: run the SAME core+ROM with only the shim
# (no main.lua). If the game renders here, the accessibility script is what
# stalls it. If it still does not render, the bug is in the core wiring.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
cd "$ROOT"
export PA_SHIM_DIR="$ROOT"

# a shim-only script: the compat layer plus a trivial per-frame loop
cat "$HOME/hosttest-data/noop.lua" > /dev/null 2>&1
ls -la "$HOME/hosttest-data/noop.lua"

echo "=== noop.lua tail (is it shim + loop?) ==="
tail -6 "$HOME/hosttest-data/noop.lua"

echo
echo "########## SHIM ONLY (no main.lua), direct boot, 20000 frames ##########"
PA_SCRIPT="$HOME/hosttest-data/noop.lua" \
timeout 250 ./Vendor/fwtest "$HOME/hosttest-data/black.nds" "" "" "" 20000 2>&1 \
  | grep -vE '^\s*$' | tail -20

echo
echo "########## SHIM ONLY, WITH real firmware+bios, 20000 frames ##########"
PA_SCRIPT="$HOME/hosttest-data/noop.lua" \
timeout 250 ./Vendor/fwtest "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 20000 2>&1 \
  | grep -vE '^\s*$' | tail -20
