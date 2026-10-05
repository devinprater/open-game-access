#!/usr/bin/env bash
# Link the NES probe against the full host object set (the real app core + Mesen), then boot a ROM.
set -uo pipefail
ROOT=/home/devin/oga-work
OUT="$HOME/oga-nes-proof"; mkdir -p "$OUT"
cd "$ROOT" || exit 1

bash scripts/build-host.sh 2>&1 | tail -n 3
[ -f Vendor/hostobj/.failed ] && { echo "!! host build failed"; exit 1; }

g++ -O0 -g -fsyntax-only -std=c++17 -w -I Core -I Sources/CPokeCore/include \
  /mnt/c/Users/Public/oga-probes/nes-link-probe.cpp || exit 1

g++ -O1 -g -std=c++17 -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
  -c /mnt/c/Users/Public/oga-probes/nes-link-probe.cpp -o "$OUT/probe.o" || exit 1

rm -f "$OUT/libnes.a"
# Every host object, EXCEPT the harness-only stub: it exists to satisfy psp_* for harnesses that
# never boot a PSP game, and its psp_* definitions are the abort-on-call kind. For the NES probe we
# still need them (pokecore.o references psp_*), so the stub stays IN.
ar rcs "$OUT/libnes.a" "$ROOT"/Vendor/hostobj/*.o || exit 1

g++ -O1 -g "$OUT/probe.o" "$OUT/libnes.a" -o "$OUT/nes-link-probe" \
  -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/link.log" 2>&1 \
  || { echo "!! LINK FAILED"; head -n 30 "$OUT/link.log"; exit 1; }
echo "== linked $OUT/nes-link-probe =="

ROM="${1:-}"
if [ -z "$ROM" ]; then
  ROM=$(find "$HOME"/roms /mnt/c/Users/Devin\ Prater/Dropbox/games -iname "*.nes" 2>/dev/null | head -1)
fi
if [ -z "$ROM" ]; then echo "!! no .nes ROM found; pass one as \$1"; exit 1; fi
echo "ROM: $ROM"

# Copy to native disk: reading a ROM across the C: mount is slow and can time out inside the core.
NATIVE="$HOME/nes-rom"; mkdir -p "$NATIVE"
cp "$ROM" "$NATIVE/game.nes" 2>/dev/null || NATIVE_DIR=""
"$OUT/nes-link-probe" "$NATIVE/game.nes" 300 2>&1 | head -n 40
