#!/usr/bin/env bash
# firmware.sh — is the game stuck because it needs firmware/BIOS state that
# direct boot did not provide? White-screen-forever is the classic symptom of a
# game waiting on the firmware/menu handshake.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== NeedsDirectBoot: when is direct boot forced? ==="
grep -n 'NeedsDirectBoot' -A 15 "$SRC/NDS.cpp" | head -22

echo
echo "=== SetupDirectBoot(): the no-arg one ==="
sed -n '293,362p' "$SRC/NDS.cpp"

echo
echo "=== SPI/Firmware direct boot ==="
grep -n 'SetupDirectBoot' -A 25 "$SRC/SPI_Firmware.cpp" | head -35
