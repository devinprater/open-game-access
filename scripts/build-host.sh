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
OBJ="$ROOT/Vendor/hostobj"
SDKINC=""

mkdir -p "$OBJ"
source "$ROOT/scripts/core-sources.sh"

# POKE_HOST, not POKE_IOS: the platform layer picks clock_gettime/usleep instead
# of mach_absolute_time. Same core, same Lua, same script.
COMMON="-O2 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC/src -I$LUA_SRC/src -I$SRC/src/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17"
CFLAGS="$COMMON $INC -std=gnu11"


compile() {
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o"
  [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0
  local flags="$CXXFLAGS" cc=g++
  case "$lang" in
    cc)  flags="$CFLAGS"; cc=gcc ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX"; cc=gcc ;;
  esac
  if ! $cc $flags -c "$src" -o "$out" 2> "$OBJ/$tag.err"; then
    echo "FAIL $tag"; head -20 "$OBJ/$tag.err"; touch "$OBJ/.failed"
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
  # ⛔ THE ADAPTER SOURCES BELONG HERE TOO. They were omitted, so the host objects
  # had a pokecore.o that referenced oga::find_by_game_code while nothing defined
  # it — every harness then failed at the LINK with an undefined symbol, which
  # reads like a source problem rather than "one file was never in the list".
  # The list is mirrored from build-core.sh deliberately; if you add a Core source
  # there, add it here in the same commit.
  printf '%s|cxx|fe_access\n'    "$ROOT/Core/fe_access.cpp"
  printf '%s|cxx|fe_adapter\n'   "$ROOT/Core/fe_adapter.cpp"
  printf '%s|cxx|gba_adapter\n'  "$ROOT/Core/gba_adapter.cpp"
  printf '%s|cxx|dbz_adapter\n'  "$ROOT/Core/dbz_adapter.cpp"
  printf '%s|cxx|dissidia_adapter\n'  "$ROOT/Core/dissidia_adapter.cpp"
  printf '%s|cxx|adapters\n'     "$ROOT/Core/adapters.cpp"
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"
echo "== host objects: $(wc -l < "$OBJ/list.txt") TUs, $JOBS jobs"
export CXXFLAGS CFLAGS OBJ
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
