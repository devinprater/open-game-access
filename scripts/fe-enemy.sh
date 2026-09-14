#!/usr/bin/env bash
# fe-enemy.sh — find a map that actually has enemies and read them.
#
# ⛔ WHY THIS EXISTS. Enemy detection cannot be verified on chapter 1: its scripted
# maps contain only the player's force, so `Next enemy` returning "No enemies found"
# is CORRECT and proves nothing about the code path. Verifying it needs a map with a
# hostile faction, which means playing further in — so this runs the boot with a
# faction census sampled every 100 frames and reports when enemies appear.
#
#   fe-enemy.sh [frames] [plan]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-20000}"
PLAN="${2:-units}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }

PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

bash "$ROOT/scripts/build-host.sh" || exit 1

echo "== rebuilding fedump"
BUILD_LOG="$(mktemp)"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ -s "$BUILD_LOG" ] && grep -qE '\berror\b' "$BUILD_LOG"; then
  echo "!! compile failed — refusing to run a stale binary:" >&2
  grep -E '\berror\b' "$BUILD_LOG" | head -20 >&2
  rm -f "$BUILD_LOG"; exit 1
fi
rm -f "$BUILD_LOG"
[ -x Vendor/fedump ] || { echo "!! fedump did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fedump '+%H:%M:%S')"
echo

ROM="$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds"
[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }

# FACTION_TRACE makes fedump print the census whenever it changes.
FACTION_TRACE=1 timeout 1200 ./Vendor/fedump "$ROM" "$FRAMES" "$PLANFILE" 2>&1
