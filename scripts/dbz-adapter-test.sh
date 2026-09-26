#!/usr/bin/env bash
# dbz-adapter-test.sh — build and run the AotS adapter's host tests.
#
# The adapter turns a verified memory map into speech, so the test feeds it a
# SYNTHETIC image of exactly that map and checks the words that come out. No ROM, no
# 5-minute boot: milliseconds.
#
# ⛔ Build artifacts go under $HOME, NOT /tmp. /tmp is volatile between separate
# `wsl.exe -- bash -l script.sh` invocations here, and a vanished object file turns a
# test run into a no-op whose only symptom is an empty log.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

OUT="${OUT:-$HOME/oga-dbz-test}"

echo "== compiling + linking the adapter test"
# This focused test supplies FE/GBA registry stubs. Link the real DBZ and
# Dissidia implementations too, because adapters.cpp now registers both.
rm -f "$OUT"
if ! g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/dbz_adapter_test.cpp Core/dbz_adapter.cpp \
    Core/dissidia_adapter.cpp Core/adapters.cpp -lpthread -ldl -lm; then
  echo "!! test failed to build" >&2
  exit 1
fi

echo "== running"
"$OUT"
rc=$?
echo "== exit=$rc"
exit $rc
