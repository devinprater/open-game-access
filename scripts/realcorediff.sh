#!/usr/bin/env bash
# realcorediff.sh — is the Android core source actually different, or only line
# endings? Compare with CR stripped.
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/melonDS-android-lib/src"
I="/home/devin/src/melonds-lua/src"

for f in NDS.cpp GPU.cpp GPU3D.cpp ARM.cpp GPU_Soft.cpp NDSCart.cpp SPI_Firmware.cpp FreeBIOS.cpp; do
  if [ -f "$A/$f" ] && [ -f "$I/$f" ]; then
    n=$(diff <(tr -d '\r' < "$A/$f") <(tr -d '\r' < "$I/$f") 2>/dev/null | wc -l)
    echo "$f : $n differing lines"
  fi
done

echo
echo "=== show the GPU.cpp differences (first 60) ==="
diff <(tr -d '\r' < "$A/GPU.cpp") <(tr -d '\r' < "$I/GPU.cpp") 2>/dev/null | head -60
