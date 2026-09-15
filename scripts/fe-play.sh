#!/usr/bin/env bash
# fe-play.sh — play Fire Emblem's Prologue with a generated plan and watch the
# faction census, so enemy detection can be exercised on real data.
#
# ⛔ WHY THIS EXISTS. A pure-A-mash plan parks the game on the Prologue movement
# tutorial ("Marth can move anywhere within the blue area"), which waits for the unit
# to actually MOVE. That is why a 15,500-frame run showed the census never changing
# (player=1 enemy=0): not "the map has no enemies" but "the tutorial was never
# satisfied". The Prologue maps do genuinely have none; reaching a hostile force means
# finishing the Prologue.
#
#   fe-play.sh [frames] [iterations]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-16000}"
ITER="${2:-18}"
PLAN="$ROOT/fe/plans/play.txt"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }

python3 "$ROOT/scripts/gen-fe-plan.py" "$PLAN" "$ITER" || exit 1

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

ROM="$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds"
[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }

mkdir -p "$HOME/fe/out"
echo
FACTION_TRACE=1 timeout 1500 ./Vendor/fedump "$ROM" "$FRAMES" "$PLAN" 2>&1
