#!/usr/bin/env bash
# mgba-version-stub-test.sh — the app link needs every MGBA_EXPORT symbol from
# mGBA's generated version.c, which this repo replaces with
# Core/mgba_version_stub.cpp (no CMake configure step here). Two silent traps:
# missing symbols (version.c.in gains one and the stub is not updated) and C++
# internal linkage (const namespace-scope variables are internal by default, so
# the object exports nothing). Both surface only at the FINAL macOS app link.
# This test compiles the stub on the host and asserts each exported symbol from
# version.c.in is a GLOBAL symbol in the object. Needs MGBA_SRC (bootstrap-deps.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MGBA_SRC="${MGBA_SRC:-$HOME/src/mgba}"
STUB="$ROOT/Core/mgba_version_stub.cpp"
IN="$MGBA_SRC/src/core/version.c.in"

[ -f "$STUB" ] || { echo "FAIL: no stub at $STUB" >&2; exit 1; }
[ -f "$IN" ] || { echo "SKIP: no mGBA checkout at $MGBA_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 0; }

OUT="${OUT:-$HOME/oga-mgba-ver-test.o}"
CXX="${CXX:-g++}"
"$CXX" -O1 -std=c++17 -c "$STUB" -o "$OUT"

WANT="$(grep -o 'MGBA_EXPORT[^;]*' "$IN" | grep -o '[A-Za-z_][A-Za-z0-9_]* *= *' | tr -d ' =')"
NM="$(command -v llvm-nm || command -v nm)"
missing=0
for sym in $WANT; do
  if "$NM" -g "$OUT" 2>/dev/null | grep -q " [A-Z] $sym\$"; then
    echo "ok: $sym exported"
  else
    echo "FAIL: $sym not a global symbol in $OUT" >&2
    missing=1
  fi
done
[ "$missing" -eq 0 ] || exit 1
echo "PASS: mgba version stub exports all generated symbols"
