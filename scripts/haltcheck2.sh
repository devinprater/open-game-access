#!/usr/bin/env bash
# haltcheck2.sh — where does Halted get set, and what sets StopExecution?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== ARM.h around Halt()/StopExecution ==="
grep -n 'Halt\|StopExecution\|IdleLoop' "$SRC/ARM.h" | head -20

echo
echo "=== callers of ->Halt() or .Halt() across the core ==="
grep -rn '\->Halt()\|\.Halt()' "$SRC" | head -20

echo
echo "=== StopExecution assignments ==="
grep -rn 'StopExecution' "$SRC" | grep -v 'ARM.cpp' | head -20

echo
echo "=== does the ARM7 HALT instruction exist in the interpreter tables? ==="
grep -rn 'A_HALT\|T_HALT' "$SRC" | head -10
