#!/usr/bin/env bash
# psp-live-proof.sh — Dissidia live-RAM adapter proof.
#
# The synthetic test proves the adapter LOGIC; this proves its ADDRESSES:
# boot the real game in the real core through the production PokeCore path,
# attach the registry Dissidia adapter to live RAM, and show it readying and
# answering WhereAmI with spoken text.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-/home/devin/oga-ppsspp-proof}"
IMG="${1:-/home/devin/dissidia.cso}"
CAP="${2:-12000}"

bash "$ROOT/scripts/build-host.sh" > /dev/null || exit 1

# PPSSPP host objects are shared with psp-host-proof.sh (same lists); it must
# have run once so host-obj/ exists. Exclude its proof-main.o (own main()).
ls "$OUT"/host-obj/c_*.o > /dev/null 2>&1 || { echo "!! run psp-host-proof.sh once first (PPSSPP host objects)" >&2; exit 1; }

g++ -O1 -g -std=c++17 -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
  "$ROOT/scripts/psp-live-proof-main.cpp" -c -o "$OUT/host-obj/live-main.o" || exit 1
# Pack a host archive (same first-wins semantics as the iOS archive: the two
# vendored xxhash/lua copies coexist, exactly one definition linked).
# shellcheck disable=SC2086
rm -f "$OUT/liblive.a"
ar rcs "$OUT/liblive.a" "$ROOT"/Vendor/hostobj/*.o \
  $(ls "$OUT"/host-obj/*.o | grep -v "proof-main.o" | grep -v "live-main.o") || exit 1
g++ -O1 -g "$OUT/host-obj/live-main.o" "$OUT/liblive.a" \
  -o "$OUT/psp-live-proof" -lz -lpthread -ldl -lm > "$OUT/live-link.log" 2>&1 \
  || { echo "!! live link failed (see $OUT/live-link.log)" >&2; exit 1; }
echo "== linked $OUT/psp-live-proof =="

export PPSSPP_ASSETS="${PPSSPP_ASSETS:-$OUT/asset-subset}"
"$OUT/psp-live-proof" "$IMG" "$OUT/host-save" "$CAP" 2>"$OUT/live-stderr.log"
