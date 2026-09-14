#!/usr/bin/env bash
# sim-play.sh — drive the iOS core from title screen into the overworld and
# exercise the reading hotkeys there.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-40000}"
cd "$ROOT" || exit 1

for f in pokecore.cpp poke_platform.cpp simrun.cpp simplay.cpp; do
  cp "/mnt/c/Users/Devin Prater/open-game-access/Core/$f" Core/ 2>/dev/null
done
cp "/mnt/c/Users/Devin Prater/open-game-access/Sources/CPokeCore/include/pokecore.h" Sources/CPokeCore/include/

g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -8
echo "pokecore: $([ -f Vendor/hostobj/pokecore.o ] && echo ok || echo FAIL)"

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simplay Core/simplay.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/simplay ] || { echo "!! did not link"; exit 1; }
echo "simplay: linked"

cat Sources/OpenGameAccess/Resources/bizhawk_compat.lua > /tmp/combined.lua
printf '\n' >> /tmp/combined.lua
cat Sources/OpenGameAccess/Resources/main.lua >> /tmp/combined.lua

export PA_SCRIPT=/tmp/combined.lua
echo
echo "===== scripted playthrough, $FRAMES frames ====="
timeout 900 ./Vendor/simplay "$HOME/hosttest-data/black.nds" "$FRAMES" 2>&1 | tail -70
echo "EXIT=$?"
