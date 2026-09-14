#!/usr/bin/env bash
# bootapi.sh — is there a boot-firmware entry point, and what is it called?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== every 'Boot' symbol in NDS.h ==="
grep -n 'Boot' "$SRC/NDS.h" | head -12

echo
echo "=== every 'Boot' symbol in NDS.cpp ==="
grep -n 'Boot' "$SRC/NDS.cpp" | head -12

echo
echo "=== how does the Qt frontend start a game WITHOUT direct boot? ==="
grep -n 'Boot()\|Start()' "$SRC/frontend/qt_sdl/EmuInstance.cpp" | head -10
