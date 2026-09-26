#!/usr/bin/env bash
# gba-adapter-test.sh — build and run the GBA adapter's host test.
#
# The adapter is compiled against the real Core/adapter.h with a STUB host built to be NO MORE
# CAPABLE than the real one (only the six declared callbacks; it can also report "not allocated
# yet" so the adapter's negative paths are exercised).
#
# ⛔ This test proves SELECTION and HONESTY (game-code matching, readiness, refusing to invent
# a position). It CANNOT prove the addresses are right on a real ROM — that is proven by
# running the reader in mGBA; see tools/re/platforms/gba/FOOTSTEP-RESULT.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${OUT:-$HOME/oga-gba-test}"
rm -f "$OUT"
if ! g++ -O2 -ICore -std=c++17 -o "$OUT" \
    Core/gba_adapter_test.cpp Core/gba_adapter.cpp Core/dbz_adapter.cpp \
    Core/dissidia_adapter.cpp Core/adapters.cpp; then
  echo "!! build failed" >&2
  exit 1
fi
"$OUT"
