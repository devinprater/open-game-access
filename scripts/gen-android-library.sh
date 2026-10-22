#!/usr/bin/env bash
# gen-android-library.sh — regenerate the Android game library data from the
# Core adapter registry.
#
# The Android app never links the Core registry (its readers are Lua), so the
# library list it shows — AdapterLibraryData.kt — is generated from the real
# registry objects by tools/android-library-gen.cpp and checked in. iOS reads
# the same registry live through poke_adapter_library_*; both present the
# same games because both come from Core/adapters.cpp.
#
# Usage:
#   bash scripts/gen-android-library.sh           rewrite AdapterLibraryData.kt
#   bash scripts/gen-android-library.sh --check   fail if the checked-in file is stale
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT_KT="app/src/main/java/com/devin/opengameaccess/AdapterLibraryData.kt"
BIN="${TMPDIR:-/tmp}/oga-android-library-gen"

# The adapter translation units: the same set the adapter host tests link
# (see scripts/khbbs-adapter-test.sh), plus gba, which those tests stub.
# fe_adapter.cpp is the real one too, but fe_access.cpp — its reader — is a
# standalone host harness with its own main() and a melonDS include graph, so
# the generator stubs its wrappers instead (see tools/android-library-gen.cpp:
# the stubs abort if called; a data generator never calls them, and fe11's
# registry strings come from the real initializer either way).
g++ -O1 -g -fwrapv -fno-strict-aliasing \
    -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" -std=c++17 \
    -o "$BIN" tools/android-library-gen.cpp \
    Core/adapters.cpp Core/announce.cpp \
    Core/fe_adapter.cpp \
    Core/gba_adapter.cpp \
    Core/dbz_adapter.cpp Core/dissidia_adapter.cpp Core/dbzar_adapter.cpp \
    Core/dq9_adapter.cpp Core/mku_adapter.cpp Core/khbbs_adapter.cpp \
    Core/alttp_adapter.cpp Core/alttp_text.cpp Core/alttp_names.cpp \
    Core/alttp_rooms.cpp Core/osk_echo.cpp \
    Core/nes_adapter.cpp Core/n64_adapter.cpp

if [ "${1:-}" = "--check" ]; then
  TMP_OUT="$(mktemp)"
  trap 'rm -f "$TMP_OUT"' EXIT
  "$BIN" > "$TMP_OUT"
  if diff -u "$OUT_KT" "$TMP_OUT" > /dev/null; then
    echo "ok: $OUT_KT matches the Core registry"
  else
    echo "!! $OUT_KT is STALE against the Core registry:" >&2
    diff -u "$OUT_KT" "$TMP_OUT" | head -40 >&2 || true
    echo "   regenerate with: bash scripts/gen-android-library.sh" >&2
    exit 1
  fi
else
  "$BIN" > "$OUT_KT"
  echo "wrote $OUT_KT"
fi
