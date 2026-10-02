#!/usr/bin/env bash
# Build the MemCheck trace tool for the Dissidia enemy-name task.
# Links the fresh glue (psp_core.cpp with the trace-only watch APIs) plus
# scripts/psp-memtrace.cpp against the existing PPSSPP object set; every other
# object is reused untouched.
set -u
ROOT="$HOME/oga-work"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OBJ="$HOME/oga-ppsspp-enemy/obj"
GEN="$HOME/oga-ppsspp-enemy/host-gen"
FFMPEG_HOST="${FFMPEG_HOST:-$HOME/ffmpeg-host}"
OUTDIR="$HOME/oga-ppsspp-enemy"
TMP="$OUTDIR/trace-build"
mkdir -p "$TMP"

# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
INC="$(ppspp_inc "$PPSPP_SRC") -I$GEN"
CXX="g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -std=c++17 $INC -I$ROOT/Core -DMOBILE_DEVICE -DUSE_FFMPEG -I$FFMPEG_HOST/include"

echo "== compile glue (psp_core with trace APIs)"
$CXX -c "$ROOT/Core/psp_core.cpp" -o "$TMP/g_psp_core.o" || exit 1
echo "== compile memtrace main"
$CXX -c "$ROOT/scripts/psp-memtrace.cpp" -o "$TMP/trace-main.o" || exit 1

echo "== link (all existing objects except the stale glue)"
LIST="$TMP/objs.txt"
ls "$OBJ"/*.o | grep -v "g_psp_core.o" | grep -v "psp-enemy-probe.o" > "$LIST"
wc -l "$LIST"
g++ -O1 -g @"$LIST" "$TMP/g_psp_core.o" "$TMP/trace-main.o" \
  -o "$OUTDIR/psp-memtrace" -lz -lpthread -ldl \
  "$FFMPEG_HOST"/lib/libavcodec.a "$FFMPEG_HOST"/lib/libavformat.a \
  "$FFMPEG_HOST"/lib/libavutil.a "$FFMPEG_HOST"/lib/libswresample.a \
  "$FFMPEG_HOST"/lib/libswscale.a || exit 1
echo "BUILD-OK $OUTDIR/psp-memtrace"
