#!/usr/bin/env bash
# androidcheck.sh — Android is the working reference. Is its APK still intact,
# and does the ROM it uses match the one the iOS harness uses?
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android"

echo "=== Android APK ==="
ls -la "$A/app/build/outputs/apk/gitHubProd/debug/" 2>/dev/null

echo
echo "=== is the Android app's Lua shim + main.lua bundled? ==="
APP="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app"
ls -la "$APP/app/src/main/assets/lua/" 2>/dev/null
echo "script sizes:"
wc -c "$APP/app/src/main/assets/lua/main.lua" 2>/dev/null

echo
echo "=== does the Android ROM match what the iOS harness used? ==="
ls -la "/mnt/c/Users/Devin Prater/Dropbox/Games/NDS/" | grep -i black
