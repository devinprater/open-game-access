#!/usr/bin/env bash
# fe-show-terrain2.sh — display sections of a saved feterrain2 dump.
# Takes the file path as $1 so no shell variable has to survive the WSL boundary.
set -uo pipefail
F="${1:-$HOME/fe/out/terrain2-6000.txt}"
[ -f "$F" ] || { echo "!! no dump at $F" >&2; exit 2; }
echo "file: $F  ($(wc -l < "$F") lines)"
echo
echo "################ INTERPRETATION B (the correct one) ################"
sed -n '/INTERPRETATION B/,/^$/p' "$F" | head -40
echo
echo "################ map state ################"
sed -n '/---- map state/,/---- units/p' "$F" | head -40
echo
echo "################ units ################"
sed -n '/---- units ----/,$p' "$F" | head -15
