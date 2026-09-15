#!/usr/bin/env bash
# startfull.sh — the rest of NDS::Start, and the firmware->cart handoff.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/startfull.txt"
: > "$OUT"

echo "########## NDS::Start, full (553-625) ##########" >> "$OUT"
sed -n '553,625p' "$SRC/NDS.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## the firmware->cart handoff (1600-1700) ##########" >> "$OUT"
sed -n '1600,1700p' "$SRC/NDS.cpp" >> "$OUT"

cat "$OUT"
