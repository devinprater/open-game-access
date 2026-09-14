#!/usr/bin/env bash
# rebuild-platform.sh — recompile just poke_platform.cpp into the archive and relink.
set -uo pipefail
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
unset POKECORE_LIB

SRC="$HOME/src/melonds-lua/src"
OBJ="$HOME/pokemon-access-ios/Vendor/obj"
LIB="$HOME/pokemon-access-ios/Vendor/libpokecore.a"
PROJ="$HOME/pokemon-access-ios"

cd "$PROJ" || exit 1

CXX_FLAGS=(
  -std=c++17 -O2 -fPIC -DPOKE_IOS=1
  -I"$SRC" -I"$PROJ/Core"
  -isysroot "$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo /usr/local/swift)"
)

echo "=== recompiling poke_platform.cpp ==="
clang++ "${CXX_FLAGS[@]}" -c "$PROJ/Core/poke_platform.cpp" -o "$OBJ/poke_platform.cpp.o" 2>&1 | head -20
echo "COMPILE_EXIT=${PIPESTATUS[0]}"
ls -la "$OBJ/poke_platform.cpp.o"

echo
echo "=== updating archive ==="
ar r "$LIB" "$OBJ/poke_platform.cpp.o" && echo "ar ok"
