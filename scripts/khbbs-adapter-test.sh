#!/usr/bin/env bash
# Build and run the standalone Kingdom Hearts: Birth by Sleep adapter host
# test.
#
# The adapter covers identity, honest speech, the DumpState anchor scan,
# the title/difficulty cursors, field state and location, the player
# position and movement-derived facing, the camp menu and command
# inventory, the meld screen, and the lock-on foe readout — so the test
# feeds it a SYNTHETIC RAM image with the structures planted at their
# real addresses, including the live-proven traps: the stale difficulty
# cursor after backing out, the boot dialogs' garbage count, and the
# ambiguous-component case, which must fail closed. No game image, no
# boot: milliseconds.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="${OUT:-$HOME/oga-khbbs-test}"

rm -f "$OUT"
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/khbbs_adapter_test.cpp \
    Core/khbbs_adapter.cpp Core/mku_adapter.cpp Core/dbz_adapter.cpp \
    Core/dbzar_adapter.cpp Core/dissidia_adapter.cpp Core/dq9_adapter.cpp \
    Core/osk_echo.cpp Core/nes_adapter.cpp Core/n64_adapter.cpp \
    Core/alttp_adapter.cpp Core/alttp_text.cpp Core/alttp_names.cpp \
    Core/alttp_rooms.cpp Core/adapters.cpp Core/announce.cpp
"$OUT"
