#!/usr/bin/env bash
# halts.sh — is the ARM7 sitting in a HALT/idle state that the interpreter does
# not short-circuit? That is the difference between "slow but working" and
# "waiting forever", and it decides whether this is fixable in the frontend.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== HaltInterrupted definition ==="
grep -n 'HaltInterrupted' -A 12 "$SRC/NDS.cpp" | head -30

echo
echo "=== where Halted is set (interpreter path) ==="
grep -rn 'Halted = ' "$SRC/ARMInterpreter_Misc.cpp" "$SRC/ARMInterpreter.cpp" 2>/dev/null | head -10

echo
echo "=== the HALT instruction handler ==="
grep -rn 'A_HALT\|THUMB_HALT\|Halt(' "$SRC/ARMInterpreter.cpp" "$SRC/ARMInterpreter_Misc.cpp" 2>/dev/null | head -10
