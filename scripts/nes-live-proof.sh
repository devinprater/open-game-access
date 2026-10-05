#!/usr/bin/env bash
# The NES through the APP's PokeCore path (nes-live-proof), plus the direct core RAM check
# (nes-link-probe). Two binaries on purpose: one proves the WIRING, the other proves the CORE.
set -uo pipefail
ROOT=/home/devin/oga-work
MESEN=/home/devin/src/mesen
OUT="$HOME/oga-nes-proof"
cd "$ROOT" || exit 1

[ -f Vendor/hostobj/.failed ] && { echo "!! host build has failures"; exit 1; }
rm -f "$OUT/libnes.a"; ar rcs "$OUT/libnes.a" Vendor/hostobj/*.o || exit 1

build() { # <src> <outname>
  local src="$1" name="$2"
  g++ -O0 -g -std=c++17 -w -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
      -I"$MESEN" -I"$MESEN/Core" -I"$MESEN/Utilities" -include "$MESEN/Core/pch.h" \
      -c "$src" -o "$OUT/$name.o" || { echo "!! compile $name"; return 1; }
  g++ -O1 -g "$OUT/$name.o" "$OUT/libnes.a" -o "$OUT/$name" -lz -lpthread -ldl -lm \
      "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
      "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
      "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/$name-link.log" 2>&1 \
      || { echo "!! link $name"; tail -n 6 "$OUT/$name-link.log"; return 1; }
  echo "== built $name"
}

build /mnt/c/Users/Public/oga-probes/nes-live-proof-main.cpp nes-live-proof || exit 1

mkdir -p "$HOME/nes-rom/home"
echo
echo "############ 1. THE CORE (direct, proves RAM advances) ############"
"$OUT/nes-link-probe" "$HOME/nes-rom/fixture.nes" 300 2>&1 | tail -n 12
echo
echo "############ 2. THE APP PATH (proves the wiring) ############"
"$OUT/nes-live-proof" "$HOME/nes-rom/fixture.nes" "$HOME/nes-rom/home" 2>&1 | tail -n 20
