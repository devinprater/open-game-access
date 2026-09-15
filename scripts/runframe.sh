#!/usr/bin/env bash
# runframe.sh — locate NDS::RunFrame and dump it. This is the code that decides
# whether a frame advances at all.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== where is RunFrame defined? ==="
grep -rn 'NDS::RunFrame' "$SRC" --include=*.cpp --include=*.h | head -10

echo
echo "=== dump it ==="
F=$(grep -rl 'NDS::RunFrame' "$SRC" --include=*.cpp | head -1)
echo "file: $F"
awk '/void NDS::RunFrame/,/^}/' "$F" | head -70
