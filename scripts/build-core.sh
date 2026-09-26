#!/usr/bin/env bash
set -uo pipefail
# ---- LLVM binutils, located rather than assumed ----
# GNU ar's index is not readable by ld64.lld, and GNU nm cannot read Mach-O, so
# these must come from the same family as the linker.
LLVM_AR=""; LLVM_RANLIB=""; LLVM_NM=""
for d in /usr/local/swift/bin /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-ar" ]     && [ -z "$LLVM_AR" ]     && LLVM_AR="$d/llvm-ar"
  [ -x "$d/llvm-ranlib" ] && [ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$d/llvm-ranlib"
  [ -x "$d/llvm-nm" ]     && [ -z "$LLVM_NM" ]     && LLVM_NM="$d/llvm-nm"
done
[ -z "$LLVM_AR" ]     && LLVM_AR="$(command -v llvm-ar || command -v ar)"
[ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$(command -v llvm-ranlib || command -v ranlib)"
[ -z "$LLVM_NM" ]     && LLVM_NM="$(command -v llvm-nm || command -v nm)"
# ---- parallelism, GNU or BSD ----
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"
MGBA_SRC="${MGBA_SRC:-$HOME/src/mgba}"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OUT="$ROOT/Vendor"
OBJ="$OUT/obj"
SDK="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CXX="${CXX:-/usr/local/swift/bin/clang++}"
[ -x "$CXX" ] || CXX="$(command -v clang++ || echo clang++)"
CC="${CC:-/usr/local/swift/bin/clang}"
[ -x "$CC" ] || CC="$(command -v clang || echo clang)"

[ -d "$SDK" ] || { echo "!! no iPhoneOS SDK at $SDK" >&2; exit 1; }
[ -d "$SRC/src" ] || { echo "!! no melonDS source at $SRC" >&2; exit 1; }
[ -d "$LUA_SRC/src" ] || { echo "!! no Lua source at $LUA_SRC" >&2; exit 1; }
[ -d "$MGBA_SRC/src" ] || { echo "!! no mGBA source at $MGBA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$PPSPP_SRC/Core" ] || { echo "!! no PPSSPP source at $PPSPP_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }

source "$ROOT/scripts/build-cache.sh"
CXX_CACHE_ID="$(oga_cache_compiler_id "$CXX")" || { echo "!! cannot identify C++ compiler: $CXX" >&2; exit 1; }
CC_CACHE_ID="$(oga_cache_compiler_id "$CC")" || { echo "!! cannot identify C compiler: $CC" >&2; exit 1; }
SDK_CACHE_ID="$(oga_cache_sdk_id "$SDK")" || { echo "!! cannot identify SDK: $SDK" >&2; exit 1; }

mkdir -p "$OBJ" "$OUT"

# ---- mGBA generated flags.h ----
#
# CMake normally generates this from src/core/flags.h.in. The iOS build does
# not run CMake (it cannot run Apple's toolchain checks from here), so the
# template's #cmakedefine lines are neutralised and every enabled feature
# comes from the audited -D flags in MGBA_DEFS (scripts/core-sources.sh).
# The two MUST agree: a symbol #defined here AND -D on the command line is
# harmless, but a symbol the code expects from flags.h that is in neither
# place silently takes the #else path.
MGBA_GEN="$OBJ/mgba-gen"
mkdir -p "$MGBA_GEN/mgba"
if [ ! -f "$MGBA_GEN/mgba/flags.h" ] || [ "$MGBA_SRC/src/core/flags.h.in" -nt "$MGBA_GEN/mgba/flags.h" ]; then
  sed -e 's/#cmakedefine01 \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      -e 's/#cmakedefine \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      "$MGBA_SRC/src/core/flags.h.in" > "$MGBA_GEN/mgba/flags.h"
fi
# ---- miniupnpc generated miniupnpcstrings.h ----
# Core/Util/PortManager.h pulls in miniwget.h, which needs miniupnpcstrings.h;
# miniupnp generates it from its VERSION via their own script (same command
# their Makefile runs). Generated per build dir, like mGBA's flags.h above.
PPSPP_GEN="$OBJ/ppspp-gen"
mkdir -p "$PPSPP_GEN"
if [ ! -f "$PPSPP_GEN/miniupnpcstrings.h" ] || [ "$PPSPP_SRC/ext/miniupnp/miniupnpc/VERSION" -nt "$PPSPP_GEN/miniupnpcstrings.h" ]; then
  ( cd "$PPSPP_SRC/ext/miniupnp/miniupnpc" && sh updateminiupnpcstrings.sh "$PPSPP_GEN/miniupnpcstrings.h" miniupnpcstrings.h.in ) > /dev/null
fi
PPSPP_INC="$(ppspp_inc "$PPSPP_SRC") -I$PPSPP_GEN"
MGBA_INC="-I$MGBA_SRC/include -I$MGBA_GEN -I$MGBA_SRC/src -I$MGBA_SRC/src/third-party/lzma -I$LUA_SRC/src"

# -fwrapv matters: melonDS's ARM interpreter relies on wrapping arithmetic.
# No JIT_ENABLED: see scripts/README.md — the ARM64 JIT needs MAP_JIT and
# pthread_jit_write_protect_np, which iOS only exposes to apps signed with the
# dynamic-codesigning entitlement. A free-account sideload has no such thing,
# so this build runs the interpreter (correct everywhere, no entitlements).
COMMON="-target arm64-apple-ios17.0 -isysroot $SDK -O2 -g -fPIC -fwrapv -fno-strict-aliasing -D__IOS__=1 -DHAVE_PTHREADS=1 -DPOKE_IOS=1 -DFE_NO_MAIN=1 -Wno-everything"
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
# ⛔ BUILT FROM THE SHARED LIST, NOT A PRIVATE COPY. This line used to be a second,
# hand-maintained list of the same files — and it is why adding a new adapter meant
# remembering two places, and why the simulator build silently had none.
GLUE=""
for _f in $OGA_GLUE; do GLUE="$GLUE $ROOT/Core/$_f"; done

compile() { # compile <lang> <src> <tag>
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o" depfile="$OBJ/$tag.d" signature="$OBJ/$tag.sig" done="$OBJ/.done.$tag"
  local flags="$CXXFLAGS" cc="$CXX" compiler_id="$CXX_CACHE_ID" fingerprint
  case "$lang" in
    cxx)
      # gba_core.cpp is OGA glue but uses mGBA's configured public API.
      if [ "$(basename "$src")" = "gba_core.cpp" ]; then
        flags="$CXXFLAGS $MGBA_DEFS $MGBA_INC"
      fi ;;
    cc)  flags="$CFLAGS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX -DLUA_USE_IOS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    mgba) flags="$CFLAGS $MGBA_DEFS $MGBA_INC"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppspp) flags="$CXXFLAGS $PPSPP_INC -DMOBILE_DEVICE" ;;
    ppsppmm) flags="$CXXFLAGS $PPSPP_INC -DMOBILE_DEVICE -fobjc-arc" ;;
    ppsppc) flags="$CFLAGS $PPSPP_INC -DMOBILE_DEVICE -DLUA_USE_IOS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppsppx) flags="$CFLAGS $PPSPP_INC -DMOBILE_DEVICE -DSTACK_LINE_READER_BUFFER_SIZE=1024"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppsppasm) flags="$COMMON"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
  esac
  fingerprint="$(oga_cache_fingerprint "$cc" "$compiler_id" "$flags" "$SDK_CACHE_ID" "$SDK")" || {
    echo "FAIL $tag: cannot fingerprint compile inputs" >&2; touch "$OBJ/.failed"; return 1;
  }
  if oga_cache_is_valid "$out" "$depfile" "$signature" "$fingerprint" \
      "$src" "$ROOT/scripts/build-core.sh" "$ROOT/scripts/core-sources.sh"; then
    touch "$done" || { echo "FAIL $tag: cannot record compile completion" >&2; touch "$OBJ/.failed"; return 1; }
    return 0
  fi
  if "$cc" $flags -MMD -MF "$depfile" -MT "$out" -c "$src" -o "$out" 2> "$OBJ/$tag.err"; then
    oga_cache_write_fingerprint "$signature" "$fingerprint" || {
      echo "FAIL $tag: cannot record compile fingerprint" >&2; touch "$OBJ/.failed"; return 1;
    }
    touch "$done" || { echo "FAIL $tag: cannot record compile completion" >&2; touch "$OBJ/.failed"; return 1; }
  else
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
  for f in $MGBA; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgba_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $PPSPP_CORE; do printf '%s|ppspp|%s\n' "$PPSPP_SRC/$f" "ppspp_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $PPSPP_EXT_CPP; do printf '%s|ppspp|%s\n' "$PPSPP_SRC/$f" "ppsspext_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $PPSPP_EXT_C; do printf '%s|ppsppc|%s\n' "$PPSPP_SRC/$f" "ppsspext_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $PPSPP_LUA; do printf '%s|ppsppc|%s\n' "$PPSPP_SRC/ext/lua/$f" "ppssplua_$(basename "$f" .c)"; done
  for f in $PPSPP_MM; do printf '%s|ppsppmm|%s\n' "$PPSPP_SRC/$f" "ppsppmm_$(basename "$f" .mm)"; done
  for f in $PPSPP_GLUE; do printf '%s|ppspp|%s\n' "$ROOT/Core/$f" "ppsppglue_$(basename "$f" .cpp)"; done
  # ARM-only helpers (libpng NEON; x86_64 builds skip these the way ARM
  # builds skip the x86 helpers above).
  case "${TRIPLE:-arm64-apple-ios}" in
    x86_64*) ;;
    *) for f in $PPSPP_ARM; do printf '%s|ppsppc|%s\n' "$PPSPP_SRC/$f" "ppssparmm_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done ;;
  esac
  # x86_64-only helpers (see the PPSPP_X86 comment in core-sources.sh). The
  # device build is always arm64; the simulator follows $TRIPLE when set.
  case "${TRIPLE:-arm64-apple-ios}" in
    x86_64*)
      for f in $PPSPP_X86; do printf '%s|ppsppx|%s\n' "$PPSPP_SRC/$f" "ppsspx86_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
      for f in $PPSPP_X86_ASM; do printf '%s|ppsppasm|%s\n' "$PPSPP_SRC/$f" "ppsspx86_$(basename "$f" .S)"; done ;;
  esac
  for f in $GLUE; do printf '%s|cxx|%s\n' "$f" "$(basename "$f" .cpp)"; done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed" "$OBJ"/.done.*
echo "== compiling $(wc -l < "$OBJ/list.txt") translation units =="
export ROOT CXX CC CXXFLAGS CFLAGS OBJ SDK CXX_CACHE_ID CC_CACHE_ID SDK_CACHE_ID
# ⛔ Exported for xargs-spawned compile() children: they need the same compiler,
# SDK and cache helpers as the parent so each translation unit validates its own
# command fingerprint and generated header dependencies.
export MGBA_DEFS MGBA_INC PPSPP_INC
export -f compile oga_cache_fingerprint oga_cache_is_valid oga_cache_write_fingerprint
# ⛔ THE `< "$OBJ/list.txt"` IS REQUIRED. Without it xargs reads STDIN, which is
# empty under a non-interactive shell, so it compiles ZERO files, the archive is
# relinked from whatever objects happen to be lying around, and the build prints
# "== archiving ==" and exits 0. That is a silent no-op reported as success — the
# archive kept its old contents and a newly added source file was simply absent.
# It cost a full round-trip here: the FE adapter compiled "cleanly" and was not
# in the archive at all. Any change to this pipeline must keep the input explicit.
xargs -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
' < "$OBJ/list.txt"

# Fail loudly if xargs did not process every requested translation unit. A valid
# cache hit counts as completed work just like a fresh compile; object mtimes alone
# cannot distinguish that from xargs silently doing nothing.
_want=$(wc -l < "$OBJ/list.txt")
_have=0
for _marker in "$OBJ"/.done.*; do
  [ -f "$_marker" ] && _have=$((_have + 1))
done
if [ "$_have" -ne "$_want" ]; then
  echo "!! processed $_have of $_want translation units — compile step incomplete" >&2
  exit 1
fi

if [ -f "$OBJ/.failed" ]; then echo "!! compile errors above" >&2; exit 1; fi

echo "== archiving =="
rm -f "$OUT/libpokecore.a"
# GNU ar's index is not readable by ld64.lld ("archive has no index"), so the
# archive is built and indexed with the Swift toolchain's LLVM binutils — the
# same family as the linker that consumes it.
AR="$LLVM_AR"
RANLIB="$LLVM_RANLIB"
[ -x "$AR" ] || AR=ar
[ -x "$RANLIB" ] || RANLIB=ranlib

"$AR" rcs "$OUT/libpokecore.a" "$OBJ"/*.o
"$RANLIB" "$OUT/libpokecore.a"
ls -la "$OUT/libpokecore.a"
echo "-- poke symbols --"; "$LLVM_NM" -g "$OUT/libpokecore.a" 2>/dev/null | grep -c " T _poke_" || true

# xtool's generated builder package links a stub against the SwiftPM product, so
# the app bundle it writes is the stub unless the .app comes from `xtool dev`;
# the core archive must therefore be force-loaded (Package.swift) AND the app
# rebuilt by xtool whenever the archive changes.
