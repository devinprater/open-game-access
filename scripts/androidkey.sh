#!/usr/bin/env bash
# androidkey.sh — how does the Android frontend deliver key input, and does it
# invert for the active-low convention?
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/src/main/cpp"

echo "=== search the whole android cpp tree for key/mask handling ==="
grep -rn 'SetKeyMask\|KeyMask\|setKeyMask\|KeysDown\|keyMask' "$A" 2>/dev/null | head -25

echo
echo "=== MelonInstance / MelonDS key API ==="
grep -n 'key\|Key' "$A/MelonDS.h" | head -25
