#!/usr/bin/env bash
# ps1-map-harness.sh — build tools/ps1-map-harness.cpp against the PCSX objects that
# scripts/ps1-core-test.sh leaves in $OUT (run that first), then play a script on a disc.
#
#   scripts/ps1-map-harness.sh DISC.cue BIOSDIR OUTDIR SCRIPT
#
# The disc and BIOS are the player's own: read from where they are, never copied into the repo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$HOME/oga-ps1-test}"
[ -d "$OUT/obj" ] || { echo "run scripts/ps1-core-test.sh first (it builds the PCSX objects)" >&2; exit 1; }
if [ ! -x "$OUT/ps1-map-harness" ] || [ "$ROOT/tools/ps1-map-harness.cpp" -nt "$OUT/ps1-map-harness" ] || [ "$ROOT/Core/mkt_adapter.cpp" -nt "$OUT/ps1-map-harness" ] || [ "$ROOT/Core/mkt_adapter.h" -nt "$OUT/ps1-map-harness" ]; then
  c++ -std=c++17 -O1 -g -I"$ROOT/Core" -c "$ROOT/tools/ps1-map-harness.cpp" -o "$OUT/harness.o"
  # The reader and the registry it lives in (the Nintendo readers are stood in for in the harness).
  mkdir -p "$OUT/adobj"
  for f in mku khbbs dbz dbzar dissidia dissidia012 dq9 osk_echo nes n64 genesis ps1 mkt alttp alttp_text alttp_names alttp_rooms alttp_graph alttp_dungeon_graph adapters announce; do
    case $f in dbz|dbzar|dissidia|dissidia012|dq9|nes|n64|genesis|ps1|mkt|mku|khbbs|alttp*|adapters|announce|osk_echo) n="${f}";; esac
    src="$ROOT/Core/${f}_adapter.cpp"; [ -f "$src" ] || src="$ROOT/Core/${f}.cpp"
    c++ -std=c++17 -O1 -w -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -c "$src" -o "$OUT/adobj/$f.o"
  done
  c++ -o "$OUT/ps1-map-harness" "$OUT/harness.o" "$OUT"/adobj/*.o $(ls "$OUT"/obj/*.o | grep -v ps1_core_test) -lpthread -lm -ldl -lEGL -lGL
fi
[ $# -eq 4 ] || exit 0
exec "$OUT/ps1-map-harness" "$@"
