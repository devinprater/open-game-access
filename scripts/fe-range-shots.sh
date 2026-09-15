#!/usr/bin/env bash
# fe-range-shots.sh — capture SEVERAL frames in ONE run and convert them all.
#
# ⛔ ONE RUN, MANY SHOTS. Each run costs ~6 minutes (full emulation to the frame), so
# capturing frames one at a time to hunt for a move preview is wasteful. A plan can hold
# multiple SHOT directives; they are sorted into frame order and the run walks them.
#
# ⛔ AND EACH SHOT MUST BE INSIDE THE LOOP: fedump advances `for (i = 0; i < frames; i++)`,
# so a SHOT at exactly `frames` is never reached and silently writes nothing.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
ROM="${ROM:-$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds}"
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
FRAMES="${1:-9000}"
SHOT_FRAMES="${2:-2000 3500 5000 6500 8000}"
OUTDIR="${3:-$HOME/fe/out/rangeshots}"

[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }
mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/*.ppm "$OUTDIR"/*.png 2>/dev/null

PLAN="$ROOT/fe/plans/multishot.txt"
{
  grep -v '^SHOT' "$ROOT/fe/plans/tutorial2.txt"
  for f in $SHOT_FRAMES; do
    [ "$f" -lt "$FRAMES" ] || continue
    echo "SHOT $f $OUTDIR/f$f.ppm"
  done
} | sort -k2,2n -s > "$PLAN"
echo "== shots: $(grep -c '^SHOT' "$PLAN") at frames [$SHOT_FRAMES], run to $FRAMES =="

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
timeout 1500 ./Vendor/fedump "$ROM" "$FRAMES" "$PLAN" 2>&1 | grep -E '\[shot\]|Cursor|TERRAIN' | tail -20

echo
echo "== converting =="
for p in "$OUTDIR"/*.ppm; do
  [ -f "$p" ] || continue
  python3 "$ROOT/scripts/ppm2png.py" "$p" "${p%.ppm}.png" 2>&1 | tail -1
done
echo "== results =="
ls -la "$OUTDIR"/*.png 2>/dev/null | awk '{print $5, $NF}'
# Copy out where the vision tool can reach them on Windows.
WINDEST="/mnt/c/Users/Devin Prater/AppData/Local/Temp/fe-range"
mkdir -p "$WINDEST" 2>/dev/null && cp "$OUTDIR"/*.png "$WINDEST"/ 2>/dev/null && \
  echo "copied to $WINDEST"
