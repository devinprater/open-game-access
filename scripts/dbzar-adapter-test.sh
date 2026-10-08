#!/usr/bin/env bash
# dbzar-adapter-test.sh -- host test for the DBZ: Shin Budokai - Another Road (PSP) adapter.
#
# Same shape as dbz-adapter-test.sh: compile the adapter with its test against synthetic
# RAM and run it. No ROM, no emulator, no PSP.
#
# ⛔ THE ROOT IS DERIVED FROM THIS SCRIPT'S OWN LOCATION, never hardcoded: a dev-machine
# path makes a gate a different program in CI, and this project has been bitten by that
# twice.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OGA_TEST_OUT:-$ROOT/.build/host-tests}"
mkdir -p "$OUT"
CXX="${CXX:-g++}"

"$CXX" -std=c++17 -O0 -g -I "$ROOT/Core" \
  -o "$OUT/dbzar-test" \
  "$ROOT/Core/dbzar_adapter_test.cpp" \
  "$ROOT/Core/dbzar_adapter.cpp" \
  "$ROOT/Core/adapters.cpp" \
  "$ROOT/Core/announce.cpp"

"$OUT/dbzar-test"
