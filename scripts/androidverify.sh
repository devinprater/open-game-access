#!/usr/bin/env bash
# androidverify.sh — is the Android reference actually verified?
#
# The "Android renders this ROM" belief came from an emulator run that happened
# BEFORE the real melonDS-lua core was swapped in (that build was a stub with no
# core symbols). Everything since has compared against it. Check the evidence.
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android"

echo "=== the shipped APK timestamp vs the core swap ==="
ls -la "$A/app/build/outputs/apk/gitHubProd/debug/" 2>/dev/null

echo
echo "=== does the shipped APK actually contain core code? (look for core symbols) ==="
APK=$(ls "$A/app/build/outputs/apk/gitHubProd/debug/"*.apk 2>/dev/null | head -1)
echo "apk: $APK"

echo
echo "=== does the APK bundle the lua shim + script as assets? ==="
unzip -l "$APK" 2>/dev/null | grep -iE 'lua|bizhawk|main\.lua' | head -10

echo
echo "=== native libs in the APK ==="
unzip -l "$APK" 2>/dev/null | grep -E '\.so$' | head -10

echo
echo "=== any screenshots / evidence from the emulator run, and when? ==="
find "/c/Users/Devin Prater" -maxdepth 3 -iname "*.png" -newermt "2026-09-09" 2>/dev/null | head -10
ls -la "$A/../../../screenshots" 2>/dev/null | head
