#!/usr/bin/env bash
# samplepc.sh — is the emulator progressing (slow) or truly frozen (hang)?
# Samples the guest PC several times; an advancing PC means slow, a constant
# one means a loop the interpreter never escapes.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/sample.log" 2>&1 &
HPID=$!
sleep 20

if ! kill -0 "$HPID" 2>/dev/null; then
  echo "finished on its own:"; tail -4 "$HOME/sample.log"; exit 0
fi

for i in 1 2 3 4; do
  echo "--- sample $i ---"
  timeout 25 gdb -batch -p "$HPID" \
    -ex 'set pagination off' \
    -ex 'printf "PC=%08x CPSR=%08x\n", $pc, $cpsr' 2>/dev/null \
    | grep -E '^PC=' || echo "PC=? (unavailable)"
  sleep 6
done

kill "$HPID" 2>/dev/null
