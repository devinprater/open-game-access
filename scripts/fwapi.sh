#!/usr/bin/env bash
# fwapi.sh — confirm the firmware/BIOS API shapes before compiling against them.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== Firmware constructors ==="
grep -n 'Firmware(' "$SRC/SPI_Firmware.h" | head -10

echo
echo "=== IsBootable ==="
grep -n 'IsBootable' "$SRC/SPI_Firmware.h" | head -5

echo
echo "=== ARM9BIOSSize / ARM7BIOSSize ==="
grep -rn 'ARM9BIOSSize' "$SRC/types.h" "$SRC/NDS.h" 2>/dev/null | head -5

echo
echo "=== NDS::Boot() ==="
grep -n 'void Boot()' "$SRC/NDS.h" | head -3

echo
echo "=== NeedsDirectBoot declaration ==="
grep -n 'NeedsDirectBoot' "$SRC/NDS.h" | head -3

echo
echo "=== SPI::GetFirmware ==="
grep -n 'GetFirmware' "$SRC/SPI.h" | head -3
