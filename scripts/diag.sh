#!/usr/bin/env bash
# diag.sh — inspect the interpreter's idle-loop handling in the vendored fork.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== ARMInterpreter_Branch.cpp: A_B ==="
sed -n '1,60p' "$SRC/ARMInterpreter_Branch.cpp"

echo
echo "=== IdleLoop / StopExecution mentions in Branch ==="
grep -n 'IdleLoop\|StopExecution\|Halted' "$SRC/ARMInterpreter_Branch.cpp" | head

echo
echo "=== is JIT_ENABLED in the iOS build? ==="
grep -n 'JIT_ENABLED\|JIT' "$HOME/pokemon-access-ios/scripts/build-core.sh" | head

echo
echo "=== NDS.cpp RunFrame: the ARM7 inner loop ==="
grep -n 'while (ARM7Timestamp < target)' -A 12 "$SRC/NDS.cpp" | head -20
