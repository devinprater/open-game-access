#!/usr/bin/env bash
# where.sh — run hosttest, then dump where it is stuck with gdb.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
./Vendor/hosttest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/script.lua" 1500 900 \
  > "$HOME/hosttest-run7.log" 2>&1 &
HPID=$!
echo "started pid=$HPID"
sleep 70
if kill -0 "$HPID" 2>/dev/null; then
  echo "=== still running; backtrace ==="
  timeout 30 gdb -batch -p "$HPID" -ex 'bt 18' 2>&1 | tail -24
  kill "$HPID" 2>/dev/null
else
  echo "=== exited on its own ==="
fi
echo
echo "=== log so far ==="
head -40 "$HOME/hosttest-run7.log"
