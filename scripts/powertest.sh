#!/usr/bin/env bash
# powertest.sh — is the LCD power the reason nothing renders?
#
#   NDS.cpp:418  PowerControl9 = 0x820F   (in SetupDirectBoot)
#   NDS.cpp:496  PowerControl9 = 0x0000   (in Reset)
#   GPU.cpp:1130 ScreensEnabled = !!(PowerControl9 & 1)
#
# If ScreensEnabled is false the GPU never draws and the screen stays flat, no
# matter how much the game runs. This checks the access level of SetPowerCnt so a
# harness can force it on and settle the question.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== GPU.h: SetPowerCnt declaration + surrounding access ==="
grep -n 'public:\|private:\|protected:\|SetPowerCnt\|ScreensEnabled' "$SRC/GPU.h" | head -30

echo
echo "=== SetPowerCnt body ==="
grep -n 'void GPU::SetPowerCnt' -A 12 "$SRC/GPU.cpp"

echo
echo "=== context around NDS.cpp:410-425 (SetupDirectBoot) ==="
sed -n '405,425p' "$SRC/NDS.cpp"
