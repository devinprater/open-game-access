#!/usr/bin/env bash
# fe-enemy.sh — verify enemy detection against a real save, then fall back to a play plan.
#
# ⛔ WHY. Enemy detection cannot be verified on the Prologue: those maps have no
# hostile force, so `Next enemy` returning none is CORRECT and proves nothing. This
# boots with a save file (SAVE=<path>) so the game starts in a chapter that has
# enemies, making the enemy path actually execute.
#
#   fe-enemy.sh [frames] [plan]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-6000}"
PLAN="${2:-units}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }

# A save is optional; auto-detect one so this works without extra arguments.
if [ -z "${SAVE:-}" ]; then
  for c in "$HOME/fe/saves/fe11-usa-finalboss.sav" "$ROOT/fe/saves/"*.sav; do
    [ -f "$c" ] && { export SAVE="$c"; break; }
  done
fi
if [ -n "${SAVE:-}" ]; then
  echo "save: $SAVE ($(stat -c%s "$SAVE") bytes)"
  head -c 4 "$SAVE" | xxd | head -1
else
  echo "save: (none — the game will boot from the title screen)"
fi

PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

bash "$ROOT/scripts/build-host.sh" || exit 1

echo "== rebuilding fedump"
BUILD_LOG="$(mktemp)"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ -s "$BUILD_LOG" ] && grep -qE '\berror\b' "$BUILD_LOG"; then
  echo "!! compile failed:" >&2; grep -E '\berror\b' "$BUILD_LOG" | head -20 >&2
  rm -f "$BUILD_LOG"; exit 1
fi
rm -f "$BUILD_LOG"
[ -x Vendor/fedump ] || { echo "!! fedump did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fedump '+%H:%M:%S')"
echo

ROM="$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds"
[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }

FACTION_TRACE=1 timeout 1200 ./Vendor/fedump "$ROM" "$FRAMES" "$PLANFILE" 2>&1
