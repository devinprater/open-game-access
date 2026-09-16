#!/usr/bin/env bash
# fe-tutorial.sh — play the Prologue tutorial plan and screenshot each beat.
#
# ⛔ WHY SCREENSHOTS PER BEAT. The Prologue is a chain of scripted movement tutorials
# and each one asks for a specific move. Memory can tell you the cursor moved and that
# the map is loaded; it cannot tell you WHICH tutorial is on screen, because that text
# is rendered on the top screen. So a plan is judged by looking at the beats, not by a
# frame count.
#
#   fe-tutorial.sh [frames]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
FRAMES="${1:-15800}"
PLAN="$ROOT/fe/plans/tutorial.txt"
[ -f "$PLAN" ] || { echo "!! no plan $PLAN" >&2; exit 2; }

# Always set PA_SHIM from a real path here; setting it inline over the Windows->WSL
# boundary breaks on the space in "Devin Prater".
export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no PA_SHIM at $PA_SHIM" >&2; exit 2; }
unset SAVE      # the Prologue is played from the title screen, no save

echo "plan : tutorial ($(grep -c '^KEY' "$PLAN") key events)"
mkdir -p "$HOME/fe/out"

FACTION_TRACE=1 timeout 1800 ./Vendor/fedump \
  "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$PLAN" 2>&1 \
  | grep -E "shot\]|fac\]|Cursor|TERRAIN|TERRAIN at|start fail|load fail"

echo
echo "== converting beats =="
bash "$ROOT/scripts/fe-convert-shots.sh" 2>&1 | tail -3
