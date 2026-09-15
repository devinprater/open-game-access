#!/usr/bin/env bash
# fe-range-shot.sh — screenshot the game at the SAME frame the reader reports its
# computed movement range, so the number can be checked against the blue overlay the
# game actually draws.
#
# Why this matters: "36 squares reachable" is a computed claim. A movement budget of 7
# on open ground would give ~113 tiles (a Manhattan diamond of radius 7), so 36 is only
# right if walls genuinely confine Marth. Comparing against the drawn overlay is the
# difference between a plausible number and a verified one.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
ROM="${ROM:-$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds}"
FRAMES="${1:-6000}"
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"

[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }

# A plan that is tutorial2 plus a SHOT at the frame under test.
# ⛔ SORT THE PLAN. Appending "SHOT 6000" to a plan whose later KEY events run to
# frame 15740 leaves the directive out of order; fedump processes events in file order,
# so the shot is scheduled after everything else and no screenshot is written.
# ⛔ AND THE SHOT FRAME MUST BE INSIDE THE LOOP. fedump advances `for (i = 0; i < frames;
# i++)`, so a SHOT at exactly `frames` is never reached and silently produces nothing.
# The shot goes one frame before the end.
PLAN="$ROOT/fe/plans/t2shot.txt"
SHOT_AT=$(( FRAMES - 1 ))
mkdir -p "$HOME/fe/out"
{
  grep -v '^SHOT' "$ROOT/fe/plans/tutorial2.txt"
  echo "SHOT $SHOT_AT $HOME/fe/out/rangeshot.ppm"
} | sort -k2,2n -s > "$PLAN"
echo "== plan: $(grep -c . "$PLAN") lines, SHOT at $SHOT_AT (frames=$FRAMES) =="
grep -n "^SHOT" "$PLAN"

echo "== building fedump =="
bash "$ROOT/scripts/build-host.sh" || exit 1
BUILD_LOG="$ROOT/Vendor/fe-range-build.log"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC/src" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if grep -qE '\berror\b' "$BUILD_LOG" 2>/dev/null; then
  echo "!! compile failed:" >&2; grep -E '\berror\b' "$BUILD_LOG" | head -10 >&2; exit 1
fi
[ -x Vendor/fedump ] || { echo "!! fedump did not link" >&2; exit 1; }

echo "== running =="
timeout 1200 ./Vendor/fedump "$ROM" "$FRAMES" "$PLAN" 2>&1 | tail -20

echo
echo "== converting =="
python3 "$ROOT/scripts/ppm2png.py" "$HOME/fe/out/rangeshot.ppm" \
        "$HOME/fe/out/rangeshot.png" 2>&1 | tail -2
ls -la "$HOME/fe/out/rangeshot."* 2>/dev/null
