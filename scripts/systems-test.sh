#!/usr/bin/env bash
# systems-test.sh — build and run the system-registry host test, then prove it
# guards every rule it claims to.
#
# Pass 1: the real build must pass (with ASan/UBSan).
# Pass 2: rebuild once per SABOTAGE_* switch in Core/systems.cpp — each one breaks a
#         single rule — and REQUIRE the test to fail. A test that still passes with
#         its rule removed was never testing that rule. Same discipline as
#         announce-test.sh and adapter-tests.sh: no ROMs, no emulator, no platform.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${OUT:-$HOME/oga-systems-test}"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

build() {  # build <out> [extra flags...]
  local out="$1"; shift
  g++ -std=c++17 -O1 -g -Wall -Wextra -Werror $SAN -ICore "$@" -o "$out" \
      Core/systems_test.cpp Core/systems.cpp
}

echo "== real build"
rm -f "$OUT"
build "$OUT" || { echo "!! build failed" >&2; exit 1; }
"$OUT" || exit 1

# Each sabotage removes exactly one rule. If the test still passes, the rule was
# never covered.
SABOTAGES=(
  SABOTAGE_UNKNOWN_IS_DS        # an unrecognised extension resolves to the DS
  SABOTAGE_GB_GETS_XY          # the Game Boy advertises buttons it has no room for
  SABOTAGE_ALL_CLAIM_RUNNABLE  # a console with no core claims to be playable
  SABOTAGE_DS_ONE_SCREEN       # the DS loses a screen
  SABOTAGE_DUPLICATE_IDS       # two rows share an id
  SABOTAGE_REASON_IS_GENERIC   # the refusal stops naming the console
)

echo
echo "== sabotage: each must FAIL the test"
bad=0
for s in "${SABOTAGES[@]}"; do
  if ! build "$OUT-sab" "-D$s" 2>/dev/null; then
    printf '  !! %-30s did not even build\n' "$s"
    bad=1
    continue
  fi
  if "$OUT-sab" >/dev/null 2>&1; then
    printf '  !! %-30s test PASSED with the rule removed (not covered)\n' "$s"
    bad=1
  else
    printf '  ok %-30s test fails as required\n' "$s"
  fi
done
rm -f "$OUT-sab"

echo
if [ "$bad" -ne 0 ]; then
  echo "FAIL: the systems test does not guard every rule it claims." >&2
  exit 1
fi
echo "PASS: the system registry is covered, and every rule is proved by mutation."
