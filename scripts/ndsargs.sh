#!/usr/bin/env bash
# ndsargs.sh — what does NDSArgs contain, and which fields am I leaving unset?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== where is NDSArgs defined? ==="
grep -rln 'struct NDSArgs' "$SRC" --include=*.h

echo
echo "=== the struct ==="
F=$(grep -rl 'struct NDSArgs' "$SRC" --include=*.h | head -1)
echo "file: $F"
awk '/struct NDSArgs/{f=1} f{print NR": "$0} f&&/^};/{exit}' "$F" | head -40
