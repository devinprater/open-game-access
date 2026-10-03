#!/usr/bin/env bash
# psp-soak.sh — long-run slowdown/leak probe for the PSP path (Dissidia).
#
# Resumes a save state and plays for N frames through the production PokeCore
# path the iOS app uses per frame (poke_frame, poke_framebuffer,
# poke_adapter_ready), printing ms/frame and resident memory every 600 frames.
# Flat RSS + flat ms/frame = no leak and no progressive slowdown in the core.
#
#   scripts/psp-soak.sh <state.ppst> [frames=36000] [image]
#
# Needs the PPSSPP host objects from psp-host-proof.sh (run it once).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/oga-ppsspp-proof}"
STATE="${1:?usage: psp-soak.sh <state.ppst> [frames] [image]}"
FRAMES="${2:-36000}"
IMG="${3:-$HOME/dissidia.cso}"

bash "$ROOT/scripts/build-host.sh" > /dev/null || exit 1
ls "$OUT"/host-obj/c_*.o > /dev/null 2>&1 || { echo "!! run psp-host-proof.sh once first (PPSSPP host objects)" >&2; exit 1; }

# Own objects live OUTSIDE host-obj/: psp-host-proof.sh links host-obj/*.o whole.
mkdir -p "$OUT/soak"
g++ -O1 -g -std=c++17 -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
  "$ROOT/scripts/psp-soak-main.cpp" -c -o "$OUT/soak/soak-main.o" || exit 1
rm -f "$OUT/soak/libsoak.a"
# shellcheck disable=SC2046
ar rcs "$OUT/soak/libsoak.a" "$ROOT"/Vendor/hostobj/*.o \
  $(ls "$OUT"/host-obj/*.o | grep -v "proof-main.o") || exit 1
FFMPEG_HOST="${FFMPEG_HOST:-$HOME/ffmpeg-host}"
g++ -O1 -g "$OUT/soak/soak-main.o" "$OUT/soak/libsoak.a" -o "$OUT/soak/psp-soak" \
  -lz -lpthread -ldl -lm \
  "$FFMPEG_HOST"/lib/libavformat.a "$FFMPEG_HOST"/lib/libavcodec.a \
  "$FFMPEG_HOST"/lib/libswresample.a "$FFMPEG_HOST"/lib/libswscale.a \
  "$FFMPEG_HOST"/lib/libavutil.a > "$OUT/soak/link.log" 2>&1 \
  || { echo "!! soak link failed (see $OUT/soak/link.log)" >&2; exit 1; }

export PPSSPP_ASSETS="${PPSSPP_ASSETS:-$OUT/asset-subset}"
"$OUT/soak/psp-soak" "$IMG" "$OUT/host-save" "$FRAMES" "$STATE" 2>/dev/null
