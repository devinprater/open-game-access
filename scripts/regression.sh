#!/usr/bin/env bash
# regression.sh — the core-level regression gate after the Android findings are
# ported. Two ROMs, because the IR fix touches one code path and not the other:
#   * Pokemon Black  (IR cart)      — must still boot, render and narrate
#   * Pokemon Diamond (non-IR cart) — must still boot and render
# A fix that repairs one cart and breaks the other is not a fix.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1

FRAMES="${FRAMES:-30000}"

bash "$ROOT/scripts/build-host.sh" || exit 1

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simboot Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/simboot ] || { echo "!! simboot did not link"; exit 1; }

cat Sources/PokemonAccess/Resources/bizhawk_compat.lua > /tmp/combined.lua
printf '\n' >> /tmp/combined.lua
cat Sources/PokemonAccess/Resources/main.lua >> /tmp/combined.lua
export PA_SCRIPT=/tmp/combined.lua

echo
echo "############ POKEMON BLACK (IR cart) with the real script, $FRAMES frames ############"
timeout 700 ./Vendor/simboot "$HOME/hosttest-data/black.nds" - - - "$FRAMES" 2>&1 | tail -18

echo
echo "############ POKEMON DIAMOND (non-IR cart), $FRAMES frames ############"
DIAMOND="/mnt/c/Users/Devin Prater/Dropbox/Games/NDS/Pokemon - Diamond Version (USA) (Rev 5).nds"
if [ -f "$DIAMOND" ]; then
  cp "$DIAMOND" "$HOME/hosttest-data/diamond.nds"
  timeout 700 ./Vendor/simboot "$HOME/hosttest-data/diamond.nds" - - - "$FRAMES" 2>&1 | tail -14
else
  echo "!! diamond ROM not found"
fi
