#!/usr/bin/env bash
# ---- parallelism, GNU or BSD ----
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi
# build-host.sh — build/refresh the host x86_64 object set for the diagnostic
# harnesses from the SAME source list the iOS builds use, so a harness can never
# be testing a different core than the app ships.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"
MGBA_SRC="${MGBA_SRC:-$HOME/src/mgba}"
OBJ="$ROOT/Vendor/hostobj"
SDKINC=""

mkdir -p "$OBJ"
source "$ROOT/scripts/core-sources.sh"

# mGBA needs its generated flags.h (mirrors build-core.sh; same audited DEFS).
MGBA_GEN="$OBJ/mgba-gen"
mkdir -p "$MGBA_GEN/mgba"
if [ ! -f "$MGBA_GEN/mgba/flags.h" ] || [ "$MGBA_SRC/src/core/flags.h.in" -nt "$MGBA_GEN/mgba/flags.h" ]; then
  sed -e 's/#cmakedefine01 \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      -e 's/#cmakedefine \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      "$MGBA_SRC/src/core/flags.h.in" > "$MGBA_GEN/mgba/flags.h"
fi
MGBA_INC="-I$MGBA_SRC/include -I$MGBA_GEN -I$MGBA_SRC/src -I$LUA_SRC/src"

# ---- Mesen (NES) ----
# The same shape mGBA gets, and for the same reason: Mesen's console cores include from the tree
# root and need their precompiled header forced in. -include pch.h is how Mesen itself builds every
# Core TU (see scripts/mesen-feasibility.sh, whose compile line is the measured one), so a host
# build that omits it is compiling a DIFFERENT program than the admission test measured.
MESEN_SRC="${MESEN_SRC:-$HOME/src/mesen}"
# -I$MESEN_SRC lets "Shared/..." and "Utilities/..." resolve; -I$MESEN_SRC/Core is where pch.h and
# the "NES/..." includes live. The force-include path is the feasibility script's own line.
MESEN_INC="-I$MESEN_SRC -I$MESEN_SRC/Core -I$MESEN_SRC/Utilities"
MESEN_DEFS=""
MESEN_FORCE="-include $MESEN_SRC/Core/pch.h"
# Host glibc has no <xlocale.h> (merged into <locale.h>); the audited DEFS
# target the Mac toolchain where it exists. Everything else carries over.
# -DHAVE_XLOCALE: glibc merged xlocale.h into locale.h (no such header).
# -DHAVE_PTHREAD_SET_NAME_NP: macOS pthread_set_name_np; glibc wants the
# plain HAVE_PTHREAD_SETNAME_NP branch (pthread_setname_np(thread, name)).
HOST_MGBA_DEFS="$(echo "$MGBA_DEFS" | sed -e 's/-DHAVE_XLOCALE//' -e 's/-DHAVE_PTHREAD_SET_NAME_NP//')"

# POKE_HOST, not POKE_IOS: the platform layer picks clock_gettime/usleep instead
# of mach_absolute_time. Same core, same Lua, same script.
COMMON="-O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -DFE_NO_MAIN=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17"
CFLAGS="$COMMON $INC -std=gnu11"


compile() {
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o" flagfile="$OBJ/$tag.flags"
  local flags="$CXXFLAGS" cc=g++
  case "$lang" in
    cxx)
      # gba_core.cpp is OGA glue but uses mGBA's configured public API.
      if [ "$(basename "$src")" = "gba_core.cpp" ]; then
        flags="$CXXFLAGS ${HOST_MGBA_DEFS:-$MGBA_DEFS} $MGBA_INC"
      fi
      # Our own Core/ code only (not melonDS): a self-calling EndFrame once
      # crashed every game on its first frame (v0.4.0) behind a mere warning.
      case "$src" in "$ROOT"/Core/*) flags="$flags -Werror=infinite-recursion" ;; esac ;;
    cc)   flags="$CFLAGS"; cc=gcc ;;
    lua)  flags="$CFLAGS -DLUA_USE_POSIX"; cc=gcc ;;
    mgba) flags="$CFLAGS $HOST_MGBA_DEFS $MGBA_INC"; cc=gcc ;;
    mesenc) flags="-O2 -g -fPIC -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -w -I$MESEN_SRC -I$MESEN_SRC/Core -I$MESEN_SRC/Utilities"; cc=gcc ;;
    # Mesen's Lua-header shim: MUST see Mesen's Lua/ FIRST (its lua.h/lauxlib.h differ from the
    # app's stock lua-5.4.7 -- same collision shape as CRC32.h). Nothing else is on this path.
    mesenlua) flags="-O2 -g -fPIC -DPOKE_HOST=1 -w -I$MESEN_SRC/Lua"; cc=gcc ;;

    mesenc) flags="-O2 -g -fPIC -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -w -I$MESEN_SRC -I$MESEN_SRC/Core -I$MESEN_SRC/Utilities"; cc=gcc ;;

    # ⛔ SELF-CONTAINED, NOT $CXXFLAGS. Mesen and melonDS BOTH ship a `CRC32.h` declaring different
    # classes; the shared flag set puts melonDS's src ahead of Mesen's Utilities, so Mesen's own
    # `#include "CRC32.h"` silently resolved to melonDS's and the error read "CRC32 has not been
    # declared" -- a shadowed header, not a missing one. Mesen TUs need none of the melonDS/Lua
    # paths, so the fix is to leave them out here rather than reorder the shared list (which would
    # aim Mesen's CRC32.h at melonDS's TUs and break those instead).
    mesen) flags="-O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything -I$ROOT/Core -I$ROOT/Sources/CPokeCore/include $MESEN_INC $MESEN_FORCE -std=c++17 -w"; cc=g++ ;;
  esac
  # Timestamp alone is not enough: flag changes (DEFS, includes) must rebuild.
  # The flags file records the exact command that produced $out.
  if [ -f "$out" ] && [ "$out" -nt "$src" ] && [ -f "$flagfile" ] && \
     [ "$(cat "$flagfile")" = "$cc $flags" ]; then return 0; fi
  if ! $cc $flags -c "$src" -o "$out" 2> "$OBJ/$tag.err"; then
    echo "FAIL $tag"; head -20 "$OBJ/$tag.err"; touch "$OBJ/.failed"
  else
    echo "$cc $flags" > "$flagfile"
  fi
}

{
  for f in $CORE_CPP; do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $CORE_C;   do printf '%s|cc|%s\n'  "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $TEAKRA;   do printf '%s|cxx|%s\n' "$SRC/src/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in "$LUA_SRC"/src/*.c; do b="$(basename "$f" .c)"
    [ "$b" = "lua" ] || [ "$b" = "luac" ] || printf '%s|lua|%s\n' "$f" "lua_$b"
  done
  printf '%s|cxx|poke_platform\n' "$ROOT/Core/poke_platform.cpp"
  printf '%s|cxx|pokecore\n'      "$ROOT/Core/pokecore.cpp"
  printf '%s|cxx|oga_core\n'      "$ROOT/Core/oga_core.cpp"
  # The system registry: which consoles this app knows and what each is like.
  # In no build list until now, which is why "both UIs read the registry" could
  # not have worked — nothing compiled it into the app.
  printf '%s|cxx|systems\n'       "$ROOT/Core/systems.cpp"
  # ⛔ THE ADAPTER SOURCES BELONG HERE TOO. They were omitted, so the host objects
  # had a pokecore.o that referenced oga::find_by_game_code while nothing defined
  # it — every harness then failed at the LINK with an undefined symbol, which
  # reads like a source problem rather than "one file was never in the list".
  # The list is mirrored from build-core.sh deliberately; if you add a Core source
  # there, add it here in the same commit.
  printf '%s|cxx|fe_access\n'    "$ROOT/Core/fe_access.cpp"
  printf '%s|cxx|fe_adapter\n'   "$ROOT/Core/fe_adapter.cpp"
  printf '%s|cxx|gba_adapter\n'  "$ROOT/Core/gba_adapter.cpp"
  # ⛔ ONE SOURCE PER ENTRY. These two were once a single printf with both paths in one
  # quoted string, so the compiler was handed "Core/nes_adapter.cpp Core/n64_adapter.cpp" as
  # a single filename and failed. A stale nes_adapter.o from an earlier build made the build
  # look clean, while n64_adapter.o simply did not exist -- and every harness then died at
  # the LINK with "undefined reference to oga::kNintendo64", which reads like a source bug.
  printf '%s|cxx|nes_adapter\n'  "$ROOT/Core/nes_adapter.cpp"
  printf '%s|cxx|n64_adapter\n'  "$ROOT/Core/n64_adapter.cpp"
  printf '%s|cxx|gba_core\n'     "$ROOT/Core/gba_core.cpp"
  printf '%s|cxx|mgba_version\n' "$ROOT/Core/mgba_version_stub.cpp"
  # Game Boy / GBC: M_CORE_GB is defined in MGBA_DEFS, so these TUs are
  # REACHABLE, not speculative. Derived from mGBA's own CMake (see MGBA_GB in
  # core-sources.sh); the GB set must be in ALL THREE builds or a host harness
  # would be testing a different emulator than the app ships.
  for f in $MGBA_GB;   do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgbagb_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MGBA_SM83; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgbasm83_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MGBA; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgba_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  # The real 7z SDK, host builds only (see the MGBA_LZMA note in core-sources.sh).
  # Without it these symbols fall to abort-on-call stubs, and any harness that
  # boots a GBA ROM dies the moment mCoreFind opens the ROM as an archive.
  for f in $MGBA_LZMA; do printf '%s|mgba|%s\n' "$MGBA_SRC/$f" "mgbalzma_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  # ---- Mesen (NES). The measured lists, NOT a hand-picked subset: these are the same entries
  # scripts/mesen-feasibility.sh compiled for aarch64-linux-android26. A host harness that compiled
  # a different subset would be proving something about a program the app does not ship.
  # ⛔ THE WHOLE CORE, NOT A SUBSET -- see the MESEN_CORE_ALL note in core-sources.sh. A NES-only
  # list compiles and cannot link, because Shared/Emulator.cpp's console factory and its Debugger
  # are unconditional. The tag keeps the tree's shape, so a missing object names its own file.
  for f in $MESEN_CORE_ALL; do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesen_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  # Utilities/ is a Core/ SIBLING and a separate list (its own project file), so it stays separate.
  for f in $MESEN_UTILS;  do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesenu_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  # spng + the 7-Zip SDK: C, so the mesenc lang, and no pch force-include.
  for f in $MESEN_C;           do printf '%s|mesenc|%s\n' "$MESEN_SRC/$f" "mesenc_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MESEN_SEVENZIP_C;  do printf '%s|mesenc|%s\n' "$MESEN_SRC/$f" "mesen7z_$(echo "$f" | tr '/' '_' | sed 's/\.c$//')"; done
  for f in $MESEN_SEVENZIP_CPP; do printf '%s|mesen|%s\n' "$MESEN_SRC/$f" "mesen7zpp_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//')"; done
  # spng + the 7-Zip SDK: C, so the mesenc lang, and no pch force-include.
  # The four symbols Mesen's Lua FORK adds over the app's Lua, satisfied without a second
  # interpreter. See the long note in Core/mesen_lua_extras.c for why this is a shim and not a build
  # of Mesen's 52-file Lua tree.
  printf '%s|mesenlua|mesen_lua_extras\n' "$ROOT/Core/mesen_lua_extras.c"
  # Our glue: the console the NES adapter reads from. This is the file that did not exist.
  printf '%s|mesen|mesen_core\n' "$ROOT/Core/mesen_core.cpp"
  printf '%s|cxx|dbz_adapter\n'  "$ROOT/Core/dbz_adapter.cpp"
  printf '%s|cxx|dissidia_adapter\n'  "$ROOT/Core/dissidia_adapter.cpp"
  printf '%s|cxx|dq9_adapter\n'  "$ROOT/Core/dq9_adapter.cpp"
  printf '%s|cxx|osk_echo\n'  "$ROOT/Core/osk_echo.cpp"
  printf '%s|cxx|adapters\n'     "$ROOT/Core/adapters.cpp"
  printf '%s|cxx|announce\n'     "$ROOT/Core/announce.cpp"
  # ⛔ HOST-ONLY LINK SATISFACTION, NEVER IN core-sources.sh. pokecore.o calls psp_*
  # unconditionally; the real Core/psp_core.cpp needs the PPSSPP tree (the `ppspp` lang), which
  # this host build does not carry. host_harness_stub.cpp is the file written for exactly this,
  # and it was in no list -- so the host set had undefined psp_* and no harness could link.
  # Its own header forbids adding it to core-sources.sh: the app satisfies these with the real
  # implementations and a second definition would break the app link.
  printf '%s|cxx|host_harness_stub\n' "$ROOT/Core/host_harness_stub.cpp"
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"

# ⛔ THE 7-ZIP DEDUPE RUNS HERE, AFTER THE COMPILE LOOP, EVERY TIME. Mesen's 7zStream.o and mGBA's
# copy define five of the same globals; localizing Mesen's copies is a per-object step that a fresh
# compile undoes, so it belongs in the same place the objects are produced -- not in a one-off fixup
# someone has to remember.
if [ -f "$OBJ/mesen7z_SevenZip_7zStream.o" ]; then
  bash "$ROOT/scripts/mesen-dedupe-7z.sh" "$OBJ/mesen7z_SevenZip_7zStream.o" || {
    echo "!! 7-Zip symbol dedupe failed" >&2; touch "$OBJ/.failed"; }
fi
echo "== host objects: $(wc -l < "$OBJ/list.txt") TUs, $JOBS jobs"
export CXXFLAGS CFLAGS OBJ MGBA_SRC
export MGBA_DEFS MGBA_INC MGBA_GEN HOST_MGBA_DEFS
export MESEN_SRC MESEN_INC MESEN_FORCE
export -f compile
# ⛔ THE `< "$OBJ/list.txt"` IS REQUIRED — see the same line in build-core.sh.
# Without it xargs reads STDIN (empty under a non-interactive shell), compiles
# ZERO files, and reports success. That reads as "the objects are up to date"
# when in fact nothing was ever built, and the harness then fails to link with
# undefined symbols that look like a source problem.
xargs -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
' < "$OBJ/list.txt"

[ -f "$OBJ/.failed" ] && { echo "!! host compile errors" >&2; exit 1; }

# Gate on work actually done, not on the absence of errors — the no-op above
# exited 0 and printed "up to date" while zero objects existed.
_have=$(ls "$OBJ"/*.o 2>/dev/null | wc -l)
if [ "$_have" -eq 0 ]; then
  echo "!! host build produced NO objects ($(wc -l < "$OBJ/list.txt") TUs requested)" >&2
  exit 1
fi
echo "== host objects: $_have objects"
