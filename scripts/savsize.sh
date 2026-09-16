#!/usr/bin/env bash
# savsize.sh — what save size does the fork derive for a retail Pokémon cart, and
# what happens when SRAMLength is 0?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== ReadROMParams: save size lookup ==="
grep -n 'SAVSize\|ReadROMParams' "$SRC/NDSCart.cpp" | head -15

echo
echo "=== CartRetail ctor (sram/sramlen handling) ==="
grep -n 'CartRetail::CartRetail' -A 25 "$SRC/NDSCart.cpp" | head -30

echo
echo "=== SetSaveMemory: what if len is 0? ==="
grep -n 'void CartCommon::SetSaveMemory' -A 15 "$SRC/NDSCart.cpp" | head -20

echo
echo "=== SaveMemoryLength / how the SPI flash chip reacts to zero-length save ==="
grep -n 'SaveMemoryLength\|GetSaveMemory' "$SRC/NDSCart.h" | head -10

echo
echo "=== IRBO in the ROM params list? ==="
grep -n 'IRBO\|IRB' "$SRC/ROMList.cpp" | head -8
