#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"
OUT="$ROOT/Vendor"
OBJ="$OUT/obj"
SDK="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CXX="/usr/local/swift/bin/clang++"
CC="/usr/local/swift/bin/clang"

[ -d "$SDK" ] || { echo "!! no iPhoneOS SDK at $SDK" >&2; exit 1; }
[ -d "$SRC/src" ] || { echo "!! no melonDS source at $SRC" >&2; exit 1; }
[ -d "$LUA_SRC/src" ] || { echo "!! no Lua source at $LUA_SRC" >&2; exit 1; }

mkdir -p "$OBJ" "$OUT"

# -fwrapv matters: melonDS's ARM interpreter relies on wrapping arithmetic.
# No JIT_ENABLED: see scripts/README.md — the ARM64 JIT needs MAP_JIT and
# pthread_jit_write_protect_np, which iOS only exposes to apps signed with the
# dynamic-codesigning entitlement. A free-account sideload has no such thing,
# so this build runs the interpreter (correct everywhere, no entitlements).
COMMON="-target arm64-apple-ios17.0 -isysroot $SDK -O2 -g -fPIC -fwrapv -fno-strict-aliasing -D__IOS__=1 -DHAVE_PTHREADS=1 -DPOKE_IOS=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17 -stdlib=libc++"
CFLAGS="$COMMON $INC -std=gnu11"

CORE_CPP=""
CORE_C=""
TEAKRA=""
# The source list lives in one place, shared with scripts/build-sim.sh: two
# copies is exactly how the simulator build ends up missing a translation unit
# and fails at the FINAL link with a symbol the device build has.
source "$ROOT/scripts/core-sources.sh"
GLUE="$ROOT/Core/poke_platform.cpp $ROOT/Core/pokecore.cpp"

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
    echo "FAIL $tag"; tail -30 "$OBJ/$tag.err"; touch "$OBJ/.failed"
  fi
}

{
  for f in $CORE_CPP; do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $CORE_C;   do printf '%s|cc|%s\n'  "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $TEAKRA;   do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in "$LUA_SRC"/src/*.c; do b="$(basename "$f" .c)"
    [ "$b" = "lua" ] || [ "$b" = "luac" ] || printf '%s|lua|%s\n' "$f" "lua_$b"
  done
  for f in $GLUE; do printf '%s|cxx|%s\n' "$f" "$(basename "$f" .cpp)"; done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"
echo "== compiling $(wc -l < "$OBJ/list.txt") translation units =="
export CXX CC CXXFLAGS CFLAGS OBJ
export -f compile
xargs -a "$OBJ/list.txt" -P "$(nproc)" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'

if [ -f "$OBJ/.failed" ]; then echo "!! compile errors above" >&2; exit 1; fi

echo "== archiving =="
rm -f "$OUT/libpokecore.a"
# GNU ar's index is not readable by ld64.lld ("archive has no index"), so the
# archive is built and indexed with the Swift toolchain's LLVM binutils — the
# same family as the linker that consumes it.
AR=/usr/local/swift/bin/llvm-ar
RANLIB=/usr/local/swift/bin/llvm-ranlib
[ -x "$AR" ] || AR=ar
[ -x "$RANLIB" ] || RANLIB=ranlib

"$AR" rcs "$OUT/libpokecore.a" "$OBJ"/*.o
"$RANLIB" "$OUT/libpokecore.a"
ls -la "$OUT/libpokecore.a"
echo "-- poke symbols --"; /usr/local/swift/bin/llvm-nm -g "$OUT/libpokecore.a" 2>/dev/null | grep -c " T _poke_" || true

# xtool's generated builder package links a stub against the SwiftPM product, so
# the app bundle it writes is the stub unless the .app comes from `xtool dev`;
# the core archive must therefore be force-loaded (Package.swift) AND the app
# rebuilt by xtool whenever the archive changes.
