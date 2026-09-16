#!/usr/bin/env bash
# survey-gb-gba.sh — survey the existing GB/GBA reader set and the Android mGBA bridge.
#
# Written to establish what ALREADY EXISTS before planning new work. The brief is to wire
# the original Pokémon Access GB/GBA generations to mGBA and hook that into Open Game
# Access, so the first question is not "how do we build this" but "how much of it is
# already here" — the Android port already ships an MGBAScript bridge, and 166 Lua files
# of readers already exist.
set -uo pipefail

G="/c/Users/Devin Prater/AppData/Local/Temp/pokemon-access-gb"
A="/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android"

echo "=============== 1. GB/GBA reader set: top level ==============="
ls "$G" 2>/dev/null | head -30
echo "files at top: $(ls "$G" 2>/dev/null | wc -l)"

echo
echo "=============== 2. per-game trees ==============="
find "$G" -maxdepth 3 -type d 2>/dev/null | sed "s|$G/||" | head -25

echo
echo "=============== 3. Lua file counts by directory ==============="
find "$G" -iname "*.lua" 2>/dev/null | sed "s|$G/||; s|/[^/]*$||" | sort | uniq -c | sort -rn | head -20

echo
echo "=============== 4. entry points / shims ==============="
for f in gb.lua gba.lua main.lua a-star.lua serpent.lua; do
  p="$G/$f"
  [ -f "$p" ] && printf '  %-14s %s bytes\n' "$f" "$(stat -c %s "$p")"
done
# The script host differs per emulator: BizHawk and mGBA expose DIFFERENT APIs.
echo
echo "  --- which host API do the readers target? ---"
grep -rlE "emu\.|joypad\.|memory\." "$G" --include=*.lua 2>/dev/null | head -5
echo "  --- mgba-specific symbols? ---"
grep -rlE "mgba\.|console:log|emu:read" "$G" --include=*.lua 2>/dev/null | head -5

echo
echo "=============== 5. the Android mGBA bridge (already working) ==============="
find "$A" -path "*/.cxx" -prune -o -name "MGBAScript*" -print 2>/dev/null | head -6
find "$A" -path "*/.cxx" -prune -o -name "MGBACore*" -print 2>/dev/null | head -6
echo
echo "  --- MGBAScriptJNI.cpp size and role ---"
J="$A/app/src/main/cpp/MGBAScriptJNI.cpp"
[ -f "$J" ] && { echo "  $J: $(stat -c %s "$J") bytes"; grep -nE "^extern|^JNIEXPORT|^static|Java_com" "$J" 2>/dev/null | head -12; }

echo
echo "=============== 6. GBA core choice in the Android app ==============="
grep -nE "mgba|MGBA|gba" "$A/app/build.gradle.kts" 2>/dev/null | head -8
grep -nE "mgba|MGBA" "$A/app/src/main/cpp/CMakeLists.txt" 2>/dev/null | head -10
