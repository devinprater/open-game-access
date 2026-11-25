#!/usr/bin/env bash
# Build and run the standalone Mortal Kombat Trilogy (PlayStation) adapter host test.
#
# The test lays a fake 2 MiB of PlayStation RAM out the way the measured map says the real
# game's is, and drives the adapter frame by frame. No disc, no emulator: milliseconds.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${OUT:-$HOME/oga-mkt-test}"

rm -f "$OUT"
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/mkt_adapter_test.cpp \
    Core/mku_adapter.cpp Core/khbbs_adapter.cpp Core/dbz_adapter.cpp Core/dbzar_adapter.cpp Core/dissidia_adapter.cpp Core/dissidia012_adapter.cpp \
    Core/dq9_adapter.cpp Core/osk_echo.cpp Core/nes_adapter.cpp \
    Core/n64_adapter.cpp Core/genesis_adapter.cpp Core/ps1_adapter.cpp Core/ps2_adapter.cpp Core/mkt_adapter.cpp Core/alttp_adapter.cpp Core/alttp_text.cpp Core/alttp_names.cpp Core/alttp_rooms.cpp Core/alttp_graph.cpp Core/alttp_dungeon_graph.cpp Core/adapters.cpp Core/announce.cpp
"$OUT"
