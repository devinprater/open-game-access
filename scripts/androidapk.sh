#!/usr/bin/env bash
# androidapk.sh — inspect the real-core Android APK: does it contain the core and
# the bundled accessibility script?
set -uo pipefail
APK="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/build/outputs/apk/gitHubProd/debug/app-gitHub-prod-debug.apk"

ls -la "$APK"
echo
echo "=== native libraries ==="
unzip -l "$APK" | grep -E '\.so$' | head -10
echo
echo "=== bundled lua assets ==="
unzip -l "$APK" | grep -iE 'lua|assets' | head -15
echo
echo "=== total entries ==="
unzip -l "$APK" | tail -3
