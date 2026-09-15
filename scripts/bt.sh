#!/usr/bin/env bash
# bt.sh — where does the first frame hang? (yama ptrace_scope relaxed locally)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope >/dev/null 2>&1 && echo "ptrace_scope=0"

./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/bt-run.log" 2>&1 &
HPID=$!
echo "pid=$HPID"
sleep 25

if kill -0 "$HPID" 2>/dev/null; then
  echo "=== stuck; backtrace ==="
  timeout 40 gdb -batch -p "$HPID" -ex 'thread apply all bt 12' 2>&1 | grep -vE '^\[|Reading|^0x0000.*in \?\?' | head -40
  kill "$HPID" 2>/dev/null
else
  echo "=== finished ==="
  tail -5 "$HOME/bt-run.log"
fi
