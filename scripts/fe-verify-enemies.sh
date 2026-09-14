#!/usr/bin/env bash
# fe-verify-enemies.sh — prove enemy detection end-to-end with the accessibility
# commands, against a save that contains a real enemy force.
#
# Prints the adapter's own output (`Where am I`, `Next enemy`) so the claim is backed
# by the command a player would actually use, not just by a struct dump.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-2500}"
PLAN_NAME="${2:-units}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }

if [ -z "${SAVE:-}" ]; then
  for c in "$HOME/fe/saves/fe11-usa-finalboss.sav" "$ROOT/fe/saves/"*.sav; do
    [ -f "$c" ] && { export SAVE="$c"; break; }
  done
fi
echo "save: ${SAVE:-<none>}"

PLANFILE="$ROOT/fe/plans/$PLAN_NAME.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

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

ROM="$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds"
timeout 1200 ./Vendor/fe_access "$ROM" "$FRAMES" "$PLANFILE" 2>&1 | tail -25
