#!/usr/bin/env bash
# samplepc2.sh — after the JIT fix, is frame 1 slow or is it still a runaway PC?
set -uo pipefail
cd "$HOME/pokemon-access-ios"

./Vendor/timingtest "$HOME/hosttest-data/black.nds" - 5 > "$HOME/sample2.log" 2>&1 &
HPID=$!
sleep 20

if ! kill -0 "$HPID" 2>/dev/null; then
  echo "finished on its own:"; tail -4 "$HOME/sample2.log"; exit 0
fi

for i in 1 2 3 4; do
  printf 'sample %d: ' "$i"
  timeout 25 gdb -batch -p "$HPID" -ex 'info registers pc' 2>/dev/null | grep -oE '0x[0-9a-f]+' | head -1
  sleep 5
done

printf 'backtrace:\n'
timeout 25 gdb -batch -p "$HPID" -ex 'bt 6' 2>/dev/null | grep -E '^#[0-9]' | head -6
kill "$HPID" 2>/dev/null
