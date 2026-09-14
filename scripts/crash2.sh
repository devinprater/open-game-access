#!/usr/bin/env bash
# crash2.sh — capture the interpreter's crash cleanly.
set -uo pipefail
cd "$HOME/pokemon-access-ios"
export PA_SHIM="$HOME/pokemon-access-ios/Sources/PokemonAccess/Resources/bizhawk_compat.lua"

echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope >/dev/null 2>&1

echo "=== run under gdb (300s cap) ==="
timeout 320 gdb -batch \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex run \
  -ex 'bt 25' \
  --args ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/crash2.log" 2>&1
echo "gdb exit=$?"
echo
echo "=== non-log output ==="
grep -vE '^\[log\]|^\[SPEAK\]|^$' "$HOME/crash2.log" | tail -45
