#!/usr/bin/env bash
# fe-save-run.sh — boot a save and press through the title into the chapter, watching
# the faction census. Uses SAVE=<path> plus a small input plan.
#
# ⛔ WHY INPUT IS STILL NEEDED WITH A SAVE. Loading a save puts the game on the TITLE
# SCREEN with the save data present; the player still has to choose CONTINUE. At 2,500
# frames with no input, gMapStateManager is still NULL and every command correctly
# answers "Not on a map yet" — which is not a reader failure.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-8000}"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
if [ -z "${SAVE:-}" ]; then
  for c in "$HOME/fe/saves/fe11-usa-finalboss.sav" "$ROOT/fe/saves/"*.sav; do
    [ -f "$c" ] && { export SAVE="$c"; break; }
  done
fi
echo "save: ${SAVE:-<none>}"

# Title -> CONTINUE -> through the save-slot screens. Mashed A/START, which is what
# advances both menus and dialogue.
PLAN="$(mktemp)"
{
  for f in 200 400 600 800 1000 1200 1400 1600 1800 2000 2200 2400 2600 2800 3000; do
    echo "KEY $f A 1"; echo "KEY $((f + 10)) A 0"
  done
  echo "KEY 3200 START 1"; echo "KEY 3240 START 0"
  for f in 3400 3600 3800 4000 4200 4400 4600 4800 5000; do
    echo "KEY $f A 1"; echo "KEY $((f + 10)) A 0"
  done
} > "$PLAN"

PLAN_DEBUG=1 FACTION_TRACE=1 timeout 1200 ./Vendor/fedump \
  "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$PLAN" 2>&1 \
  | grep -E "save\]|plan\]|fac\]|faction [0-9]|first enemy|Where am I"
rc=${PIPESTATUS[0]}
rm -f "$PLAN"
exit "$rc"
