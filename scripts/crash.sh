#!/usr/bin/env bash
# crash.sh — the interpreter runs real code then dumps core. Get the fault.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

export PA_SHIM="$HOME/pokemon-access-ios/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope >/dev/null 2>&1

timeout 300 gdb -batch \
  -ex 'set pagination off' \
  -ex run \
  -ex 'bt 20' \
  -ex 'info registers' \
  --args ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/crash.log" 2>&1
echo "gdb exit=$?"
grep -vE '^\[log\]|^\[SPEAK\]|^$' "$HOME/crash.log" | tail -45
