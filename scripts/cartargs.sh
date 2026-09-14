#!/usr/bin/env bash
# cartargs.sh — Android always passes writable SRAM in NDSCartArgs. My ParseROM
# call passes std::nullopt. What does the cart do with no SRAM args, and what
# save size does it derive from the ROM header?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== NDSCartArgs fields ==="
grep -n 'struct NDSCartArgs' -A 20 "$SRC/NDSCart.h" | head -25

echo
echo "=== ParseROM: how is SRAM set up when args is nullopt? ==="
grep -n 'std::unique_ptr<CartCommon> ParseROM' -A 60 "$SRC/NDSCart.cpp" | head -70
