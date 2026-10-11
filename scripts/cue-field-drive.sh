#!/usr/bin/env bash
# cue-field-drive.sh — boot DBZ: Shin Budokai Another Road through the REAL PSP core ABI, drive it
# into the STORY MODE field by reading the game's own menu register, and write a savestate THAT
# THIS BUILD WROTE so the cue-capture harness can load it.
#
# ⛔ IT MIRRORS scripts/psp-host-proof.sh's BUILD, because that is the only path on this host that
# links Core/psp_core.cpp cleanly. It deliberately does NOT link pokecore.cpp: that file creates the
# PSP core but never gives it a button path (poke_set_button only reaches a backend ops table this
# build populates for NDS alone), so a harness built against poke_* cannot press a single button on
# a PSP game. psp_core.h carries input, and it carries psp_debug_read for RAM.
#
# Cost note: the PPSSPP subset objects are cached in $OUT/host-obj, so a rerun is link + run.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG="${DBZ_ISO:-$HOME/dbz-another-road.iso}"
SAVEDIR="${DBZ_SAVEDIR:-$HOME/cue-run2}"
STATE_OUT="${DBZ_DRIVE_OUT:-$HOME/cue-run2/field-ours.ppst}"

[ -f "$IMG" ] || { echo "FAIL: no game image at $IMG"; exit 1; }
[ -d "$PPSPP_SRC/Core" ] || { echo "FAIL: no PPSSPP tree at $PPSPP_SRC"; exit 1; }

GEN="$OUT/host-gen"; mkdir -p "$GEN" "$OUT/host-obj"
if [ ! -f "$GEN/miniupnpcstrings.h" ]; then
  ( cd "$PPSPP_SRC/ext/miniupnp/miniupnpc" && sh updateminiupnpcstrings.sh "$GEN/miniupnpcstrings.h" miniupnpcstrings.h.in ) > /dev/null 2>&1
fi
INC="$(ppspp_inc "$PPSPP_SRC") -I$GEN"
FFMPEG_HOST="${FFMPEG_HOST:-$HOME/ffmpeg-host}"
CXX="g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -std=c++17 $INC -I$ROOT/Core -DMOBILE_DEVICE -DUSE_FFMPEG -I$FFMPEG_HOST/include"
CC="gcc -O1 -g -fPIC -fwrapv -fno-strict-aliasing $INC -DMOBILE_DEVICE -DLUA_USE_IOS -DUSE_FFMPEG -I$FFMPEG_HOST/include"

echo "== compiling the PPSSPP-core subset (cached)"
n=0; fail=0
for f in $PPSPP_CORE; do
  o="$OUT/host-obj/c_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { $CXX -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_EXT_CPP; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { $CXX -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_EXT_C; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
  [ -f "$o" ] || { $CC -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_LUA; do
  o="$OUT/host-obj/l_$(basename "$f" .c).o"
  [ -f "$o" ] || { $CC -c "$PPSPP_SRC/ext/lua/$f" -o "$o" || fail=1; n=$((n+1)); }
done
case "$(uname -m)" in
  x86_64)
    for f in $PPSPP_X86; do
      o="$OUT/host-obj/x_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
      [ -f "$o" ] || { $CC -DSTACK_LINE_READER_BUFFER_SIZE=1024 -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
    done
    for f in $PPSPP_X86_ASM; do
      o="$OUT/host-obj/x_$(basename "$f" .S).o"
      [ -f "$o" ] || { $CC -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
    done ;;
esac
# The glue is small and changes often: always rebuild it.
for f in $PPSPP_GLUE; do
  o="$OUT/host-obj/g_$(basename "$f" .cpp).o"
  rm -f "$o"; $CXX -c "$ROOT/Core/$f" -o "$o" || fail=1; n=$((n+1))
done
[ "$fail" -eq 0 ] || { echo "!! compile errors"; exit 1; }
echo "== compiled $n TUs this pass"

CXXFLAGS_MAIN="$CXX"
echo "== compiling the driver"
# ⛔ THE DRIVER'S OBJECT STAYS OUT OF host-obj. That directory is shared with the poke_*-based
# capture harness, which links host-obj/*.o wholesale -- a second main() sitting there breaks it.
$CXXFLAGS_MAIN -c "$ROOT/scripts/cue-field-drive-main.cpp" -o "$OUT/cue-drive-main.o" \
  || { echo "!! driver compile failed"; exit 1; }

echo "== linking"
# ⛔ SELECT THE OBJECTS, DO NOT GLOB THE DIRECTORY. This shared dir also holds glue from the
# poke_*-based harness (g_pokecore.o, g_adapters.o, g_announce.o), and those drag in melonDS, mGBA
# and every adapter -- so a wildcard link fails on hundreds of undefined references that have
# nothing to do with this binary. This driver needs the PPSSPP core plus psp_core.cpp and nothing
# else, and that is also why it links WITHOUT the duplicate-symbol crutch the other harness needs.
CORE_OBJS="$(ls "$OUT"/host-obj/*.o | grep -E '/(c_|e_|l_|x_)' | tr '\n' ' ')"
g++ -O1 -g "$OUT/cue-drive-main.o" "$OUT/host-obj/g_psp_core.o" $CORE_OBJS \
  -o "$OUT/cue-field-drive" -lz -lpthread -ldl \
  "$FFMPEG_HOST"/lib/libavcodec.a "$FFMPEG_HOST"/lib/libavformat.a "$FFMPEG_HOST"/lib/libavutil.a \
  "$FFMPEG_HOST"/lib/libswresample.a "$FFMPEG_HOST"/lib/libswscale.a \
  > "$OUT/cue-drive-link.log" 2>&1 \
  || { echo "!! link failed"; grep -E "error|undefined reference|multiple definition" "$OUT/cue-drive-link.log" | head -20; exit 1; }
echo "== linked $OUT/cue-field-drive"

export PPSSPP_ASSETS="${PPSSPP_ASSETS:-$ROOT/build-assets/ppsspp-assets}"
rm -f "$STATE_OUT"
echo "== driving (save dir $SAVEDIR)"
"$OUT/cue-field-drive" "$IMG" "$SAVEDIR" "$STATE_OUT"
rc=$?
echo "drive exit: $rc"
ls -la "$STATE_OUT" 2>/dev/null || echo "NO STATE WRITTEN"
