#!/usr/bin/env bash
# THE decisive test: does the lock target pointer (P+0x2EC) EVER hold an alternate value?
#
# Everything else has been circumstantial. This reads the pointer itself, every frame, and
# reports the complete distribution of values it takes:
#   0            = lock off
#   == [P+0x2F0] = enemy
#   anything else = an alternate object (what the adapter calls the EX core)
#
# If "anything else" never occurs, the state-3 timbre has never had a subject anywhere on
# this machine, and the answer to "why don't I hear it" is that the ring has never produced
# one -- not that the audio is broken.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG="${DISSIDIA_CSO:-$HOME/dissidia.cso}"
mkdir -p "$HOME/cue-out"

cat > "$HOME/cue-out/tgt-dist.cpp" <<'EOF'
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include "pokecore.h"
#include "adapter.h"

namespace oga { namespace dissidia { const Host* HostForProbe(void); } }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    PokeCore* core = poke_create();
    if (!core) return 1;
    if (!poke_load_rom(core, argv[1], "/home/devin/cue-out/save")) return 1;
    if (!poke_start(core)) return 1;
    if (!poke_load_state(core, argv[2])) { printf("LOADFAIL\n"); return 1; }
    poke_adapter_ready(core);
    (void) poke_command(core, (int)oga::Command::MenuState);
    const oga::Host* h = oga::dissidia::HostForProbe();
    if (!h) { printf("NOHOST\n"); return 1; }

    const unsigned MGR = 0x08B955A0u, OFF_FIGHTERS = 0x14u, OFF_PAIR = 0x2F0u;
    int frames = (argc > 3) ? atoi(argv[3]) : 3600;

    int n_off = 0, n_enemy = 0, n_alt = 0, n_none = 0;
    unsigned altValues[8] = {0}; int nAltUniq = 0;

    for (int f = 0; f < frames; f++) {
        // Cycle the whole ring repeatedly: L1 four times per press-burst covers
        // enemy -> ex-core -> off -> enemy.
        if (f % 90 == 0) {
            for (int press = 0; press < 2; press++) {
                poke_set_button(core, POKE_BTN_L, true);
                for (int k = 0; k < 4; k++) poke_frame(core);
                f += 4;
                poke_set_button(core, POKE_BTN_L, false);
                for (int k = 0; k < 4; k++) poke_frame(core);
                f += 4;
            }
        }
        if (!poke_frame(core)) break;

        unsigned m = h->read32(h->ctx, MGR);
        if (!m) { n_none++; continue; }
        unsigned p0 = h->read32(h->ctx, m + OFF_FIGHTERS);
        if (!p0) { n_none++; continue; }
        unsigned tgt = h->read32(h->ctx, p0 + 0x2ECu);
        unsigned enemy = h->read32(h->ctx, p0 + OFF_PAIR);

        if (tgt == 0) n_off++;
        else if (tgt == enemy && enemy != 0) n_enemy++;
        else {
            n_alt++;
            bool known = false;
            for (int i = 0; i < nAltUniq; i++) if (altValues[i] == tgt) known = true;
            if (!known && nAltUniq < 8) altValues[nAltUniq++] = tgt;
        }
    }
    printf("target pointer distribution over %d frames:\n", frames);
    printf("   lock OFF         %6d\n", n_off);
    printf("   enemy            %6d\n", n_enemy);
    printf("   ALTERNATE/CORE   %6d   <-- the cue's second timbre\n", n_alt);
    printf("   unreadable       %6d\n", n_none);
    if (nAltUniq) {
        printf("   distinct alternate pointers seen:\n");
        for (int i = 0; i < nAltUniq; i++) printf("      %08X\n", altValues[i]);
    }
    poke_stop(core); poke_destroy(core);
    return 0;
}
EOF

g++ -O1 -g -std=c++17 -I Core -I Sources/CPokeCore/include \
  -c "$HOME/cue-out/tgt-dist.cpp" -o "$OUT/tgt-dist.o" || exit 1
rm -f "$OUT/libtgt.a"
ar rcs "$OUT/libtgt.a" Vendor/hostobj/*.o "$OUT/n64_adapter.o" "$OUT/nes_adapter.o" \
  $(ls "$OUT"/host-obj/*.o | grep -v proof-main.o) || exit 1
g++ -O1 -g "$OUT/tgt-dist.o" "$OUT/libtgt.a" -o "$OUT/tgt-dist" \
  -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/tgt-link.log" 2>&1 \
  || { echo "!! link failed"; tail -12 "$OUT/tgt-link.log"; exit 1; }

echo "linked"
export PPSSPP_ASSETS="$OUT/asset-subset"
for st in "$@"; do
  echo "----- $(basename "$(dirname "$st")")"
  PPSSPP_ASSETS="$OUT/asset-subset" "$OUT/tgt-dist" "$IMG" "$st" 3600 2>/dev/null | head -n 14
done
