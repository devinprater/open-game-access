#!/usr/bin/env bash
# probe-long.sh — does the first frame EVER finish? One frame, generous timeout,
# with progress reported by the harness around the single poke_frame() call.
set -uo pipefail
cd "$HOME/pokemon-access-ios"

ROM="$HOME/hosttest-data/black.nds"

echo "### single frame, 600s budget, no-op script ###"
timeout 600 ./Vendor/timingtest "$ROM" "$HOME/hosttest-data/noop.lua" 1 > "$HOME/probe-1frame.log" 2>&1
echo "exit=$? (124=still stuck after 600s)"
tail -5 "$HOME/probe-1frame.log"
