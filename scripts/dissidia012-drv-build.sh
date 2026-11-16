#!/usr/bin/env bash
# Build the Dissidia 012 driver with exec-breakpoint support (bp/runbp/bppoll).
# Same shape as psp-memtrace-build.sh: fresh glue (psp_core.cpp with the
# trace-only break APIs) plus scripts/dissidia012-drv.cpp, linked against the
# existing PPSSPP object set; every other object is reused untouched.
set -u
ROOT="$HOME/oga-work"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OBJ="$HOME/oga-ppsspp-enemy/obj"
GEN="$HOME/oga-ppsspp-enemy/host-gen"
FFMPEG_HOST="${FFMPEG_HOST:-$HOME/ffmpeg-host}"
OUT="$HOME/drv-bp"
TMP="$HOME/oga-ppsspp-enemy/drvbp-build"
mkdir -p "$TMP"

# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
INC="$(ppspp_inc "$PPSPP_SRC") -I$GEN"
CXX="g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -std=c++17 $INC -I$ROOT/Core -DMOBILE_DEVICE -DUSE_FFMPEG -I$FFMPEG_HOST/include"

echo "== compile glue (psp_core with break APIs)"
$CXX -c "$ROOT/Core/psp_core.cpp" -o "$TMP/g_psp_core.o" || exit 1
echo "== compile drv main"
$CXX -c "$ROOT/scripts/dissidia012-drv.cpp" -o "$TMP/drv-main.o" || exit 1

echo "== link (all existing objects except the stale glue + other mains)"
LIST="$TMP/objs.txt"
ls "$OBJ"/*.o | grep -v "g_psp_core.o" | grep -v "psp-enemy-probe.o" > "$LIST"
wc -l "$LIST"
g++ -O1 -g @"$LIST" "$TMP/g_psp_core.o" "$TMP/drv-main.o" \
  -o "$OUT" -lz -lpthread -ldl \
  "$FFMPEG_HOST"/lib/libavcodec.a "$FFMPEG_HOST"/lib/libavformat.a \
  "$FFMPEG_HOST"/lib/libavutil.a "$FFMPEG_HOST"/lib/libswresample.a \
  "$FFMPEG_HOST"/lib/libswscale.a || exit 1
echo "BUILD-OK $OUT"
