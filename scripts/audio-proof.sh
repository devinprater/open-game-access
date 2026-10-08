#!/usr/bin/env bash
# audio-proof.sh — measure whether the emulated console's audio carries a SIGNAL.
#
# ⛔ IT REBUILDS FIRST. This script used to link Vendor/hostobj/*.o without rebuilding, so run
# right after a source edit it measured the PREVIOUS objects. That twice produced a confident
# "SILENT OR NEARLY" readout from a tree that already had the fix, and each time it looked like
# the fix had failed. A probe that links objects it did not build is measuring history.
#
# ⛔ AND IT MEASURES THE SIGNAL, NOT THE FRAME COUNT. The ring drains just as well when it holds
# nothing but zeros, so "2048 frames returned" passed on pure silence. Peak amplitude and the
# nonzero fraction are what distinguish sound from a drained empty buffer.
set -u

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1
T="${TMPDIR:-$HOME/fe}/audio-proof"; mkdir -p "$T"

echo "== rebuilding the host objects (this script links them, so it must build them)"
bash scripts/build-host.sh > "$T/build.log" 2>&1 || { echo "SKIP: build-host.sh failed"; tail -5 "$T/build.log"; exit 0; }

cat > "$T/p.cpp" <<'CPP'
#include "pokecore.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    if (!poke_load_rom(core, argv[1], nullptr)) { printf("LOADFAIL\n"); return 1; }
    poke_set_script_dir(core, argv[2]);
    poke_set_audio_enabled(core, true);
    if (!poke_start(core)) { printf("STARTFAIL\n"); return 1; }
    for (int f = 0; f < 900; f++) poke_frame(core);

    static int16_t buf[2048 * 2];
    long total = 0, nonzero = 0; int peak = 0; double sumsq = 0;
    for (int f = 0; f < 3000; f++) {
        poke_frame(core);
        int n = poke_read_audio(core, buf, 2048);
        for (int i = 0; i < n * 2; i++) {
            int v = buf[i];
            total++; if (v) nonzero++;
            if (abs(v) > peak) peak = abs(v);
            sumsq += (double) v * v;
        }
    }
    printf("frames_read=%ld  samples=%ld  nonzero=%ld  peak=%d  rms=%.1f\n",
           total / 2, total, nonzero, peak, total ? sqrt(sumsq / total) : 0.0);
    printf("VERDICT: %s\n", (peak > 500 && nonzero > total / 10) ? "NON-SILENT AUDIO" : "SILENT OR NEARLY");
    return 0;
}
CPP

if ! g++ -O2 -g -DPOKE_HOST=1 -ICore -ISources/CPokeCore/include \
      -I"$HOME/src/melonds-lua/src" -std=c++17 \
      -o "$T/p" "$T/p.cpp" Vendor/hostobj/*.o -lpthread -lm -ldl 2> "$T/link.log"; then
  echo "SKIP: the probe did not link"; tail -5 "$T/link.log"; exit 0
fi

ROMDIR="${OGA_ROM_DIR:-/mnt/c/Users/Devin Prater/Dropbox/games/GBA}"
found=0
for ROM in "$ROMDIR/Pokemon - Crystal Version (USA).gbc" \
           "$ROMDIR/Pokemon - FireRed Version (USA).gba"; do
  [ -f "$ROM" ] || continue
  found=1
  echo "=== $(basename "$ROM")"
  timeout 400 "$T/p" "$ROM" "$R/Sources/OpenGameAccess/Resources/gba-lua" 2>/dev/null \
    | grep -E "frames_read|VERDICT" | head -2
done
[ "$found" -eq 1 ] || { echo "SKIP: no cartridge at $ROMDIR (needs the ROM library)"; exit 0; }
