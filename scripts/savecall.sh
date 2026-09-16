#!/usr/bin/env bash
# savecall.sh — NDS.cpp:787 calls SetSaveMemory from inside the core. When, and
# with what? That determines whether a null SRAM arg matters.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== NDS.cpp 770-805 ==="
sed -n '770,805p' "$SRC/NDS.cpp"

echo
echo "=== CartCommon::SetSaveMemory + the save-chip setup (CartCommon.cpp) ==="
grep -n 'SetSaveMemory\|SaveMemSize\|save chip\|SPI_Firmware\|Flash' -A 14 "$SRC/NDSCart/CartCommon.cpp" | sed -n '1,60p'

echo
echo "=== where is save memory allocated? (ROMParams.SAVSize) ==="
grep -rn 'SAVSize' "$SRC/NDSCart/CartRetail.cpp" "$SRC/NDSCart/CartCommon.cpp" "$SRC/NDSCart.h" 2>/dev/null | head -10
