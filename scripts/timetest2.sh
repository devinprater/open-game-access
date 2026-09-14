#!/usr/bin/env bash
# timetest2.sh — time pure emulation vs emulation+script, logging directly to files.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

echo "### WITHOUT script (pure emulation, 600 frames) ###"
timeout 150 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" NOSCRIPT 600 \
  > "$HOME/tt-noscript.log" 2>&1
echo "exit=$?"
tail -6 "$HOME/tt-noscript.log"

echo
echo "### WITH script (600 frames) ###"
timeout 240 ./Vendor/timingtest "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/script.lua" 600 \
  > "$HOME/tt-withscript.log" 2>&1
echo "exit=$?"
tail -6 "$HOME/tt-withscript.log"
