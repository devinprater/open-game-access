#!/usr/bin/env bash
# corediff.sh — the Android build WORKS with the same ROM. Does its melonDS core
# differ from the source tree the iOS build compiled? A divergent core file would
# explain everything.
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/melonDS-android-lib/src"
I="/home/devin/src/melonds-lua/src"

echo "=== android core dir ==="
ls "$A" 2>/dev/null | head -30

echo
echo "=== file counts ==="
echo "android: $(find "$A" -name '*.cpp' 2>/dev/null | wc -l) cpp"
echo "ios src: $(find "$I" -name '*.cpp' 2>/dev/null | wc -l) cpp"

echo
echo "=== does the android core have the LuaScripts? ==="
find "$A" -iname '*lua*' 2>/dev/null | head

echo
echo "=== compare key files ==="
for f in NDS.cpp GPU.cpp GPU3D.cpp ARM.cpp ARMInterpreter.cpp GPU_Soft.cpp NDSCart.cpp; do
  if [ -f "$A/$f" ] && [ -f "$I/$f" ]; then
    if cmp -s "$A/$f" "$I/$f"; then
      echo "  SAME  $f"
    else
      echo "  DIFF  $f  ($(wc -l < "$A/$f") vs $(wc -l < "$I/$f") lines)"
    fi
  else
    echo "  MISSING one side: $f (android=$([ -f "$A/$f" ] && echo y || echo n) ios=$([ -f "$I/$f" ] && echo y || echo n))"
  fi
done
