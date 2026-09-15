#!/usr/bin/env bash
# sched.sh — dump the event/reset machinery. The game is in a wait loop, so the
# question is: which event never fires?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/sched.txt"
: > "$OUT"

echo "########## NDS::Reset ##########" >> "$OUT"
awk '/^void NDS::Reset\(\)/,/^}/' "$SRC/NDS.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## where is Event_LCD first scheduled? ##########" >> "$OUT"
grep -n 'Event_LCD' "$SRC/NDS.cpp" "$SRC/GPU.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## GPU::Reset ##########" >> "$OUT"
awk '/^void GPU::Reset\(\)/,/^}/' "$SRC/GPU.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## NDS::RunFrame ##########" >> "$OUT"
awk '/^void NDS::RunFrame\(\)/,/^}/' "$SRC/NDS.cpp" >> "$OUT"

wc -l "$OUT"
