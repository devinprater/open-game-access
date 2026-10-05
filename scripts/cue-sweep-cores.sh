#!/usr/bin/env bash
# Sweep every savestate on the machine for one where a LIVE EX CORE exists, i.e. where the
# adapter reports state 3.
#
# WHY: the two battle states I have both sit at enemy-lock for their entire duration. If NO
# state anywhere has a core, then the honest conclusion is that the cue's second timbre has
# never had a subject -- and I need a battle where the ENEMY enters EX mode, which means
# playing one rather than finding one.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG="${DISSIDIA_CSO:-$HOME/dissidia.cso}"

# quick probe: load, cycle the ring, report whether state 3 EVER occurs
cat > "$HOME/cue-out/sweep.cpp" <<'EOF'
#include <cstdio>
#include <cstdlib>
#include "pokecore.h"
#include "adapter.h"
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    PokeCore* core = poke_create(); if (!core) return 1;
    if (!poke_load_rom(core, argv[1], "/home/devin/cue-out/save")) return 1;
    if (!poke_start(core)) return 1;
    if (!poke_load_state(core, argv[2])) { printf("LOADFAIL\n"); return 0; }
    poke_adapter_ready(core);
    (void) poke_command(core, (int)oga::Command::MenuState);
    int s1=0,s2=0,s3=0,n=0;
    int frames = (argc>3)?atoi(argv[3]):600;
    for (int f=0; f<frames; f++) {
        // cycle the lock ring twice during the window
        if (f == frames/4 || f == frames/2 || f == (3*frames)/4) {
            poke_set_button(core, POKE_BTN_L, true);
            for (int k=0;k<6;k++) poke_frame(core);
            f += 6;
            poke_set_button(core, POKE_BTN_L, false);
        }
        if (!poke_frame(core)) break;
        float d=0; int st = poke_cue_snapshot(core,&d);
        if (st==1)s1++; else if (st==2)s2++; else if (st==3){s3++;n++;}
    }
    printf("battle=%d enemy=%d CORE=%d\n", s1, s2, s3);
    poke_stop(core); poke_destroy(core);
    return 0;
}
EOF
g++ -O1 -g -std=c++17 -I Core -I Sources/CPokeCore/include \
  -c "$HOME/cue-out/sweep.cpp" -o "$OUT/sweep.o" || exit 1
rm -f "$OUT/libsweep.a"
ar rcs "$OUT/libsweep.a" Vendor/hostobj/*.o "$OUT/n64_adapter.o" "$OUT/nes_adapter.o" \
  $(ls "$OUT"/host-obj/*.o | grep -v proof-main.o) || exit 1
g++ -O1 -g "$OUT/sweep.o" "$OUT/libsweep.a" -o "$OUT/sweep" -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/sweep-link.log" 2>&1 \
  || { echo "!! link failed"; tail -10 "$OUT/sweep-link.log"; exit 1; }

export PPSSPP_ASSETS="$OUT/asset-subset"
echo "sweeping savestates for a live EX core (600 frames each, ring cycled):"
find "${1:-$HOME/enemy-out}" -name "final.ppst" 2>/dev/null | sort | while read -r st; do
  label="$(basename "$(dirname "$st")")"
  out=$(timeout 200 "$OUT/sweep" "$IMG" "$st" 600 2>/dev/null | tr -d '\r' | grep -E "battle=|LOADFAIL" | head -1)
  case "$out" in
    *CORE=0*) ;;                       # no core: not interesting, stay quiet
    "") ;;
    *) echo "  $label: $out" ;;
  esac
  case "$out" in *"CORE=0") ;; "") echo "  $label: $out" ;; esac
done
echo
echo "(only states with a non-zero CORE count are listed; all others reported no core)"
