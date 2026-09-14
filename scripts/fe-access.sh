#!/usr/bin/env bash
# fe-access.sh — build and run the Fire Emblem accessibility prototype.
#   fe-access.sh <plan-name> <frames>
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
PLAN="${1:-units}"
FRAMES="${2:-5000}"
PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }
mkdir -p "$HOME/fe/out"

echo "== building fe_access"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$HOME/src/melonds-lua/src" -std=c++17 \
  -o Vendor/fe_access Core/fe_access.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -12
[ -x Vendor/fe_access ] || { echo "!! fe_access did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fe_access '+%H:%M:%S')"
echo

timeout 900 ./Vendor/fe_access "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" \
  "$FRAMES" "$PLANFILE" 2>&1
