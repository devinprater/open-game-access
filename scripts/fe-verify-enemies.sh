#!/usr/bin/env bash
# fe-verify-enemies.sh — prove enemy detection end-to-end with the accessibility
# commands, on the PROLOGUE (no save needed).
#
# ⛔ HISTORY. This used to require a save file, because no enemy had ever appeared. The
# real blocker was input: the Prologue's "Waiting" tutorial popup swallows map input, so
# a plan that never presses B re-reads the popup forever and the enemy phase is never
# reached. Adding B (which the popup's own text tells you to press: "You can also press
# B ... to cancel the move") unblocks it, and the Prologue's enemies appear at ~frame
# 5900. See fe/plans/tutorial2.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-8000}"
PLAN_NAME="${2:-tutorial2}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no PA_SHIM at $PA_SHIM" >&2; exit 2; }
unset SAVE      # the Prologue is played from the title; no save required

PLAN="$ROOT/fe/plans/$PLAN_NAME.txt"
[ -f "$PLAN" ] || { echo "!! no plan $PLAN" >&2; exit 2; }
echo "plan: $PLAN_NAME   frames: $FRAMES"

bash "$ROOT/scripts/build-host.sh" || exit 1
echo "== rebuilding fe_access"
BUILD_LOG="$(mktemp)"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fe_access Core/fe_access.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ -s "$BUILD_LOG" ] && grep -qE '\berror\b' "$BUILD_LOG"; then
  echo "!! compile failed:" >&2; grep -E '\berror\b' "$BUILD_LOG" | head -20 >&2
  rm -f "$BUILD_LOG"; exit 1
fi
rm -f "$BUILD_LOG"
[ -x Vendor/fe_access ] || { echo "!! fe_access did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fe_access '+%H:%M:%S')"
echo

timeout 1200 ./Vendor/fe_access \
  "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$PLAN" 2>&1 | tail -22
