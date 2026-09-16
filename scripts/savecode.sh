#!/usr/bin/env bash
# savecode.sh — where does the save memory actually live?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== files under NDSCart/ ==="
ls "$SRC/NDSCart/"

echo
echo "=== SetSaveMemory definitions anywhere ==="
grep -rn 'SetSaveMemory' "$SRC" --include=*.cpp | head -10

echo
echo "=== SaveMemSize / GetSaveMemoryLength ==="
grep -rn 'SaveMemSize\|GetSaveMemoryLength\|SaveMemoryLength' "$SRC/NDSCart.h" "$SRC/NDSCart/"*.cpp 2>/dev/null | head -12

echo
echo "=== IRBO / gamecode lookup in ROMList ==="
grep -rn 'IRBO\|IRB' "$SRC/ROMList.cpp" | head -5
echo "--- what does Black's entry look like? search by title fragment ---"
grep -n 'Pokemon\|POKEMON' "$SRC/ROMList.cpp" | head -5
