#!/usr/bin/env bash
# ps2-core-test.sh — build Core/ps2_core_test.cpp against the PS2 wrapper and
# run it. The no-core half (refusals in words) always runs; the core half
# runs when the pinned Linux Play! core is on disk (scripts/bootstrap-deps.sh
# fetches it to $OUT) or OGA_PS2_TEST_CORE names one; the disc half runs when
# OGA_PS2_TEST_DISC names the player's own PS2 ISO.
#
#   scripts/ps2-core-test.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/oga-ps2-test}"
PCSX_SRC="${PCSX_SRC:-$HOME/src/pcsx_rearmed}"
CXX="${CXX:-c++}"
mkdir -p "$OUT"

# -lEGL/-lGL: the wrapper owns a headless GL context on EGL platforms. On a
# platform without them the wrapper compiles its GL out (see ps2_core.cpp)
# and the link line drops both.
GLLIBS="-lEGL -lGL"
if ! printf 'int main(){return 0;}' | $CXX -x c++ - -o /dev/null -lEGL -lGL 2>/dev/null; then
  GLLIBS=""
fi
$CXX -std=c++17 -O1 -g -Wall -Wextra -Werror -ICore -I"$PCSX_SRC/deps/libretro-common/include" \
  Core/ps2_core_test.cpp Core/ps2_core.cpp -o "$OUT/ps2_core_test" -ldl $GLLIBS -lpthread

CORE="${OGA_PS2_TEST_CORE:-$OUT/play_libretro.so}"
# The pinned bootstrap location, when the local copy is absent.
if [ ! -f "$CORE" ] && [ -f "$HOME/src/ps2-cores/linux/play_libretro.so" ]; then
  CORE="$HOME/src/ps2-cores/linux/play_libretro.so"
fi
if [ -f "$CORE" ]; then
  "$OUT/ps2_core_test" "$CORE" ${OGA_PS2_TEST_DISC:+"$OGA_PS2_TEST_DISC"}
else
  echo "note: no pinned Play! core at $CORE — running the refusal half only" >&2
  "$OUT/ps2_core_test"
fi
