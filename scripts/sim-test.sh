#!/usr/bin/env bash
# sim-test.sh — the iOS port's test harness on this machine.
#
# An iOS Simulator cannot run here: the simulator runtime boots an iOS userland
# that is only shipped for macOS (simctl/CoreSimulator + a macOS-only runtime
# sysroot), and nothing on Windows/Linux can execute it. What CAN be done
# exactly is everything the app does below UIKit: the same melonDS+Lua sources,
# the same C ABI, the same bundled script, the same once-per-frame pacing.
#
# Usage: sim-test.sh [frames] [bootframes]
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-30000}"
BOOT="${2:-20000}"
cd "$ROOT" || exit 1

# Windows is the source of truth; mirror it in.
for f in pokecore.cpp poke_platform.cpp simrun.cpp; do
  cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/$f" Core/ 2>/dev/null
done
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Sources/CPokeCore/include/pokecore.h" Sources/CPokeCore/include/

g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -8
echo "pokecore: $([ -f Vendor/hostobj/pokecore.o ] && echo ok || echo FAIL)"

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simrun Core/simrun.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/simrun ] || { echo "!! simrun did not link"; exit 1; }
echo "simrun: linked"

# The app's own concatenation: shim first, then main.lua, one chunk.
cat Sources/PokemonAccess/Resources/bizhawk_compat.lua > /tmp/combined.lua
printf '\n' >> /tmp/combined.lua
cat Sources/PokemonAccess/Resources/main.lua >> /tmp/combined.lua
echo "script: $(wc -c < /tmp/combined.lua) bytes"
echo
echo "===== DIRECT BOOT (what the app does) + real script + hotkeys, $FRAMES frames ====="
export PA_SCRIPT=/tmp/combined.lua
timeout 900 ./Vendor/simrun "$HOME/hosttest-data/black.nds" - - - "$FRAMES" "$BOOT" 2>&1 | tail -50
echo "EXIT=$?"
