#!/usr/bin/env bash
# snes-alttp-proof.sh — the SNES console's host proof.
#
# One mode per task in docs/plans/alttp-snes-console.md. The script BUILDS the
# core and the probe from source every run, so a stale archive cannot make a
# failed change look green.
#
# ⛔ The ROM is Devin's own dump, read from outside the repo. Nothing here copies
# it in, and scripts/check-no-roms.sh stays honest.
# MEASURED: a .smc with the 512-byte copier header reports the SAME
# payload CRC as the headerless file (777aac2f). Mesen hashed the whole
# file (f59f60d1) until the core began hashing the payload; see the note
# beside CrcTable() in Core/snes_core.cpp.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESEN="${MESEN_SRC:-$HOME/src/mesen}"
OUT="${OUT:-$HOME/oga-snes-proof}"
ROM="${ALTP_ROM:-/mnt/c/Users/Devin Prater/Dropbox/games/snes/Legend of Zelda, The - A Link to the Past (USA).sfc}"
SAVE="${ALTP_SAVE:-$HOME/z3work/alttp-home}"
MODE="${1:---identity}"

[ -d "$MESEN/Core" ] || { echo "!! no Mesen source at $MESEN" >&2; exit 1; }
[ -f "$ROM" ]        || { echo "!! no ROM at $ROM (set ALTP_ROM)" >&2; exit 1; }
[ -d "$ROOT/Vendor/hostobj" ] || { echo "!! run scripts/build-host.sh first" >&2; exit 1; }

mkdir -p "$OUT" "$SAVE"

# The Mesen object set is shared with every other host harness. Re-archive only
# when the build is newer than the archive.
if [ ! -f "$OUT/libmesen.a" ] || \
   [ -n "$(find "$ROOT/Vendor/hostobj" -name '*.o' -newer "$OUT/libmesen.a" -print -quit)" ]; then
  rm -f "$OUT/libmesen.a"
  ar rcs "$OUT/libmesen.a" "$ROOT"/Vendor/hostobj/*.o || { echo "!! ar failed" >&2; exit 1; }
fi

CXXFLAGS=(-O1 -g -std=c++17 -w -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1)

# The core needs Mesen's headers and its forced precompiled header, exactly as
# Core/mesen_core.cpp does.
if ! g++ "${CXXFLAGS[@]}" \
     -I"$ROOT/Core" -I"$MESEN" -I"$MESEN/Core" -I"$MESEN/Utilities" \
     -include "$MESEN/Core/pch.h" \
     -c "$ROOT/Core/snes_core.cpp" -o "$OUT/snes_core.o"; then
  echo "!! compile snes_core.cpp" >&2
  exit 1
fi

# The probe is plain C++ against the core's public header: no Mesen includes,
# so a leak of Mesen types into the probe is a compile error rather than a habit.
if ! g++ "${CXXFLAGS[@]}" -I"$ROOT/Core" \
     -c "$ROOT/Core/snes_probe.cpp" -o "$OUT/snes_probe.o"; then
  echo "!! compile snes_probe.cpp" >&2
  exit 1
fi

FFMPEG="$HOME/ffmpeg-host/lib"
if ! g++ "$OUT/snes_probe.o" "$OUT/snes_core.o" "$OUT/libmesen.a" -o "$OUT/snes-probe" \
     -lz -lpthread -ldl -lm \
     "$FFMPEG/libavformat.a" "$FFMPEG/libavcodec.a" "$FFMPEG/libswresample.a" \
     "$FFMPEG/libswscale.a" "$FFMPEG/libavutil.a" > "$OUT/link.log" 2>&1; then
  echo "!! link snes-probe" >&2
  tail -n 8 "$OUT/link.log" >&2
  exit 1
fi

echo "== $MODE"
exec "$OUT/snes-probe" "$MODE" "$ROM" "$SAVE"
