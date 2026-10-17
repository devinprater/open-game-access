#!/usr/bin/env bash
# Build and run the standalone Mortal Kombat: Unchained adapter host test.
#
# The adapter is a scaffold — identity, honest speech, and the DumpState
# anchor scan — so the test feeds it a SYNTHETIC RAM image with menu strings
# planted at known addresses and checks the scan reports exactly those, no
# more. No game image, no boot: milliseconds.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${OUT:-$HOME/oga-mku-test}"

rm -f "$OUT"
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/mku_adapter_test.cpp \
    Core/mku_adapter.cpp Core/dbz_adapter.cpp Core/dissidia_adapter.cpp \
    Core/dq9_adapter.cpp Core/osk_echo.cpp Core/nes_adapter.cpp \
    Core/n64_adapter.cpp Core/adapters.cpp Core/announce.cpp
"$OUT"
