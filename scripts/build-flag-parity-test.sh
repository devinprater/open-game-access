#!/usr/bin/env bash
# build-flag-parity-test.sh — build-core.sh and build-sim.sh must agree on the
# defines that change what gets compiled, not just the file list in
# core-sources.sh. The drift that proved this: device defined -DFE_NO_MAIN=1
# (fe_access.cpp's standalone main() excluded) while sim did not, so the sim
# archive shipped a second _main and the app link died with `duplicate symbol`.
# That failure exists ONLY on the macOS link; this host test is the backstop.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fail=0
for flag in FE_NO_MAIN POKE_IOS MOBILE_DEVICE; do
  for script in build-core.sh build-sim.sh; do
    if grep -q "\-D$flag" "$ROOT/scripts/$script"; then
      echo "ok: $script defines -D$flag"
    else
      echo "FAIL: $script is missing -D$flag" >&2
      fail=1
    fi
  done
done
# Same drift class as FE_NO_MAIN, different shape: both scripts must carry
# the PPSSPP wiring, or one archive silently ships without the PSP core.
for token in PPSPP_SRC PPSPP_INC "ppspp)"; do
  for script in build-core.sh build-sim.sh; do
    if grep -qF -- "$token" "$ROOT/scripts/$script"; then
      echo "ok: $script carries $token"
    else
      echo "FAIL: $script is missing $token" >&2
      fail=1
    fi
  done
done
[ "$fail" -eq 0 ] || exit 1
echo "PASS: device/sim builds agree on linkage inputs (incl. PPSSPP)"
