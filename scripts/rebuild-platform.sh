#!/usr/bin/env bash
# rebuild-platform.sh — recompile just poke_platform.cpp into the archive and relink.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
unset POKECORE_LIB

SRC="$HOME/src/melonds-lua/src"
OBJ="$ROOT/Vendor/obj"
LIB="$ROOT/Vendor/libpokecore.a"
PROJ="$ROOT"

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
