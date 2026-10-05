#!/usr/bin/env bash
# dq9-live-proof.sh — DQ9 live-RAM adapter proof.
#
# The synthetic test (dq9_adapter_test.cpp) proves the adapter LOGIC against an image shaped like
# the mod's documented layout. This proves the ADDRESSES: boot the real ROM in the real melonDS
# core through the production PokeCore path, let the registry hand back the real DQ9 adapter, and
# check BOTH halves --
#
#   * at the TITLE it must REFUSE (a gate that never refuses is a random-text generator)
#   * in game it must report a map code that CHANGES as the game runs
#
# Builds from the same host object set the app uses (scripts/build-host.sh), so a harness can
# never be testing a different core than the app ships.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/oga-dq9-proof}"
BIOS="${DQ9_BIOS:-$HOME/ds-bios}"
ROM="${DQ9_ROM:-$HOME/dq9-rom/dq9.nds}"
CAP="${1:-24000}"

# The ROM is 268 MB; a copy on the Linux filesystem avoids reading it across the C: mount on
# every run. Not required -- only a speed note.
if [ ! -f "$ROM" ]; then
  echo "!! no DQ9 ROM at $ROM" >&2
  echo "   put one there, or set DQ9_ROM=<path>. It is never committed (see the ROM rule)." >&2
  exit 1
fi
[ -d "$BIOS" ] || { echo "!! no DS BIOS dir at $BIOS (bios9.bin, bios7.bin, firmware.bin)" >&2; exit 1; }

bash "$ROOT/scripts/build-host.sh" > /dev/null || { echo "!! host objects failed to build" >&2; exit 1; }

mkdir -p "$OUT"
rm -f "$OUT/dq9-live.o" "$OUT/liblive.a"
g++ -O1 -g -std=c++17 -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
  "$ROOT/scripts/dq9-live-proof-main.cpp" -c -o "$OUT/dq9-live.o" || exit 1
ar rcs "$OUT/liblive.a" "$ROOT"/Vendor/hostobj/*.o || exit 1
g++ -O1 -g "$OUT/dq9-live.o" "$OUT/liblive.a" -o "$OUT/dq9-live-proof" \
  -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a || { echo "!! link failed" >&2; exit 1; }
echo "== linked $OUT/dq9-live-proof =="

SAVE="$OUT/save"; rm -rf "$SAVE"; mkdir -p "$SAVE"
"$OUT/dq9-live-proof" "$ROM" "$SAVE" "$CAP" \
  "$BIOS/bios9.bin" "$BIOS/bios7.bin" "$BIOS/firmware.bin" 2>"$OUT/stderr.log"
