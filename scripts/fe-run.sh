#!/usr/bin/env bash
# fe-run.sh — run the freshly built fedump with the correct shim, no shell games.
#   fe-run.sh <plan-name> <frames>
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
PLAN="${1:-units}"
FRAMES="${2:-5000}"
PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }
echo "shim: $(wc -c < "$PA_SHIM") bytes"
mkdir -p "$HOME/fe/out"

# ⛔ REBUILD. The first version of this script ran whatever Vendor/fedump already
# existed, which silently produced output from the PREVIOUS source revision — the
# run looked successful and the new instrumentation was simply absent.
echo "== rebuilding fedump"
# ⛔ DO NOT pipe the compiler through grep. The previous version did, so a compile
# error printed NOTHING, the old binary stayed in place, and the run proceeded
# against stale code — reporting the pre-fix result as if it were the new one. That
# cost several full emulator runs chasing a bug that had already been fixed. Capture
# the log, report errors, and exit non-zero.
BUILD_LOG="$(mktemp)"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$HOME/src/melonds-lua/src" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ -s "$BUILD_LOG" ] && grep -qE '\berror\b' "$BUILD_LOG"; then
  echo "!! compile failed — refusing to run a stale binary:" >&2
  grep -E '\berror\b' "$BUILD_LOG" | head -20 >&2
  rm -f "$BUILD_LOG"
  exit 1
fi
[ -s "$BUILD_LOG" ] && grep -E 'warning' "$BUILD_LOG" | head -3
rm -f "$BUILD_LOG"
[ -x Vendor/fedump ] || { echo "!! fedump did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fedump '+%H:%M:%S')"

timeout 900 ./Vendor/fedump "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" \
  "$FRAMES" "$PLANFILE" 2>&1
