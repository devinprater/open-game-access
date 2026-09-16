#!/usr/bin/env bash
# cartcommon.sh — what does the cart do with NO SRAM buffer, and how does the
# save chip size itself? Android always passes a real SRAM buffer AND requires
# the .sav file to exist, so this is a real behavioural difference.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/cartcommon.txt"
: > "$OUT"

echo "########## CartCommon ctor ##########" >> "$OUT"
awk '/^CartCommon::CartCommon/,/^}/' "$SRC/NDSCart/CartCommon.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## SetSaveMemory ##########" >> "$OUT"
awk '/^void CartCommon::SetSaveMemory/,/^}/' "$SRC/NDSCart/CartCommon.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## SaveMemSize / SaveMemoryLength ##########" >> "$OUT"
grep -n 'SaveMemoryLength\|SaveMemSize\|SRAMLength\|sramlen' "$SRC/NDSCart/CartCommon.cpp" >> "$OUT"
grep -n 'SaveMemoryLength\|SaveMemSize' "$SRC/NDSCart.h" >> "$OUT"

echo >> "$OUT"
echo "########## who sets PowerControl9 / ScreensEnabled? ##########" >> "$OUT"
grep -rn 'ScreensEnabled = \|PowerControl9' "$SRC/GPU.cpp" "$SRC/NDS.cpp" "$SRC/GPU2D.cpp" 2>/dev/null >> "$OUT"

echo >> "$OUT"
echo "########## REST OF RunFrame ##########" >> "$OUT"
sed -n '1010,1085p' "$SRC/NDS.cpp" >> "$OUT"

wc -l "$OUT"
