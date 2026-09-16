#!/usr/bin/env bash
# blank.sh — the framebuffer is PURE WHITE. That is not "nothing drawn": it is
# what GPU::BlankFrame() paints. Find who calls it and what condition gates it.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/blank.txt"
: > "$OUT"

echo "########## BlankFrame body ##########" >> "$OUT"
grep -rn 'void GPU::BlankFrame' -A 25 "$SRC/GPU.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## every caller of BlankFrame ##########" >> "$OUT"
grep -rn 'BlankFrame' "$SRC" --include=*.cpp --include=*.h >> "$OUT"

echo >> "$OUT"
echo "########## FinishFrame + the blanking condition ##########" >> "$OUT"
grep -n 'void GPU::FinishFrame' -A 40 "$SRC/GPU.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## StartFrame ##########" >> "$OUT"
grep -n 'void GPU::StartFrame' -A 20 "$SRC/GPU.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## GPU2D::SetEnabled / A_BG / B_BG enables ##########" >> "$OUT"
grep -n 'void GPU2D::SetEnabled' -A 12 "$SRC/GPU2D.cpp" >> "$OUT"
grep -n 'void GPU3D::SetEnabled' -A 12 "$SRC/GPU3D.cpp" >> "$OUT"

cat "$OUT"
