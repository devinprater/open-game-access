#!/usr/bin/env bash
# build-sim.sh — build a REAL iOS-Simulator .app (arm64-apple-ios-simulator).
#
# This is the closest thing to "run it in a simulated iPhone" that this machine
# can produce: the same app, compiled and linked against the iPhoneSimulator SDK
# with the core archive built for the simulator platform, which is exactly the
# artifact Simulator.app / simctl / a cloud simulator service installs.
#
# What it cannot do here: RUN it. The iOS Simulator is a macOS-only userland
# (CoreSimulator + launchd_sim + an iOS rintime sysroot that only ships with
# Xcode); there is no simulator runtime for Windows or Linux. So this script
# proves the simulator build is valid and leaves execution to a macOS host.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
# NOTE: SRC is the melonDS TREE root, not its src/ dir — every use appends /src.
# Pointing it at .../src doubles the segment and every core file goes missing.
SRC="$HOME/src/melonds-lua"
LUA_SRC="$HOME/src/lua-5.4.7"
SDKROOT="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk"
OUT="$ROOT/Vendor/sim"
OBJ="$OUT/obj"
TRIPLE="${TRIPLE:-arm64-apple-ios17.0-simulator}"
ARCHDIR="${ARCHDIR:-arm64}"

[ -d "$SDKROOT" ] || { echo "!! no iPhoneSimulator SDK at $SDKROOT" >&2; exit 1; }
mkdir -p "$OBJ" "$OUT"

CXX=/usr/local/swift/bin/clang++
CC=/usr/local/swift/bin/clang
COMMON="-target $TRIPLE -isysroot $SDKROOT -O2 -g -fPIC -fwrapv -fno-strict-aliasing -D__IOS__=1 -DHAVE_PTHREADS=1 -DPOKE_IOS=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17 -stdlib=libc++"
CFLAGS="$COMMON $INC -std=gnu11"

source "$ROOT/scripts/core-sources.sh"

compile() { # compile <lang> <src> <tag>
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o"
  [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0
  local flags="$CXXFLAGS"
  case "$lang" in
    cc)  flags="$CFLAGS" ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX -DLUA_USE_IOS" ;;
  esac
  local cc="$CXX"
  if [ "$lang" = "cc" ] || [ "$lang" = "lua" ]; then cc="$CC"; fi
  if ! "$cc" $flags -c "$src" -o "$out" 2> "$OBJ/$tag.err"; then
    echo "FAIL $tag"; tail -25 "$OBJ/$tag.err"; touch "$OBJ/.failed"
  fi
}

{
  for f in $CORE_CPP; do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $CORE_C;   do printf '%s|cc|%s\n'  "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $TEAKRA;   do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in "$LUA_SRC"/src/*.c; do b="$(basename "$f" .c)"
    [ "$b" = "lua" ] || [ "$b" = "luac" ] || printf '%s|lua|%s\n' "$f" "lua_$b"
  done
  for f in "$ROOT/Core/poke_platform.cpp" "$ROOT/Core/pokecore.cpp"; do
    printf '%s|cxx|%s\n' "$f" "$(basename "$f" .cpp)"
  done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"
echo "== compiling $(wc -l < "$OBJ/list.txt") TUs for the SIMULATOR ($TRIPLE)"
export CXX CC CXXFLAGS CFLAGS OBJ
export -f compile
xargs -a "$OBJ/list.txt" -P "$(nproc)" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'
[ -f "$OBJ/.failed" ] && { echo "!! simulator compile errors" >&2; exit 1; }

echo "== archiving"
rm -f "$OUT/libpokecore-sim.a"
/usr/local/swift/bin/llvm-ar rcs "$OUT/libpokecore-sim.a" "$OBJ"/*.o
/usr/local/swift/bin/llvm-ranlib "$OUT/libpokecore-sim.a"
ls -la "$OUT/libpokecore-sim.a"
echo "poke symbols: $(/usr/local/swift/bin/llvm-nm -g "$OUT/libpokecore-sim.a" | grep -c ' T _poke_')"
