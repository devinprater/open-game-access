#!/usr/bin/env bash
# announce-test.sh — build and run the announcement-queue host test, then prove it guards
# every rule it claims to.
#
# Pass 1: the real build must pass (with ASan/UBSan).
# Pass 2: rebuild once per SABOTAGE_* switch in Core/announce.cpp — each one deletes a single
# rule — and REQUIRE the test to fail. A test that still passes with its rule removed was never
# testing that rule. No ROMs, no emulator, no platform speech.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${OUT:-$HOME/oga-announce-test}"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

build() {  # build <out> [extra flags...]
  local out="$1"; shift
  g++ -std=c++17 -O1 -g -Wall -Wextra $SAN -ICore "$@" -o "$out" \
      Core/announce_test.cpp Core/announce.cpp
}

echo "== real build"
rm -f "$OUT"
build "$OUT" || { echo "!! build failed" >&2; exit 1; }
"$OUT" || exit 1

SABOTAGES=(
  SABOTAGE_NO_RATE_LIMIT        # distinctness-only guard: per-frame HP floods
  SABOTAGE_GROUP_ALWAYS_MATCH   # replacement across groups eats unrelated lines
  SABOTAGE_DEDUP_LABEL_ONLY     # key without value drops HP 41 -> 40
  SABOTAGE_DIAG_GATES_PLAY      # turning diagnostics off silences play speech
  SABOTAGE_EVICT_NEWEST         # a full queue drops the newest instead of the oldest ambient
  SABOTAGE_NO_IN_FLIGHT_GATE    # everything handed to the platform at once
  SABOTAGE_STALE_DONE           # a late done from an interrupted line releases the queue
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
