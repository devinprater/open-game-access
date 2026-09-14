#!/usr/bin/env bash
# fe-dump.sh — read Fire Emblem: Shadow Dragon's tactical state via the game's
# own structures (symbols from the fe11-us decompilation).
#
# Usage: fe-dump.sh [plan-name] [frames]
#   plan-name is a file in fe/plans/ WITHOUT the .txt (default: skip-intro)
#   Passing a path is deliberately not supported: the Windows side runs this
#   through MSYS, which rewrites absolute paths into /c/... form, and WSL cannot
#   open those — the plan silently goes missing and NOTHING gets pressed, which
#   looks exactly like "the game did not advance".
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"

bash "$ROOT/scripts/build-host.sh" || exit 1
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fedump Core/fedump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -10
[ -x Vendor/fedump ] || { echo "!! fedump did not link"; exit 1; }

PLAN="${1:-skip-intro}"
FRAMES="${2:-4000}"
planFile="$ROOT/fe/plans/$PLAN.txt"
[ -f "$planFile" ] || { echo "!! no plan: $planFile" >&2; exit 2; }
mkdir -p "$HOME/fe/out"
echo "== plan: $PLAN  ($FRAMES frames)"
timeout 900 ./Vendor/fedump "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$planFile" 2>&1 | tail -60
echo
echo "== snapshots taken =="
ls -la "$HOME/fe/out"/*.ram 2>/dev/null | tail -12
