#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${OUT:-$HOME/oga-dissidia012-test}"
g++ -std=c++17 -O1 -ICore -ISources/CPokeCore/include -o "$OUT" Core/dissidia012_adapter_test.cpp Core/dissidia012_adapter.cpp Core/dissidia_adapter.cpp Core/dbzar_adapter.cpp Core/dq9_adapter.cpp Core/osk_echo.cpp Core/nes_adapter.cpp Core/n64_adapter.cpp Core/genesis_adapter.cpp Core/alttp_adapter.cpp Core/alttp_text.cpp Core/alttp_names.cpp Core/alttp_rooms.cpp Core/alttp_graph.cpp Core/alttp_dungeon_graph.cpp Core/adapters.cpp Core/mku_adapter.cpp Core/khbbs_adapter.cpp Core/announce.cpp
"$OUT"
