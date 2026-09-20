#!/usr/bin/env bash
# build-sim.sh — build the melonDS+Lua core for the iOS-Simulator platform.
#
# ⛔ PORTABILITY. This script grew up on Linux/WSL and quietly assumed GNU userland
# and a Linux Swift install. On a macOS runner every one of those assumptions is
# false:
#   * `nproc` does not exist            -> use sysctl/hw.ncpu
#   * `xargs -a file` is GNU-only       -> feed via stdin
#   * Swift is at /usr/local/swift      -> Xcode's toolchain, found via xcrun
#   * the SDK path/version is xtool's   -> SDKROOT must be overridable
# Each of those produced a confusing downstream error (a "missing SDK" for a
# missing root; "0 poke symbols" for a missing llvm-ar), so they are handled here
# explicitly rather than assumed.
#
# Usage:
#   ./scripts/build-sim.sh
#   SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)" ./scripts/build-sim.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# SRC is the melonDS TREE root, not its src/ dir — every use appends /src.
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"
OUT="$ROOT/Vendor/sim"
OBJ="$OUT/obj"
TRIPLE="${TRIPLE:-arm64-apple-ios17.0-simulator}"
SDKROOT="${SDKROOT:-${POKE_SDK:-$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk}}"

# ---- toolchain, found rather than assumed --------------------------------
# Prefer an explicit override, then the Linux Swift install, then Xcode's clang.
if [ -n "${CXX:-}" ] && command -v "$CXX" >/dev/null 2>&1; then :
elif [ -x /usr/local/swift/bin/clang++ ]; then CXX=/usr/local/swift/bin/clang++
elif command -v xcrun >/dev/null 2>&1; then CXX="$(xcrun -f clang++)"
else CXX=clang++; fi
if [ -n "${CC:-}" ] && command -v "$CC" >/dev/null 2>&1; then :
elif [ -x /usr/local/swift/bin/clang ]; then CC=/usr/local/swift/bin/clang
elif command -v xcrun >/dev/null 2>&1; then CC="$(xcrun -f clang)"
else CC=clang; fi

# LLVM binutils. GNU ar's index is not readable by ld64.lld, and GNU nm cannot
# read Mach-O at all, so these must come from the same family as the linker.
LLVM_AR=; LLVM_RANLIB=; LLVM_NM=
for d in /usr/local/swift/bin "$(dirname "$(command -v xcrun 2>/dev/null || echo /usr/bin/xcrun)")/../bin" /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-ar" ]     && [ -z "$LLVM_AR" ]     && LLVM_AR="$d/llvm-ar"
  [ -x "$d/llvm-ranlib" ] && [ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$d/llvm-ranlib"
  [ -x "$d/llvm-nm" ]     && [ -z "$LLVM_NM" ]     && LLVM_NM="$d/llvm-nm"
done
[ -z "$LLVM_AR" ]     && LLVM_AR="$(command -v llvm-ar || command -v ar)"
[ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$(command -v llvm-ranlib || command -v ranlib)"
[ -z "$LLVM_NM" ]     && LLVM_NM="$(command -v llvm-nm || command -v nm)"

# ---- parallelism, GNU or BSD --------------------------------------------
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi

[ -d "$SRC/src" ]  || { echo "!! no melonDS source at $SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$LUA_SRC/src" ] || { echo "!! no Lua source at $LUA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$SDKROOT" ] || { echo "!! no iPhoneSimulator SDK at $SDKROOT" >&2; exit 1; }

echo "CXX     = $CXX"
echo "JOBS    = $JOBS"
echo "SDKROOT = $SDKROOT"
echo "llvm-ar = $LLVM_AR"

mkdir -p "$OBJ" "$OUT"
source "$ROOT/scripts/core-sources.sh"

COMMON="-target $TRIPLE -isysroot $SDKROOT -O2 -g -fPIC -fwrapv -fno-strict-aliasing -D__IOS__=1 -DHAVE_PTHREADS=1 -DPOKE_IOS=1 -DFE_NO_MAIN=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17 -stdlib=libc++"
CFLAGS="$COMMON $INC -std=gnu11"

compile() {
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o"
  [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0
  local flags="$CXXFLAGS" cc="$CXX"
  case "$lang" in
    cc)  flags="$CFLAGS"; cc="$CC" ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX -DLUA_USE_IOS"; cc="$CC" ;;
  esac
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
  # ⛔ THE GLUE LIST COMES FROM core-sources.sh, NOT FROM HERE. It used to be two
  # hardcoded lines, which meant the simulator core had no adapters at all and the
  # device core had all of them — a drift that only surfaced as an undefined symbol
  # at the very end of the simulator link.
  for f in $OGA_GLUE; do printf '%s|cxx|%s\n' "$ROOT/Core/$f" "$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"
echo "== compiling $(wc -l < "$OBJ/list.txt") TUs for the SIMULATOR ($TRIPLE)"
export CXX CC CXXFLAGS CFLAGS OBJ
export -f compile
# stdin, not `xargs -a` (GNU-only).
# shellcheck disable=SC2002
cat "$OBJ/list.txt" | xargs -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'

# ⛔ FAIL LOUDLY. The first CI run compiled nothing (xargs -a failed on macOS) and
# then reported "poke symbols: 0" while exiting 0 — the job only died later, at a
# step whose error message pointed at the wrong thing entirely.
if [ -f "$OBJ/.failed" ]; then
  echo "!! simulator compile errors (see $OBJ/*.err)" >&2
  exit 1
fi
count=$(ls "$OBJ"/*.o 2>/dev/null | wc -l)
if [ "$count" -lt 100 ]; then
  echo "!! only $count object files — the compile step did not really run" >&2
  exit 1
fi

echo "== archiving"
rm -f "$OUT/libpokecore-sim.a"
"$LLVM_AR" rcs "$OUT/libpokecore-sim.a" "$OBJ"/*.o || { echo "!! ar failed" >&2; exit 1; }
"$LLVM_RANLIB" "$OUT/libpokecore-sim.a" 2>/dev/null || true
ls -la "$OUT/libpokecore-sim.a"

SYMS="$("$LLVM_NM" -g "$OUT/libpokecore-sim.a" 2>/dev/null | grep -c ' T _poke_')"
echo "poke symbols: $SYMS"
if [ "${SYMS:-0}" -lt 10 ]; then
  echo "!! the archive has almost no poke_ symbols — it is not the core" >&2
  exit 1
fi
echo "== ok"
