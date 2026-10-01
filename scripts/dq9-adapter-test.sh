#!/usr/bin/env bash
# dq9-adapter-test.sh — build and run the Dragon Quest IX adapter's host test.
#
# The adapter turns the mod's documented addresses into speech, so the test
# feeds it a SYNTHETIC image of exactly that layout and checks the words that
# come out. No ROM, no boot: milliseconds.
#
# ⛔ Build artifacts go under $HOME, NOT /tmp. /tmp is volatile between separate
# `wsl.exe -- bash -l script.sh` invocations here, and a vanished object file turns a
# test run into a no-op whose only symptom is an empty log.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

OUT="${OUT:-$HOME/oga-dq9-test}"

echo "== compiling + linking the adapter test"
# The focused test supplies FE/GBA/DBZ/Dissidia registry stubs. Link the real
# DBZ and Dissidia implementations too, because adapters.cpp now registers all.
rm -f "$OUT"
if ! g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/dq9_adapter_test.cpp Core/dq9_adapter.cpp \
    Core/dbz_adapter.cpp Core/dissidia_adapter.cpp Core/osk_echo.cpp \
    Core/adapters.cpp Core/announce.cpp -lpthread -ldl -lm; then
  echo "!! test failed to build" >&2
  exit 1
fi

echo "== running"
"$OUT"
rc=$?
echo "== exit=$rc"
exit $rc
