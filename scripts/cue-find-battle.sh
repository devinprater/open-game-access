#!/usr/bin/env bash
# Find a savestate that is IN A BATTLE, by asking the adapter.
#
# The adapter is the authority on "am I in a battle" (BattleFighters() resolving is exactly
# what CmdBattleSelf/CmdLock gate on), so rather than guess from directory names, load each
# candidate state and ask the core. CueSnapshotFill answers battle/locked/dist; if it says
# battle=true the cue has something real to track.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"

cat > /tmp/probe-battle.cpp <<'EOF'
#include <cstdio>
#include <cstring>
#include "pokecore.h"
#include "adapter.h"

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    PokeCore* core = poke_create();
    if (!core) return 1;
    if (!poke_load_rom(core, argv[1], "/tmp/probe-save")) return 1;
    if (!poke_start(core)) return 1;
    if (!poke_load_state(core, argv[2])) { printf("LOADFAIL\n"); return 1; }
    // Attach the adapter the way the app does: ask it something real.
    poke_adapter_ready(core);
    (void) poke_command(core, (int)oga::Command::MenuState);
    for (int i = 0; i < 8; i++) poke_frame(core);
    float dist = 0;
    int st = poke_cue_snapshot(core, &dist);
    printf("STATE=%d DIST=%.1f\n", st, dist);
    poke_stop(core);
    poke_destroy(core);
    return 0;
}
EOF

g++ -O1 -g -std=c++17 -I Core -I Sources/CPokeCore/include \
  /tmp/probe-battle.cpp -c -o "$OUT/probe-battle.o" || exit 1

# The registry TU references every sibling adapter, so the link needs them all. The ones
# whose sources are NOT in hostobj (n64, nes) get built here -- the same missing-sibling
# trap dbz_adapter_test.cpp documents.
for extra in n64_adapter nes_adapter; do
  [ -f "Core/$extra.cpp" ] || continue
  g++ -O1 -g -fwrapv -fno-strict-aliasing -std=c++17 -I Core \
    -c "Core/$extra.cpp" -o "$OUT/$extra.o" || exit 1
done

rm -f "$OUT/libprobe.a"
ar rcs "$OUT/libprobe.a" "$ROOT"/Vendor/hostobj/*.o \
  "$OUT"/n64_adapter.o "$OUT"/nes_adapter.o \
  $(ls "$OUT"/host-obj/*.o | grep -v "proof-main.o") || exit 1

g++ -O1 -g "$OUT/probe-battle.o" "$OUT/libprobe.a" -o "$OUT/probe-battle" \
  -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/probe-link.log" 2>&1 \
  || { echo "!! link failed"; tail -20 "$OUT/probe-link.log"; exit 1; }
echo "linked probe-battle"
echo

export PPSSPP_ASSETS="$OUT/asset-subset"
for st in "$@"; do
  label="$(basename "$(dirname "$st")")/$(basename "$st")"
  printf "%-40s " "$label"
  timeout 180 "$OUT/probe-battle" ${DISSIDIA_CSO:-$HOME/dissidia.cso} "$st" 2>/dev/null \
    | tr -d '\r' | grep -E "STATE|LOADFAIL" | head -1 || echo "(timeout)"
done
