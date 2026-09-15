#!/usr/bin/env bash
# fe-terrain2.sh — dump the FE11 terrain category table, movement cost matrix, and the
# movement-range bitmap.
#
# ⛔ PA_SHIM IS SET HERE, NOT INLINE. Setting it as part of a one-liner over the
# Windows->WSL boundary breaks on the space in "Devin Prater": the export lands with an
# unquoted path and the core then fails with
#   start: Script error: main.lua:3: attempt to index a nil value (global 'emu')
# which looks like a broken script rather than a quoting bug.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-7000}"
PLAN="${2:-tutorial2}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no PA_SHIM at $PA_SHIM" >&2; exit 2; }
unset SAVE

PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

bash "$ROOT/scripts/build-host.sh" || exit 1
echo "== rebuilding feterrain2"
BUILD_LOG="$(mktemp)"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/feterrain2 Core/feterrain2.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ -s "$BUILD_LOG" ] && grep -qE '\berror\b' "$BUILD_LOG"; then
  echo "!! compile failed:" >&2; grep -E '\berror\b' "$BUILD_LOG" | head -20 >&2
  rm -f "$BUILD_LOG"; exit 1
fi
rm -f "$BUILD_LOG"
[ -x Vendor/feterrain2 ] || { echo "!! feterrain2 did not link" >&2; exit 1; }

timeout 1200 ./Vendor/feterrain2 "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" \
  "$FRAMES" "$PLANFILE" 2>&1
