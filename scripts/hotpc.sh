#!/usr/bin/env bash
# hotpc.sh — WHERE is the ARM9 spending its time, and did the game write any
# graphics at all? A tiny set of repeated PCs = a wait/crash loop; a wide spread
# = real work.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== where does 'Game is now booting' come from? ==="
grep -rn "Game is now booting" "$SRC" | head -3

echo
echo "=== does NDS take a delegate, and is it optional? ==="
grep -n 'NDS(NDSArgs' "$SRC/NDS.h" | head -5
grep -n 'NDSDelegate' "$SRC/NDS.h" | head -8

echo
echo "=== NDSArgs fields (what might be left unset) ==="
grep -n 'struct NDSArgs' -A 30 "$SRC/NDS.h" | head -36
