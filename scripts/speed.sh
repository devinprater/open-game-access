#!/usr/bin/env bash
# speed.sh — measure interpreter throughput: how long does ONE frame take?
set -uo pipefail
cd "$HOME/pokemon-access-ios"

# 1 frame with a generous budget; timingtest prints per-frame time only if >0.5s,
# so also take wall-clock around the whole call.
for N in 1 5 20; do
  echo "=== $N frame(s), no-op script ==="
  START=$(date +%s)
  timeout 300 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" "$N" \
    > "$HOME/speed-$N.log" 2>&1
  RC=$?
  END=$(date +%s)
  echo "exit=$RC wall=$((END-START))s"
  grep -E 'frames in|took|stopped' "$HOME/speed-$N.log" | tail -4
  echo
done
