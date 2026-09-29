#!/usr/bin/env bash
# Build and run the standalone universal OSK echo engine host test.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${OUT:-$HOME/oga-osk-test}"

rm -f "$OUT"
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -std=c++17 \
    -o "$OUT" Core/osk_echo_test.cpp \
    Core/osk_echo.cpp
"$OUT"
