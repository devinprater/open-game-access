#!/usr/bin/env bash
# fe-save-shots.sh — boot the save, press through, and screenshot at several frames.
#
# Screenshots are the only way to tell WHICH screen the game is on: the preparations
# screen and a loaded map look identical from memory (units are in their forces either
# way) but gMapStateManager only exists on the map.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
FRAMES="${1:-8400}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
if [ -z "${SAVE:-}" ]; then
  for c in "$HOME/fe/saves/fe11-usa-finalboss.sav" "$ROOT/fe/saves/"*.sav; do
    [ -f "$c" ] && { export SAVE="$c"; break; }
  done
fi
echo "save: ${SAVE:-<none>}"

PLAN="$ROOT/fe/plans/save-continue.txt"
[ -f "$PLAN" ] || { echo "!! no plan $PLAN" >&2; exit 2; }

mkdir -p "$HOME/fe/out"
PLAN_DEBUG=1 FACTION_TRACE=1 timeout 1500 ./Vendor/fedump \
  "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$PLAN" 2>&1 \
  | grep -E "save\]|plan\]|shot\]|fac\]|Cursor"

echo
echo "== converting shots =="
for p in "$HOME"/fe/out/sv-*.ppm; do
  [ -f "$p" ] || continue
  n="$(basename "$p" .ppm)"
  python3 "$ROOT/scripts/ppm2png.py" "$p" "/mnt/c/Users/Devin Prater/AppData/Local/Temp/fesave/shots/$n.png" 2>&1 | tail -1
done
