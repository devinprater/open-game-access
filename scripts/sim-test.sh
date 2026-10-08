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
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
FRAMES="${1:-30000}"
BOOT="${2:-20000}"
cd "$ROOT" || exit 1

# ⛔ There is no Windows tree. This used to `cp` pokecore.cpp, poke_platform.cpp,
# simrun.cpp and pokecore.h in from /mnt/c/.../open-game-access with 2>/dev/null
# and no error check, so the copy silently did nothing and the script built
# whatever was already in Core/ — correct output by accident, from a file that
# no longer exists. The checkout this script lives in is the source of truth.

g++ -O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -ICore -ISources/CPokeCore/include -I"$SRC" -I"$HOME/src/lua-5.4.7/src" -I"$SRC/teakra/include" \
  -std=c++17 -c Core/pokecore.cpp -o Vendor/hostobj/pokecore.o 2>&1 | grep -E '\berror\b' | head -8
echo "pokecore: $([ -f Vendor/hostobj/pokecore.o ] && echo ok || echo FAIL)"

g++ -O2 -g -DPOKE_HOST=1 -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/simrun Core/simrun.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/simrun ] || { echo "!! simrun did not link"; exit 1; }
echo "simrun: linked"

# The app's own concatenation: shim first, then main.lua, one chunk.
COMBINED="$(mktemp)"
cat Sources/OpenGameAccess/Resources/bizhawk_compat.lua > "$COMBINED"
printf '\n' >> "$COMBINED"
cat Sources/OpenGameAccess/Resources/main.lua >> "$COMBINED"
echo "script: $(wc -c < "$COMBINED") bytes"
echo
echo "===== DIRECT BOOT (what the app does) + real script + hotkeys, $FRAMES frames ====="
export PA_SCRIPT="$COMBINED"
timeout 900 ./Vendor/simrun "$HOME/hosttest-data/black.nds" - - - "$FRAMES" "$BOOT" 2>&1 | tail -50
echo "EXIT=$?"
