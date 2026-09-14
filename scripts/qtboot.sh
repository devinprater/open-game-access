#!/usr/bin/env bash
# qtboot.sh — the reference sequence: how the Qt frontend boots a ROM.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== files ==="
ls "$SRC/frontend" 2>/dev/null | head

echo
echo "=== EmuInstance / frontend boot calls ==="
for f in "$SRC"/frontend/*.cpp "$SRC"/frontend/qt_sdl/*.cpp; do
  [ -f "$f" ] || continue
  if grep -lq 'SetupDirectBoot' "$f" 2>/dev/null; then
    echo "--- $f"
    grep -n 'SetupDirectBoot\|SetNDSCart\|->Reset()\|Start()' "$f" | head -12
  fi
done

echo
echo "=== NDS::Start() body ==="
grep -n 'void NDS::Start()' -A 25 "$SRC/NDS.cpp" | head -30
