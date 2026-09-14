#!/usr/bin/env bash
# stage-all.sh — full staging pipeline in one shot, in the right order.
#
# Order matters: stage-repo wipes the output directory, so stage-extras (which adds
# .gitignore, LICENSE, workflows and the GB script set) must run AFTER it, and the
# guard runs last on the assembled tree.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OGA_OUT:-$HOME/oga-work}"
GB_SRC="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/src/main/assets/lua/gb"

echo "######## 1. stage the source allow-list ########"
bash "$HERE/stage-repo.sh"

echo
echo "######## 2. add .gitignore, LICENSE, workflows ########"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/.gitignore" "$OUT/"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/LICENSE"    "$OUT/"
mkdir -p "$OUT/.github/workflows"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/.github/workflows/"*.yml "$OUT/.github/workflows/"

echo
echo "######## 3. the Game Boy / GBC / GBA script set ########"
mkdir -p "$OUT/app/src/main/assets/lua"
if [ -d "$GB_SRC" ]; then
  cp -r "$GB_SRC/." "$OUT/app/src/main/assets/lua/gb/"
  echo "  gb scripts: $(find "$OUT/app/src/main/assets/lua/gb" -name '*.lua' | wc -l) .lua files"
  echo "  earcon sounds: $(find "$OUT/app/src/main/assets/lua/gb/sounds" -type f 2>/dev/null | wc -l) files"
else
  echo "  !! GB script set not found at $GB_SRC"
fi

echo
echo "######## 4. guard ########"
bash "/mnt/c/Users/Devin Prater/pokemon-access-ios/scripts/check-no-roms.sh" "$OUT"

echo
echo "######## result ########"
cd "$OUT"
echo "files: $(find . -type f -not -path './.git/*' | wc -l)"
du -sh --exclude=.git .
