#!/usr/bin/env bash
# Build and run the standalone Dissidia adapter host test.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${OUT:-$HOME/oga-dissidia-test}"

rm -f "$OUT"
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/dissidia_adapter_test.cpp \
    Core/dissidia_adapter.cpp Core/adapters.cpp
"$OUT"
