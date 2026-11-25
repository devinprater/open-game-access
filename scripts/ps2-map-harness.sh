#!/usr/bin/env bash
# ps2-map-harness.sh — build tools/ps2-map-harness.cpp against the PS2 core
# wrapper (Core/ps2_core.cpp) and run it. The wrapper dlopen()s its core, so
# the harness needs only the wrapper, libretro.h (from the PCSX checkout the
# deps bootstrap clones), and the platform GL/EGL libraries.
#
#   scripts/ps2-map-harness.sh --rom DISC.iso --out DIR [--core CORE.so]
#                              [--cores DIR] [--system BIOSDIR] [--frames N] ...
#
# The disc and BIOS are the player's own: read from where they are, never
# copied into the repo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/oga-ps2-test}"
PCSX_SRC="${PCSX_SRC:-$HOME/src/pcsx_rearmed}"
mkdir -p "$OUT"
if [ ! -x "$OUT/ps2-map-harness" ] || [ "$ROOT/tools/ps2-map-harness.cpp" -nt "$OUT/ps2-map-harness" ] || [ "$ROOT/Core/ps2_core.cpp" -nt "$OUT/ps2-map-harness" ] || [ "$ROOT/Core/ps2_core.h" -nt "$OUT/ps2-map-harness" ]; then
  c++ -std=c++17 -O1 -g -I"$ROOT/Core" -I"$PCSX_SRC/deps/libretro-common/include" \
    "$ROOT/tools/ps2-map-harness.cpp" "$ROOT/Core/ps2_core.cpp" \
    -o "$OUT/ps2-map-harness" -ldl -lEGL -lGL -lpthread
fi
exec "$OUT/ps2-map-harness" "$@"
