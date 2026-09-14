#!/usr/bin/env bash
# build-host.sh — build/refresh the host x86_64 object set for the diagnostic
# harnesses from the SAME source list the iOS builds use, so a harness can never
# be testing a different core than the app ships.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
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

JOBS="${JOBS:-$(nproc)}"

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
} > "$OBJ/list.txt"

rm -f "$OBJ/.failed"
echo "== host objects: $(wc -l < "$OBJ/list.txt") TUs, $JOBS jobs"
export CXXFLAGS CFLAGS OBJ
export -f compile
xargs -a "$OBJ/list.txt" -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'
[ -f "$OBJ/.failed" ] && { echo "!! host compile errors" >&2; exit 1; }
echo "== host objects up to date ($(ls "$OBJ"/*.o | wc -l) objects)"
