#!/usr/bin/env bash
# crash.sh — the interpreter runs real code then dumps core. Get the fault.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
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
