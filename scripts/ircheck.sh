#!/usr/bin/env bash
# ircheck.sh — Pokémon Black/White/HGSS ship with an INFRARED transceiver in the
# cartridge (irversion = 2 in NDSCart.cpp). Diamond has no IR. If the game polls
# the IR hardware at boot and nothing ever responds, it hangs — which would
# explain exactly why Diamond works and Black does not on the same core.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/ir.txt"
: > "$OUT"

echo "########## how is the IR cart detected? ##########" >> "$OUT"
grep -n 'irversion' -B6 -A6 "$SRC/NDSCart.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## CartRetailIR: what does it need? ##########" >> "$OUT"
grep -n 'class CartRetailIR' -A 40 "$SRC/NDSCart/CartRetailIR.h" >> "$OUT"

echo >> "$OUT"
echo "########## CartRetailIR.cpp: the IR device implementation ##########" >> "$OUT"
sed -n '1,120p' "$SRC/NDSCart/CartRetailIR.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## who calls the IR device? ##########" >> "$OUT"
grep -rn 'IRDevice\|CartRetailIR\|Addon_IR\|SetIR' "$SRC/NDS.cpp" "$SRC/NDSCart.cpp" "$SRC/SPI.cpp" 2>/dev/null | head -20 >> "$OUT"

wc -l "$OUT"
cat "$OUT"
