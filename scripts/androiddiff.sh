#!/usr/bin/env bash
# androiddiff.sh — the Android frontend runs this ROM correctly with the SAME
# core. What does its boot sequence do that pokecore.cpp does not?
set -uo pipefail
A="$HOME/pokemon-access-ios/native/melonDS-android/app/src/main/cpp"

echo "=== files ==="
ls "$A" | head -20

echo
echo "=== MelonDS.cpp: the loadRom / setup path ==="
grep -n 'loadRom\|NDS(\|nds->Reset\|SetupDirectBoot\|SetNDSCart\|->Start()\|Boot()' "$A/MelonDS.cpp" | head -30

echo
echo "=== MelonInstance.cpp: console setup ==="
grep -n 'NDS(\|nds->Reset\|SetupDirectBoot\|SetNDSCart\|->Start()\|NDSArgs\|Firmware\|SetRenderer' "$A/MelonInstance.cpp" | head -30
