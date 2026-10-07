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
# ⛔ SAME DIRECTORY SEARCH AS THE OTHER THREE. This was a bare `command -v`, so on
# macOS -- where Homebrew's llvm-objcopy lives at /opt/homebrew/opt/llvm/bin and is
# NOT on the default PATH -- llvm-ar/ranlib/nm were found and llvm-objcopy was not.
# The overlap then could not be localised at all.
LLVM_OBJCOPY=""
for d in /usr/local/swift/bin "$(dirname "$(command -v xcrun 2>/dev/null || echo /usr/bin/xcrun)")/../bin" /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-objcopy" ] && [ -z "$LLVM_OBJCOPY" ] && LLVM_OBJCOPY="$d/llvm-objcopy"
done
[ -z "$LLVM_OBJCOPY" ] && LLVM_OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"
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
# ⛔ THE SDK IS NOT ALWAYS XTOOL'S ARTIFACTBUNDLE. That path exists on this machine;
# on a macOS runner the SDK ships with Xcode. Resolve in this order so the local
# build is unchanged and CI can point at its own SDK:
#   1. SDK=       explicit override
#   2. SDKROOT=   what a runner sets from `xcrun --sdk iphoneos --show-sdk-path`
#   3. xcrun      a macOS box with Xcode
#   4. xtool's    this machine's default, last so local behaviour does not move
XTOOL_SDK="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ -z "${SDK:-}" ]; then
  if [ -n "${SDKROOT:-}" ]; then
    SDK="$SDKROOT"
  elif command -v xcrun >/dev/null 2>&1 && SDK="$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)" && [ -n "$SDK" ]; then
    : # xcrun answered
  else
    SDK="$XTOOL_SDK"
  fi
fi
echo "SDK       = $SDK"
CXX="${CXX:-/usr/local/swift/bin/clang++}"
[ -x "$CXX" ] || CXX="$(command -v clang++ || echo clang++)"
CC="${CC:-/usr/local/swift/bin/clang}"
[ -x "$CC" ] || CC="$(command -v clang || echo clang)"

[ -d "$SDK" ] || { echo "!! no iPhoneOS SDK at $SDK" >&2; echo "   set SDKROOT (macOS: SDKROOT=\$(xcrun --sdk iphoneos --show-sdk-path))" >&2; exit 1; }
[ -d "$SRC/src" ] || { echo "!! no melonDS source at $SRC" >&2; exit 1; }
[ -d "$LUA_SRC/src" ] || { echo "!! no Lua source at $LUA_SRC" >&2; exit 1; }
[ -d "$MGBA_SRC/src" ] || { echo "!! no mGBA source at $MGBA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
[ -d "$PPSPP_SRC/Core" ] || { echo "!! no PPSSPP source at $PPSPP_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }

source "$ROOT/scripts/build-cache.sh"
CXX_CACHE_ID="$(oga_cache_compiler_id "$CXX")" || { echo "!! cannot identify C++ compiler: $CXX" >&2; exit 1; }
CC_CACHE_ID="$(oga_cache_compiler_id "$CC")" || { echo "!! cannot identify C compiler: $CC" >&2; exit 1; }
SDK_CACHE_ID="$(oga_cache_sdk_id "$SDK")" || { echo "!! cannot identify SDK: $SDK" >&2; exit 1; }

mkdir -p "$OBJ" "$OUT"
: > "$OBJ/.expected"

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
# ---- ffmpeg (video decode for sceVideocodec/sceMpeg) ----
# Minimal static libs from scripts/build-ffmpeg.sh; their objects are extracted
# into $OBJ with an ffav_ prefix so the archive glob below picks them up.
# The ffmpeg build is stamp-gated (no-op when current).
FFMPEG_OUT="${FFMPEG_OUT:-$HOME/ffmpeg-ios}/device"
FFMPEG_SRC="$PPSPP_SRC/ffmpeg" bash "$ROOT/scripts/build-ffmpeg.sh" device || exit 1
FFMPEG_INC="$FFMPEG_OUT/include"
FFMPEG_LIB="$FFMPEG_OUT/lib"
# PPSPP_INC base paths are set after core-sources.sh is sourced below
# (ppspp_inc lives there); start empty so `set -u` stays happy.
PPSPP_INC="-DUSE_FFMPEG -I$FFMPEG_INC"
MGBA_INC="-I$MGBA_SRC/include -I$MGBA_GEN -I$MGBA_SRC/src -I$MGBA_SRC/src/third-party/lzma -I$LUA_SRC/src"

# ---- Mesen (NES/SNS/... cores) ----
# The same shape mGBA gets, and for the same reason: Mesen's console cores include from the tree
# root and need their precompiled header forced in. -include pch.h is how Mesen itself builds every
# Core TU, so a build that omits it is compiling a DIFFERENT program than the one Mesen ships.
MESEN_SRC="${MESEN_SRC:-$HOME/src/mesen}"
MESEN_INC="-I$MESEN_SRC -I$MESEN_SRC/Core -I$MESEN_SRC/Utilities"
MESEN_FORCE="-include $MESEN_SRC/Core/pch.h"
[ -d "$MESEN_SRC/Core" ] || { echo "!! no Mesen source at $MESEN_SRC" >&2; exit 1; }


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
  for f in $GLUE; do printf '%s|cxx|%s\n' "$f" "$(basename "$f" .cpp)"; done
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed" "$OBJ"/.done.*
echo "== compiling $(wc -l < "$OBJ/list.txt") translation units =="
export ROOT CXX CC CXXFLAGS CFLAGS OBJ SDK CXX_CACHE_ID CC_CACHE_ID SDK_CACHE_ID
# ⛔ Exported for xargs-spawned compile() children: they need the same compiler,
# SDK and cache helpers as the parent so each translation unit validates its own
# command fingerprint and generated header dependencies.
PPSPP_INC="$(ppspp_inc "$PPSPP_SRC") -I$PPSPP_GEN $PPSPP_INC"
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

# ---- Localise the 7-Zip symbols Mesen shares with PPSSPP's vendored copy ----
# ⛔ TWO VENDORED COPIES OF THE SAME SDK. Mesen's SevenZip/ and PPSSPP's ext/lzma-sdk/ are the same
# 7-Zip SDK, and both export the seven stream/vtable globals, so ld64.lld dies with
# "duplicate symbol: LookInStream_Read" and friends. The project's rule is to resolve this PER
# SYMBOL -- never by keeping or dropping a whole copy -- and to compute the overlap from `nm`,
# because a hand-written symbol list goes stale within one build.
#
# This must run AFTER the compile loop: a fresh compile rewrites those objects and undoes it.
if [ -z "$LLVM_OBJCOPY" ]; then
  echo "!! no llvm-objcopy on this host: the Mesen/PPSSPP 7-Zip overlap cannot be" >&2
  echo "!! localised, and the link will fail with duplicate symbol: _LookInStream_Read" >&2
  exit 1
fi
if [ -n "$LLVM_OBJCOPY" ]; then
  _other="$OBJ"
  _shared=$( { for o in "$_other"/ppsspext_ext_lzma-sdk_*.o; do
                 [ -f "$o" ] || continue
                 "$LLVM_NM" --defined-only --extern-only "$o" 2>/dev/null | awk '{print $3}'
               done; } | sort -u )
  for o in "$_other"/mesen7z_SevenZip_*.o; do
    [ -f "$o" ] || continue
    _mine=$("$LLVM_NM" --defined-only --extern-only "$o" 2>/dev/null | awk '{print $3}')
    for s in $_mine; do
      if echo "$_shared" | grep -qx "$s"; then
        "$LLVM_OBJCOPY" --localize-symbol "$s" "$o" 2>/dev/null || true
      fi
    done
  done
  echo "== localised Mesen's shared 7-Zip symbols for the link =="
fi

echo "== archiving =="
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
