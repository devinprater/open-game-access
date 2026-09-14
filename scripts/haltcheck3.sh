#!/usr/bin/env bash
# haltcheck3.sh — compare this fork's HALT handling with upstream.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== ARMInterpreter files present ==="
ls "$SRC" | grep -i 'ARMInterpreter'

echo
echo "=== instruction table construction in ARMInterpreter.cpp ==="
grep -n 'InstrTable' "$SRC/ARMInterpreter.cpp" | head -10

echo
echo "=== any HALT symbol in the whole source tree ==="
grep -rn 'HALT' "$SRC" | head -10

echo
echo "=== what the interpreter does when Halted is set (ARM.cpp 590-615) ==="
sed -n '590,615p' "$SRC/ARM.cpp"

echo
echo "=== ARM.cpp 715-750 (ARM9 loop tail) ==="
sed -n '715,750p' "$SRC/ARM.cpp"
