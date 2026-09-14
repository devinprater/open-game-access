#!/usr/bin/env bash
# fe-shot.sh — run an existing plan and capture a screenshot at the end.
#
# ⛔ WHY A SCRIPT AND NOT AN INLINE COMMAND. `export PA_SHIM=$PWD/Sources/...` fails
# through the Windows→WSL boundary because the path contains a space, and the error
# ("not a valid identifier") points at the export rather than at the quoting. A script
# file sidesteps it entirely and makes the run reproducible.
#
#   fe-shot.sh <plan> <frames> [shot-frame] [out.ppm]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
PLAN_NAME="${1:-units}"
FRAMES="${2:-12000}"
SHOT_AT="${3:-$((FRAMES - 100))}"
OUTPPM="${4:-$HOME/fe/out/end.ppm}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no PA_SHIM at $PA_SHIM" >&2; exit 2; }

PLAN="$ROOT/fe/plans/$PLAN_NAME.txt"
[ -f "$PLAN" ] || { echo "!! no plan $PLAN" >&2; exit 2; }

# Append a SHOT to a copy so the original plan is untouched.
TMPPLAN="$(mktemp)"
grep -v '^SHOT' "$PLAN" > "$TMPPLAN"
echo "SHOT $SHOT_AT $OUTPPM" >> "$TMPPLAN"
echo "plan: $PLAN  (+ SHOT at $SHOT_AT -> $OUTPPM)"

mkdir -p "$(dirname "$OUTPPM")"
timeout 1200 ./Vendor/fedump "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" \
  "$FRAMES" "$TMPPLAN" 2>&1 | grep -E "shot|snap|fac\]|FAIL|error"
rm -f "$TMPPLAN"

if [ -f "$OUTPPM" ]; then
  echo "captured: $OUTPPM ($(stat -c%s "$OUTPPM") bytes)"
  python3 "$ROOT/scripts/ppm2png.py" "$OUTPPM" "${OUTPPM%.ppm}.png" 2>&1 | tail -1
else
  echo "!! no screenshot written" >&2
fi
