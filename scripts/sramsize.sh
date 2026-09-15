#!/usr/bin/env bash
# sramsize.sh — how does the cart size its save, and what happens with no SRAM
# args? Android always passes a real SRAM buffer.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== CartCommon::SetSaveMemory / SRAM sizing ==="
grep -rn 'SetSaveMemory\|SaveMemoryLength\|SRAMLength' "$SRC/NDSCart.cpp" | head -20

echo
echo "=== the generic cart constructor using args ==="
grep -n 'args.SRAM\|args->SRAM\|SRAMLength' -B4 -A12 "$SRC/NDSCart.cpp" | head -50

echo
echo "=== GetNDSSave in NDS.h / NDSCart ==="
grep -rn 'GetNDSSave' "$SRC/NDS.h" "$SRC/NDSCart.h" | head -10
