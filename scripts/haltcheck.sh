#!/usr/bin/env bash
# haltcheck.sh — how does the fork implement HALT, and is it reachable?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== files ==="
ls "$SRC" | head -5

echo
echo "=== Halted assignments anywhere in the core ==="
grep -rn 'Halted = ' "$SRC"/*.cpp "$SRC"/*.h 2>/dev/null | head -20

echo
echo "=== HALT instruction handlers ==="
grep -rn 'A_HALT\|T_HALT' "$SRC"/ARMInterpreter*.cpp "$SRC"/ARMInterpreter*.h 2>/dev/null | head -10

echo
echo "=== NDS::Halt definition ==="
grep -n 'void NDS::Halt' -A 8 "$SRC/NDS.cpp" | head -14

echo
echo "=== StopExecution / IdleLoop in the interpreter path ==="
grep -rn 'IdleLoop\|StopExecution' "$SRC/ARM.cpp" | head -10
