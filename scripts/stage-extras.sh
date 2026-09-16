#!/usr/bin/env bash
# stage-extras.sh — files the staged tree needs that live outside the source
# allow-list: the .gitignore, the licence, the CI workflows, and the repo README.
set -uo pipefail
WIN_SRC="/mnt/c/Users/Devin Prater/open-game-access"
OUT="${OGA_OUT:-$HOME/oga-work}"

cp "$WIN_SRC/.gitignore" "$OUT/.gitignore"
cp "$WIN_SRC/LICENSE"    "$OUT/LICENSE"
cp "$WIN_SRC/README.md"  "$OUT/README.md"
mkdir -p "$OUT/.github/workflows"
cp "$WIN_SRC/.github/workflows/"*.yml "$OUT/.github/workflows/"
# app-level build files (the source allow-list only took app/src and a few files)
for f in app/build.gradle.kts app/settings.gradle.kts app/gradle.properties; do
  base="${f##*/}"; src="$WIN_SRC/$f"
  [ -f "$src" ] && cp "$src" "$OUT/app/$base"
done

echo "== staged tree (final)"
cd "$OUT" || exit 1
ls -a
echo
echo "== no-roms guard, against the staged tree =="
bash "$WIN_SRC/scripts/check-no-roms.sh" "$OUT"
echo
echo "== file count / size =="
find . -type f -not -path './.git/*' | wc -l
du -sh --exclude=.git .
echo
echo "== top-level layout =="
find . -maxdepth 2 -type d -not -path './.git*' | sort
