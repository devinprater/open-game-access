#!/usr/bin/env bash
# fwtest.sh — boot Pokémon Black with REAL bios7/bios9/firmware and compare
# against the FreeBIOS direct-boot path.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

# stage the downloaded dumps
mkdir -p "$HOME/ds-bios"
for f in bios7.bin bios9.bin firmware.bin; do
  cp "/mnt/c/Users/Devin Prater/AppData/Local/Temp/ndsbios/$f" "$HOME/ds-bios/$f" 2>/dev/null
done
ls -la "$HOME/ds-bios/"

cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/fwtest.cpp" Core/
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/pokecore.cpp" Core/
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Sources/CPokeCore/include/pokecore.h" Sources/CPokeCore/include/

# rebuild pokecore (it changed)
g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -8
echo "pokecore rebuild: $([ -f Vendor/hostobj/pokecore.o ] && echo ok || echo FAIL)"

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/fwtest Core/fwtest.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "fwtest linked: $([ -x Vendor/fwtest ] && echo yes || echo NO)"

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
echo
echo "############ WITH REAL FIRMWARE + BIOS ############"
timeout 300 ./Vendor/fwtest "$HOME/hosttest-data/black.nds" \
  "$HOME/ds-bios/bios9.bin" "$HOME/ds-bios/bios7.bin" "$HOME/ds-bios/firmware.bin" 30000 \
  2>&1 | tail -25
