#!/usr/bin/env bash
# stage-repo.sh — assemble a clean publish tree for open-game-access.
#
# ⛔ WHY A SEPARATE TREE: the working directory contains the emulator core source
# (fetched, not authored), ~1.5 GB of object files and archives, RAM snapshots,
# screenshots, and ROMs sitting one directory away. Publishing from it directly
# means trusting .gitignore to be perfect. Copying only what is meant to ship is
# verifiable by inspection afterwards, which a .gitignore is not.
set -uo pipefail
WIN_SRC="/mnt/c/Users/Devin Prater/pokemon-access-ios"
OUT="$HOME/open-game-access"

echo "== cleaning $OUT"
rm -rf "$OUT"
mkdir -p "$OUT"

# ---- sources that ship ----
mkdir -p "$OUT/Core" "$OUT/Sources" "$OUT/scripts" "$OUT/fe/plans" "$OUT/docs" "$OUT/app"

# ⛔ `cp -r src dest` copies INTO dest when dest already exists, producing
# scripts/scripts and docs/docs. Copy the CONTENTS (`src/.`) into the destination
# so the staged tree matches the intended layout exactly.
copy() { # copy <relative-path>
  local src="$WIN_SRC/$1" dst="$OUT/$1"
  if [ -d "$src" ]; then
    mkdir -p "$dst"
    cp -r "$src/." "$dst/"
  elif [ -f "$src" ]; then
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
  else
    echo "  !! missing: $1"
    return 1
  fi
}

# Core: our glue + the instruments, but NOT the emulator (fetched by scripts)
for f in adapter.h adapters.cpp pokecore.cpp poke_platform.cpp poke_platform.h poke_internal.h \
         fe_access.cpp fedump.cpp probe.cpp screen.cpp ramwatch.cpp ramscan.cpp joytest.cpp \
         simrun.cpp simplay.cpp fwtest.cpp hosttest.c finalcheck.cpp gpustate.cpp \
         timingtest.c version.h version_impl.cpp; do
  copy "Core/$f"
done

# Swift app + the C ABI header
copy "Sources/CPokeCore"
copy "Sources/PokemonAccess"
copy "Package.swift"
copy "xtool.yml"

# build + analysis scripts (the reverse-engineering instruments are the point)
copy "scripts"
copy "wsl.sh"

# experiment plans are documentation of how findings were made
copy "fe/plans"

# docs
copy "docs"
copy "README.md"
copy "STATUS-AND-TEST-PLAN.md"
copy "TEST_RESULTS.md"
copy "NDS-GAME-FEASIBILITY.md"
copy "TIER1-RESEARCH-STATUS.md"

# ---- the Android app: sources and build files only ----
AND="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app"
if [ -d "$AND/app/src" ]; then
  mkdir -p "$OUT/app/src"
  cp -r "$AND/app/src/main" "$OUT/app/src/" 2>/dev/null
  for f in build.gradle.kts settings.gradle.kts gradle.properties build-apk.sh README.md BUILD_NOTES.md; do
    [ -f "$AND/$f" ] && cp "$AND/$f" "$OUT/app/" 2>/dev/null
  done
  [ -d "$AND/gradle" ] && cp -r "$AND/gradle" "$OUT/app/" 2>/dev/null
  echo "  android app staged: $(find "$OUT/app" -type f | wc -l) files"
else
  echo "  !! android app not found at $AND"
fi

# ---- provenance for the Android core integration (it lives inside a clone) ----
cd "$AND/native/melonDS-android" 2>/dev/null && {
  mkdir -p "$OUT/app/native-overlay"
  for f in app/src/main/cpp/PokeScript.cpp app/src/main/cpp/PokeScript.h \
           app/src/main/cpp/MGBAScriptJNI.cpp app/src/main/cpp/MGBACore.cpp app/src/main/cpp/MGBACore.h \
           app/src/main/cpp/MGBARunner.cpp app/src/main/cpp/MGBARunner.h; do
    [ -f "$f" ] && { mkdir -p "$OUT/app/native-overlay/$(dirname "$f")"; cp "$f" "$OUT/app/native-overlay/$f"; }
  done
  for d in app/src/main/java/me/magnum/melonds/accessibility; do
    [ -d "$d" ] && { mkdir -p "$OUT/app/native-overlay/$d"; cp -r "$d/." "$OUT/app/native-overlay/$d/"; }
  done
  echo "  android core overlay staged: $(find "$OUT/app/native-overlay" -type f 2>/dev/null | wc -l) files"
}
cd "$OUT" || exit 1

# ---- the guard: refuse to publish anything that looks like game data ----
echo
echo "== scanning the staged tree for game data (must be empty)"
BAD=$(find "$OUT" -type f \( -iname '*.nds' -o -iname '*.gba' -o -iname '*.gbc' -o -iname '*.gb' \
      -o -iname '*.sav' -o -iname '*.srm' -o -iname '*.dsv' -o -iname '*.state' \
      -o -iname 'bios*.bin' -o -iname 'firmware*.bin' -o -iname '*.xip' -o -iname '*.a' \
      -o -iname '*.o' -o -iname '*.ram' -o -iname '*.ppm' -o -iname '*.apk' -o -iname '*.ipa' \) 2>/dev/null)
if [ -n "$BAD" ]; then
  echo "!! GAME DATA OR BUILD OUTPUT FOUND — refusing:"; echo "$BAD"; exit 1
fi
echo "  clean"

LUA=$(find "$OUT" -name '*.lua' 2>/dev/null)
if [ -n "$LUA" ]; then
  echo
  echo "== Lua files that WOULD be published:"
  echo "$LUA"
  echo
  echo "  ⛔ The NDS reader script is the user's own work and is attributed in the"
  echo "     README; confirm it is meant to ship before pushing."
fi

echo
echo "== staged tree =="
du -sh "$OUT"
find "$OUT" -type f | wc -l
echo "files:"
find "$OUT" -maxdepth 2 -type d | sort | head -25
