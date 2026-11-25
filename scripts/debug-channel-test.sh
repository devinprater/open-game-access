#!/usr/bin/env bash
# debug-channel-test.sh — build and run the in-game debug channel host test
# (Core/debug_channel_test.cpp), then prove it guards the rules it claims to.
#
# Pass 1: the real build must pass (with ASan/UBSan).
# Pass 2: rebuild once per SABOTAGE_* switch in Core/debug_channel.h — each one deletes a
# single rule — and REQUIRE the test to fail. No ROMs, no emulator, no platform speech.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${OUT:-$HOME/oga-debug-channel-test}"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

build() {  # build <out> [extra flags...]
  local out="$1"; shift
  g++ -std=c++17 -O1 -g -Wall -Wextra $SAN -ICore "$@" -o "$out" \
      Core/debug_channel_test.cpp -lpthread
}

echo "== real build"
rm -f "$OUT"
build "$OUT" || { echo "!! build failed" >&2; exit 1; }
"$OUT" || exit 1

SABOTAGES=(
  SABOTAGE_NO_RELEASE_GAP     # two presses of one button merge into one long hold
  SABOTAGE_IGNORE_BUSY        # pad and wait stop holding the lines after them
  SABOTAGE_NO_NOISE_MUTE      # a frame counter floods the log (and the speech)
  SABOTAGE_KEEP_IDLE_HOOKS    # a finished trace or an uncount leaves its breakpoint armed
  SABOTAGE_SEARCH_STALE_BASE  # search steps compare with the start, not the step before
  SABOTAGE_SEARCH_NO_LIST     # a search with a few candidates left never names them
)

echo
echo "== sabotage: each must FAIL the test"
bad=0
for s in "${SABOTAGES[@]}"; do
  if ! build "$OUT-sab" "-D$s" 2>/dev/null; then
    echo "!! $s: build failed"; bad=1; continue
  fi
  if "$OUT-sab" > "$OUT-sab.log" 2>&1; then
    echo "!! $s: test still PASSES with the rule removed"
    bad=1
  else
    echo "ok: $s caught ($(grep -c '^FAIL' "$OUT-sab.log") failing checks)"
  fi
done
rm -f "$OUT-sab" "$OUT-sab.log"

if [ "$bad" -ne 0 ]; then
  echo "!! sabotage verification failed" >&2
  exit 1
fi
echo "ALL SABOTAGE CASES CAUGHT"
