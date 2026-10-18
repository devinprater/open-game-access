#!/usr/bin/env bash
# alttp-adapter-test.sh — build and run the A Link to the Past adapter's host test.
#
# Synthetic 128 KiB work RAM, a stub Host, no ROM: the test checks validation,
# silence while booting, and the dump. Milliseconds.
#
# ⛔ Build artifacts go under $HOME, NOT /tmp (volatile between WSL invocations).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

OUT="${OUT:-$HOME/oga-alttp-test}"

echo "== compiling + linking the adapter test"
rm -f "$OUT"
if ! g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/alttp_adapter_test.cpp Core/alttp_adapter.cpp Core/alttp_text.cpp Core/alttp_names.cpp Core/alttp_rooms.cpp \
    Core/dbz_adapter.cpp Core/dissidia_adapter.cpp Core/dbzar_adapter.cpp Core/osk_echo.cpp \
    Core/nes_adapter.cpp Core/n64_adapter.cpp Core/adapters.cpp Core/mku_adapter.cpp Core/dq9_adapter.cpp \
    Core/announce.cpp -lpthread -ldl -lm; then
  echo "!! test failed to build" >&2
  exit 1
fi

echo "== running"
"$OUT"
