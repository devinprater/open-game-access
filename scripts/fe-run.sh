#!/usr/bin/env bash
# fe-run.sh — run the freshly built fedump with the correct shim, no shell games.
#   fe-run.sh <plan-name> <frames>
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
cd "$ROOT" || exit 1
PLAN="${1:-units}"
FRAMES="${2:-5000}"
PLANFILE="$ROOT/fe/plans/$PLAN.txt"
[ -f "$PLANFILE" ] || { echo "!! no plan $PLANFILE" >&2; exit 2; }

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
[ -f "$PA_SHIM" ] || { echo "!! no shim at $PA_SHIM" >&2; exit 2; }
echo "shim: $(wc -c < "$PA_SHIM") bytes"
mkdir -p "$HOME/fe/out"

# ⛔ REBUILD. The first version of this script ran whatever Vendor/fedump already
# existed, which silently produced output from the PREVIOUS source revision — the
# run looked successful and the new instrumentation was simply absent.
echo "== rebuilding fedump"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$HOME/src/melonds-lua/src" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -10
[ -x Vendor/fedump ] || { echo "!! fedump did not link" >&2; exit 1; }
echo "built: $(date -r Vendor/fedump '+%H:%M:%S')"

timeout 900 ./Vendor/fedump "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" \
  "$FRAMES" "$PLANFILE" 2>&1
