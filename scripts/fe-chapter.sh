#!/usr/bin/env bash
# fe-chapter.sh — chapter identity, movement range, and dialogue text.
#
# ⛔ PA_SHIM IS SET HERE, NOT INLINE. Building it as a one-liner through wsl.exe from
# Windows breaks on the space in "Devin Prater", and the failure surfaces much later
# as "main.lua:3: attempt to index a nil value (global 'emu')" — which reads like a
# broken script rather than a quoting bug. Script files avoid the whole class of error.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
ROM="${ROM:-$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds}"
FRAMES="${1:-7000}"
# Accept a plan NAME (tutorial2) or a full path, matching fe-terrain.sh / fe-terrain2.sh.
PLAN_ARG="${2:-tutorial2}"
case "$PLAN_ARG" in
  /*) PLAN="$PLAN_ARG" ;;
  *)  PLAN="$ROOT/fe/plans/${PLAN_ARG%.txt}.txt" ;;
esac
BUILD_LOG="$ROOT/Vendor/fechapter-build.log"

[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }
[ -f "$PLAN" ] || { echo "!! no plan at $PLAN" >&2; exit 2; }

echo "== building fechapter =="
# ⛔ THE INCLUDE PATH MATTERS. fechapter dereferences nds->MainRAM, so it needs the real
# melonDS NDS.h — not just a forward declaration. build-host.sh gets it via -I$SRC/src;
# without that flag the compile fails with "'melonDS' has not been declared".
SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
g++ -std=c++17 -O1 -ICore -ISources/CPokeCore/include -I"$SRC/src" -I Vendor \
    -o Vendor/fechapter Core/fechapter.cpp Vendor/hostobj/*.o \
    -lpthread -lm -ldl 2>"$BUILD_LOG"
if [ $? -ne 0 ]; then
  echo "!! compile failed — refusing to run a stale binary:" >&2
  grep -E "error" "$BUILD_LOG" | head -12 >&2
  exit 1
fi
[ -x Vendor/fechapter ] || { echo "!! fechapter did not link" >&2; exit 1; }

OUT="$HOME/fe/out/chapter-$FRAMES.txt"
mkdir -p "$HOME/fe/out"
echo "== running: frames=$FRAMES plan=$(basename "$PLAN") =="
timeout 1200 ./Vendor/fechapter "$ROM" "$FRAMES" "$PLAN" 2>&1 | tee "$OUT" | tail -70
echo
echo "== full output: $OUT ($(wc -l < "$OUT") lines) =="
