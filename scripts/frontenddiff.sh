#!/usr/bin/env bash
# frontenddiff.sh — what does the Android frontend do around Reset/Boot that my
# glue does not? setDateTime()/setBatteryLevels() run after EVERY Reset there.
set -uo pipefail
A="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/src/main/cpp"

echo "########## setBatteryLevels ##########"
grep -n 'void MelonInstance::setBatteryLevels' -A 20 "$A/MelonInstance.cpp"

echo
echo "########## setDateTime ##########"
grep -n 'void MelonInstance::setDateTime' -A 25 "$A/MelonInstance.cpp"

echo
echo "########## who builds NDSArgs / Renderer for Android? ##########"
grep -n 'Renderer\|NDSArgs\|JIT\|OutputSampleRate' "$A/EmulatorArgsBuilder.cpp" | head -20
echo "--- header ---"
grep -n 'Renderer\|NDSArgs' "$A/EmulatorArgsBuilder.h" | head -20
