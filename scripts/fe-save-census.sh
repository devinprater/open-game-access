#!/usr/bin/env bash
# fe-save-census.sh — boot a save and print the faction census + cursor state.
#
# ⛔ Always run this via wsl.sh (or from ~/open-game-access) so PA_SHIM is set from a
# value that survives the Windows->WSL path boundary. Setting it inline as
# `PA_SHIM=$PWD/Sources/...` breaks on the space in "Devin Prater" and the failure
# reads as a script error: main.lua:3 attempt to index a nil value (global 'emu').
#
#   fe-save-census.sh <save-name> [frames] [plan]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SAVE_NAME="${1:-}"
FRAMES="${2:-6000}"
PLAN_NAME="${3:-units}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no PA_SHIM at $PA_SHIM" >&2; exit 2; }

if [ -z "$SAVE_NAME" ]; then
  for c in "$HOME/fe/saves/"*.sav; do [ -f "$c" ] && SAVE_NAME="$(basename "$c")" && break; done
fi
export SAVE="$HOME/fe/saves/$SAVE_NAME"
[ -f "$SAVE" ] || { echo "!! no save at $SAVE" >&2; exit 2; }
echo "save : $SAVE ($(stat -c%s "$SAVE") bytes)"
echo "magic: $(head -c 4 "$SAVE")"

PLAN="$ROOT/fe/plans/$PLAN_NAME.txt"
[ -f "$PLAN" ] || { echo "!! no plan $PLAN" >&2; exit 2; }

echo "plan : $PLAN_NAME ($(grep -c '^KEY' "$PLAN") key events)"
echo
FACTION_TRACE=1 PLAN_DEBUG=1 timeout 1200 ./Vendor/fedump \
  "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$PLAN" 2>&1 \
  | grep -E "save\]|plan\]|fac\]|faction [0-9]|first enemy|start fail|load fail|TERRAIN|Cursor"
