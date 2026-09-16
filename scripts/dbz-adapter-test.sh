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

OBJ="$ROOT/Vendor/hostobj"
OUT="${OUT:-$HOME/oga-dbz-test}"

if [ ! -f "$OBJ/adapters.o" ] || [ ! -f "$OBJ/dbz_adapter.o" ]; then
  echo "== host objects missing; building"
  bash scripts/build-host.sh >/dev/null 2>&1 || { echo "!! build-host.sh failed" >&2; exit 1; }
fi

for f in dbz_adapter adapters; do
  [ -f "$OBJ/$f.o" ] || { echo "!! missing $OBJ/$f.o — is Core/$f.cpp in build-host.sh?" >&2; exit 1; }
done

echo "== compiling + linking the adapter test"
g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$OUT" Core/dbz_adapter_test.cpp "$OBJ/dbz_adapter.o" "$OBJ/adapters.o" \
    -lpthread -ldl -lm 2>&1 | grep -viE '^$|warning:' | head -12

[ -x "$OUT" ] || { echo "!! test failed to build" >&2; exit 1; }

echo "== running"
"$OUT"
rc=$?
echo "== exit=$rc"
exit $rc
