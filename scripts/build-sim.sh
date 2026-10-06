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
MGBA_SRC="${MGBA_SRC:-$HOME/src/mgba}"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
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
# ⛔ SAME DIRECTORY SEARCH AS THE OTHER THREE. This was a bare `command -v`, so on
# macOS -- where Homebrew's llvm-objcopy lives at /opt/homebrew/opt/llvm/bin and is
# NOT on the default PATH -- llvm-ar/ranlib/nm were found and llvm-objcopy was not.
# The overlap then could not be localised at all.
LLVM_OBJCOPY=""
for d in /usr/local/swift/bin "$(dirname "$(command -v xcrun 2>/dev/null || echo /usr/bin/xcrun)")/../bin" /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-objcopy" ] && [ -z "$LLVM_OBJCOPY" ] && LLVM_OBJCOPY="$d/llvm-objcopy"
done
[ -z "$LLVM_OBJCOPY" ] && LLVM_OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"

# ---- parallelism, GNU or BSD --------------------------------------------
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi

[ -d "$SRC/src" ]  || { echo "!! no melonDS source at $SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$LUA_SRC/src" ] || { echo "!! no Lua source at $LUA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$MGBA_SRC/src" ] || { echo "!! no mGBA source at $MGBA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$PPSPP_SRC/Core" ] || { echo "!! no PPSSPP source at $PPSPP_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$SDKROOT" ] || { echo "!! no iPhoneSimulator SDK at $SDKROOT" >&2; exit 1; }

source "$ROOT/scripts/build-cache.sh"
CXX_CACHE_ID="$(oga_cache_compiler_id "$CXX")" || { echo "!! cannot identify C++ compiler: $CXX" >&2; exit 1; }
CC_CACHE_ID="$(oga_cache_compiler_id "$CC")" || { echo "!! cannot identify C compiler: $CC" >&2; exit 1; }
SDK_CACHE_ID="$(oga_cache_sdk_id "$SDKROOT")" || { echo "!! cannot identify SDK: $SDKROOT" >&2; exit 1; }

echo "CXX     = $CXX"
echo "JOBS    = $JOBS"
echo "SDKROOT = $SDKROOT"
# ---- Localise the 7-Zip symbols Mesen shares with PPSSPP's vendored copy ----
# ⛔ TWO VENDORED COPIES OF THE SAME SDK. Mesen's SevenZip/ and PPSSPP's ext/lzma-sdk/ are the same
# 7-Zip SDK, and both export the seven stream/vtable globals, so ld64.lld dies with
# "duplicate symbol: LookInStream_Read" and friends. The project's rule is to resolve this PER
# SYMBOL -- never by keeping or dropping a whole copy -- and to compute the overlap from `nm`,
# because a hand-written symbol list goes stale within one build.
#
# ⛔ THIS IS A FUNCTION, NOT A TOP-LEVEL BLOCK, AND THAT IS THE BUG THAT WAS FIXED.
# It used to sit right HERE -- before the compile loop -- while its own comment said
# it must run AFTER: a fresh compile rewrites those objects and undoes it. So the
# object cache was populated with UNLOCALISED Mesen 7-Zip objects and the LINK died
# with "duplicate symbol: _LookInStream_Read". It is now CALLED from after the
# compile loop (see the call site just before "== archiving").
#
# It also fails loudly now. `llvm-objcopy --localize-symbol` is a silent no-op for a
# symbol it cannot find, so the old `|| true` could hide a whole unfixed overlap.
localise_7z_symbols() {
  [ -n "$LLVM_OBJCOPY" ] || { echo "!! no llvm-objcopy: the 7-Zip overlap cannot be resolved" >&2; return 1; }
  local _shared _mine _o _s _left
  _shared=$( { for _o in "$OBJ"/ppsspext_ext_lzma-sdk_*.o; do
                 [ -f "$_o" ] || continue
                 "$LLVM_NM" --defined-only --extern-only "$_o" 2>/dev/null | awk '{print $3}'
               done; } | sort -u )
  for _o in "$OBJ"/mesen7z_SevenZip_*.o; do
    [ -f "$_o" ] || continue
    _mine=$("$LLVM_NM" --defined-only --extern-only "$_o" 2>/dev/null | awk '{print $3}')
    for _s in $_mine; do
      if echo "$_shared" | grep -qx "$_s"; then
        "$LLVM_OBJCOPY" --localize-symbol "$_s" "$_o" 2>/dev/null || true
      fi
    done
  done
  echo "== localised Mesen's shared 7-Zip symbols for the link"
  # PROVE IT: no Mesen 7-Zip object may still export a PPSSPP 7-Zip global.
  _left=0
  for _o in "$OBJ"/mesen7z_SevenZip_*.o; do
    [ -f "$_o" ] || continue
    for _s in $("$LLVM_NM" --defined-only --extern-only "$_o" 2>/dev/null | awk '{print $3}'); do
      if echo "$_shared" | grep -qx "$_s"; then
        echo "!! still duplicated: $_s in $(basename "$_o")" >&2
        _left=$((_left + 1))
      fi
    done
  done
  [ "$_left" -eq 0 ] || { echo "!! $_left 7-Zip symbol(s) still duplicated; the link would fail" >&2; return 1; }
  return 0
}

echo "llvm-ar = $LLVM_AR"

mkdir -p "$OBJ" "$OUT"
: > "$OBJ/.expected"
source "$ROOT/scripts/core-sources.sh"

# ---- mGBA generated flags.h (same as build-core.sh; see the comment there) ----
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
# ---- ffmpeg (video decode for sceVideocodec/sceMpeg) ----
# Minimal static libs from scripts/build-ffmpeg.sh; their objects are extracted
# into $OBJ with an ffav_ prefix so the archive glob below picks them up.
# The ffmpeg build is stamp-gated (no-op when current).
FFMPEG_OUT="${FFMPEG_OUT:-$HOME/ffmpeg-ios}/sim"
FFMPEG_SRC="$PPSPP_SRC/ffmpeg" bash "$ROOT/scripts/build-ffmpeg.sh" sim || exit 1
FFMPEG_INC="$FFMPEG_OUT/include"
FFMPEG_LIB="$FFMPEG_OUT/lib"
PPSPP_INC="$PPSPP_INC -DUSE_FFMPEG -I$FFMPEG_INC"
MGBA_INC="-I$MGBA_SRC/include -I$MGBA_GEN -I$MGBA_SRC/src -I$MGBA_SRC/src/third-party/lzma -I$LUA_SRC/src"

# ---- Mesen (NES/SNS/... cores) ----
# The same shape mGBA gets, and for the same reason: Mesen's console cores include from the tree
# root and need their precompiled header forced in. -include pch.h is how Mesen itself builds every
# Core TU, so a build that omits it is compiling a DIFFERENT program than the one Mesen ships.
MESEN_SRC="${MESEN_SRC:-$HOME/src/mesen}"
MESEN_INC="-I$MESEN_SRC -I$MESEN_SRC/Core -I$MESEN_SRC/Utilities"
MESEN_FORCE="-include $MESEN_SRC/Core/pch.h"
[ -d "$MESEN_SRC/Core" ] || { echo "!! no Mesen source at $MESEN_SRC" >&2; exit 1; }


# -DFE_NO_MAIN=1 MUST stay in step with scripts/build-core.sh: fe_access.cpp owns
# a standalone-host main() behind #ifndef FE_NO_MAIN, and without this define the
# simulator archive ships a second _main that fails the app link with
# `duplicate symbol '_main'` (fe_access.o vs the app). Device had it; sim did not.
COMMON="-target $TRIPLE -isysroot $SDKROOT -O2 -g -fPIC -fwrapv -fno-strict-aliasing -D__IOS__=1 -DHAVE_PTHREADS=1 -DPOKE_IOS=1 -DFE_NO_MAIN=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17 -stdlib=libc++"
CFLAGS="$COMMON $INC -std=gnu11"

compile() {
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o" depfile="$OBJ/$tag.d" signature="$OBJ/$tag.sig" done="$OBJ/.done.$tag"
  echo "$tag.o" >> "$OBJ/.expected"
  local flags="$CXXFLAGS" cc="$CXX" compiler_id="$CXX_CACHE_ID" fingerprint
  case "$lang" in
    cxx)
      # gba_core.cpp is OGA glue but uses mGBA's configured public API.
      if [ "$(basename "$src")" = "gba_core.cpp" ]; then
        flags="$CXXFLAGS $MGBA_DEFS $MGBA_INC"
      fi
      # mesen_core.cpp is OGA glue that needs Mesen's headers and forced pch, the same way
      # gba_core.cpp needs mGBA's. Same isolation rule as the mesen case: NO $INC, because
      # melonDS's src/CRC32.h shadows Mesen's own.
      if [ "$(basename "$src")" = "mesen_core.cpp" ]; then
        flags="$COMMON -I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$LUA_SRC/src -std=c++17 -stdlib=libc++ -fwrapv -fno-strict-aliasing $MESEN_INC $MESEN_FORCE"
      fi
      # Our own Core/ code only (not melonDS): a self-calling EndFrame once
      # crashed every game on its first frame (v0.4.0) behind a mere warning.
      case "$src" in "$ROOT"/Core/*) flags="$flags -Werror=infinite-recursion" ;; esac ;;
    cc)  flags="$CFLAGS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX -DLUA_USE_IOS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    mgba) flags="$CFLAGS $MGBA_DEFS $MGBA_INC"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    # Mesen C++: the tree root on the include path and pch.h forced in, exactly as Mesen builds it.
    # -fwrapv/-fno-strict-aliasing match the mGBA/PSP sets; Mesen is not strict-aliasing clean.
    # ⛔ ISOLATED FROM $INC ON PURPOSE. $CXXFLAGS carries $INC, which puts melonDS's src/
    # FIRST -- and melonDS ships its own CRC32.h, so Mesen's `#include "CRC32.h"` resolved
    # to melonDS's different class and Bps/UpsPatcher.cpp failed with "use of undeclared
    # identifier 'CRC32'". A shadowed header, not a missing one. Build from $COMMON plus
    # our own roots and Mesen's, exactly as the working host case does -- never reorder $INC.
    mesen) flags="$COMMON -I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$LUA_SRC/src -std=c++17 -stdlib=libc++ -fwrapv -fno-strict-aliasing $MESEN_INC $MESEN_FORCE" ;;
    # Mesen C: spng and the 7-Zip SDK -- no pch force-include (those are plain C).
    mesenc) flags="$COMMON -I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -std=gnu11 $MESEN_INC"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    # The four symbols Mesen's Lua FORK adds over the app's Lua, satisfied without a second
    # interpreter (see the long note in Core/mesen_lua_extras.c). Needs Mesen's Lua include root.
    # Mesen's OWN Lua headers must win here (lua_WatchDogHook and the sandbox global live
    # in Mesen's fork). Mesen's Lua root goes FIRST and $INC is absent entirely.
    mesenlua) flags="$COMMON -I$MESEN_SRC/Lua -I$MESEN_SRC/Core -I$LUA_SRC/src -I$ROOT/Core -std=gnu11 -DLUA_USE_POSIX -DLUA_USE_IOS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppspp) flags="$CXXFLAGS $PPSPP_INC -DMOBILE_DEVICE" ;;
    ppsppmm) flags="$CXXFLAGS $PPSPP_INC -DMOBILE_DEVICE -fobjc-arc" ;;
    ppsppc) flags="$CFLAGS $PPSPP_INC -DMOBILE_DEVICE -DLUA_USE_IOS"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppsppx) flags="$CFLAGS $PPSPP_INC -DMOBILE_DEVICE -DSTACK_LINE_READER_BUFFER_SIZE=1024"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
    ppsppasm) flags="$COMMON"; cc="$CC"; compiler_id="$CC_CACHE_ID" ;;
  esac
  fingerprint="$(oga_cache_fingerprint "$cc" "$compiler_id" "$flags" "$SDK_CACHE_ID" "$TRIPLE" "$SDKROOT")" || {
    echo "FAIL $tag: cannot fingerprint compile inputs" >&2; touch "$OBJ/.failed"; return 1;
  }
  if oga_cache_is_valid "$out" "$depfile" "$signature" "$fingerprint" \
      "$src" "$ROOT/scripts/build-sim.sh" "$ROOT/scripts/core-sources.sh"; then
    touch "$done" || { echo "FAIL $tag: cannot record compile completion" >&2; touch "$OBJ/.failed"; return 1; }
    return 0
  fi
  if "$cc" $flags -MMD -MF "$depfile" -MT "$out" -c "$src" -o "$out" 2> "$OBJ/$tag.err"; then
    oga_cache_write_fingerprint "$signature" "$fingerprint" || {
      echo "FAIL $tag: cannot record compile fingerprint" >&2; touch "$OBJ/.failed"; return 1;
    }
    touch "$done" || { echo "FAIL $tag: cannot record compile completion" >&2; touch "$OBJ/.failed"; return 1; }
  else
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
  # Game Boy / GBC: M_CORE_GB is defined in MGBA_DEFS, so these TUs are
  # REACHABLE, not speculative. Derived from mGBA's own CMake (see MGBA_GB in
  # core-sources.sh); the GB set must be in ALL THREE builds or a host harness
  # would be testing a different emulator than the app ships.
  for f in $MGBA_GB;   do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgbagb_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MGBA_SM83; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgbasm83_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MGBA; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgba_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  # ---- Mesen (NES). The measured lists, NOT a hand-picked subset: Shared/Emulator.cpp constructs
  # EVERY console Mesen supports and owns a Debugger unconditionally, so a NES-only list cannot LINK
  # however cleanly it compiles. Tagged by the tree's own shape so a missing object names its file.
  for f in $MESEN_CORE_ALL; do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesen_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $MESEN_UTILS;  do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesenu_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $MESEN_C;           do printf '%s|mesenc|%s\n' "$MESEN_SRC/$f" "mesenc_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MESEN_SEVENZIP_C;  do printf '%s|mesenc|%s\n' "$MESEN_SRC/$f" "mesen7z_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MESEN_SEVENZIP_CPP; do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesen7zpp_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  # The Lua-fork symbol shim, and our NES glue -- the console the NES adapter reads from.
  printf '%s|mesenlua|mesen_lua_extras\n' "$ROOT/Core/mesen_lua_extras.c"
  # mesen_core.cpp is NOT emitted here: it is in OGA_GLUE and the cxx case below
  # gives it Mesen's flags. Emitting it twice wrote the same object from two different
  # flag sets, and the failing one won.
  for f in $PPSPP_CORE; do printf '%s|ppspp|%s\n' "$PPSPP_SRC/$f" "ppspp_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $PPSPP_EXT_CPP; do printf '%s|ppspp|%s\n' "$PPSPP_SRC/$f" "ppsspext_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  for f in $PPSPP_EXT_C; do printf '%s|ppsppc|%s\n' "$PPSPP_SRC/$f" "ppsspext_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  # PPSPP_LUA is NOT compiled here: the app lua-5.4.7 objects (lua_*)
  # already provide the identical ABI (see the PPSPP_LUA note).
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
  # ⛔ THE GLUE LIST COMES FROM core-sources.sh, NOT FROM HERE. It used to be two
  # hardcoded lines, which meant the simulator core had no adapters at all and the
  # device core had all of them — a drift that only surfaced as an undefined symbol
  # at the very end of the simulator link.
  for f in $OGA_GLUE; do printf '%s|cxx|%s\n' "$ROOT/Core/$f" "$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed" "$OBJ"/.done.*
echo "== compiling $(wc -l < "$OBJ/list.txt") TUs for the SIMULATOR ($TRIPLE)"
export ROOT CXX CC CXXFLAGS CFLAGS OBJ TRIPLE SDKROOT CXX_CACHE_ID CC_CACHE_ID SDK_CACHE_ID
# ⛔ These MUST be exported: compile() runs in xargs-spawned child shells via
# `export -f`; children need the compiler, SDK, flags, and cache helpers too.
export MGBA_DEFS MGBA_INC PPSPP_INC
# ⛔ COMMON TOO: the mesen/mesenc/mesenlua cases build their flags from $COMMON (they must
# NOT carry $INC -- melonDS's src/CRC32.h shadows Mesen's), and an unexported COMMON
# reaches the xargs children EMPTY, so there is no --target and no --isysroot at all.
# The symptom is "'memory' file not found", which reads like a missing C++ library.
export COMMON LUA_SRC ROOT
# ⛔ EXPORTED: compile() runs in xargs-spawned children, where an unexported include path
# expands to EMPTY and every Mesen TU fails with "header not found".
export MESEN_SRC MESEN_INC MESEN_FORCE
export -f compile oga_cache_fingerprint oga_cache_is_valid oga_cache_write_fingerprint
# stdin, not `xargs -a` (GNU-only).
# shellcheck disable=SC2002
cat "$OBJ/list.txt" | xargs -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'

# A cache hit is still completed work. Verify each requested TU was either
# compiled or accepted from a current cache entry, so xargs no-ops remain fatal.
_want=$(wc -l < "$OBJ/list.txt")
_done=0
for _marker in "$OBJ"/.done.*; do
  [ -f "$_marker" ] && _done=$((_done + 1))
done
if [ "$_done" -ne "$_want" ]; then
  echo "!! processed $_done of $_want translation units — compile step incomplete" >&2
  exit 1
fi

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

# ⛔ AFTER the compile loop, BEFORE archiving. See localise_7z_symbols above for why
# the position is load-bearing: a fresh compile rewrites these objects.
localise_7z_symbols || exit 1

echo "== archiving"
# Merge the ffmpeg static slices into the core archive via $OBJ.
# (One subdir per lib: generic names like utils.o exist in several libs and
# would overwrite each other in a shared dir.)
rm -rf "$OBJ/ffav" && mkdir -p "$OBJ/ffav" && cd "$OBJ/ffav" || exit 1
for _lib in avcodec avformat avutil swresample swscale; do
  mkdir -p "$_lib" && cd "$_lib" || exit 1
  "$LLVM_AR" x "$FFMPEG_LIB/lib${_lib}.a" || exit 1
  for _o in *.o; do mv "$_o" "ffav_${_lib}_${_o}"; done
  for _m in ffav_*.o; do echo "$_m" >> "$OBJ/.expected"; done
  mv ffav_*.o "$OBJ/" && cd "$OBJ/ffav" || exit 1
done
cd "$OBJ" && rm -rf ffav
# Sweep strays: a TU dropped from the source lists must not linger in $OBJ
# (its ghost .o would still match the archive glob and resurrect twins like
# the mGBA lzma set did). Anything not visited this run goes.
for _o in "$OBJ"/*.o; do
  _b="$(basename "$_o")"
  grep -qx "$_b" "$OBJ/.expected" || rm -f "$_o"
done
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
