#!/usr/bin/env bash
# sim-boot.sh — iOS core (host build) with REAL BIOS+firmware and the REAL
# accessibility script. First gate: does the console reach graphics setup and
# does the script load without a Lua error?
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/fwtest.cpp" Core/
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pokecore.cpp" Core/
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Sources/CPokeCore/include/pokecore.h" Sources/CPokeCore/include/

# Rebuild the glue with the CURRENT core sources' semantics.
g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -8
echo "pokecore rebuild: $([ -f Vendor/hostobj/pokecore.o ] && echo ok || echo FAIL)"

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simboot Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "simboot linked: $([ -x Vendor/simboot ] && echo yes || echo NO)"

# the real combined script: shim + main.lua, same order the app uses
cat Sources/PokemonAccess/Resources/bizhawk_compat.lua > /tmp/combined.lua
printf '\n' >> /tmp/combined.lua
cat Sources/PokemonAccess/Resources/main.lua >> /tmp/combined.lua
wc -c /tmp/combined.lua

echo
echo "############ REAL BIOS + FIRMWARE + REAL SCRIPT, 30000 frames ############"
export PA_SCRIPT=/tmp/combined.lua
timeout 500 ./Vendor/simboot "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 30000 2>&1 \
  | tail -40
echo "EXIT=$?"
