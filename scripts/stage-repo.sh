#!/usr/bin/env bash
# stage-repo.sh — assemble a clean publish tree for open-game-access.
#
# ⛔ WHY A SEPARATE TREE: the working directory contains the emulator core source
# (fetched, not authored), ~1.5 GB of object files and archives, RAM snapshots,
# screenshots, and ROMs sitting one directory away. Publishing from it directly
# means trusting .gitignore to be perfect. Copying only what is meant to ship is
# verifiable by inspection afterwards, which a .gitignore is not.
set -uo pipefail
WIN_SRC="/mnt/c/Users/Devin Prater/open-game-access"
# ⛔ The staging target is a CLONE of the remote, not a directory built from
# nothing. The first version staged into a fresh directory, which meant the git
# history and credential config lived only there and were lost when the tree was
# rebuilt. Staging into a clone keeps the real commit graph.
OUT="${OGA_OUT:-$HOME/oga-work}"

echo "== cleaning $OUT (preserving .git)"
# ⛔ DO NOT `rm -rf` the whole directory: the output IS the git working tree, and
# wiping it deletes .git and the remote config. Clear the contents except .git.
if [ -d "$OUT/.git" ]; then
  find "$OUT" -mindepth 1 -maxdepth 1 -not -name '.git' -exec rm -rf {} +
else
  rm -rf "$OUT"
fi
mkdir -p "$OUT"

# ---- sources that ship ----
mkdir -p "$OUT/Core" "$OUT/Sources" "$OUT/scripts" "$OUT/fe/plans" "$OUT/docs" "$OUT/app" \
         "$OUT/tools" "$OUT/reverse-engineering"

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
         fe_access.cpp fe_adapter.cpp fedump.cpp probe.cpp screen.cpp ramwatch.cpp ramscan.cpp joytest.cpp \
         simrun.cpp simplay.cpp fwtest.cpp hosttest.c finalcheck.cpp gpustate.cpp \
         timingtest.c version.h version_impl.cpp \
         gba_adapter.cpp gba_adapter_test.cpp \
         fechapter.cpp feterrain2.cpp powerprobe.cpp \
         purecore.cpp ramdump.cpp renderprobe.cpp saveprobe.cpp schedprobe.cpp scriptrun.cpp shot.cpp speedtest2.c timingprobe.cpp vramprobe.cpp; do
  copy "Core/$f"
done

# ⛔ THE ALLOW-LIST SILENTLY DROPS NEW FILES — A REPEAT OFFENCE.
# gba_adapter.cpp / gba_adapter_test.cpp were committed and pushed locally, yet the GitHub
# remote 404'd on them: this list is exhaustive and NOTHING WARNS when a file is missing. The
# same trap previously swallowed tools/re/ and reverse-engineering/.
#
# Guard: every source file in Core/ must either be staged above, or named here as deliberately
# excluded. A new file that is neither makes this fail loudly instead of vanishing.
DELIBERATELY_EXCLUDED="bootcompare.c armstate.cpp blackprobe.cpp crashprobe.cpp frameprobe.cpp instrrate.cpp memtest.cpp memtest2.cpp memtest3.cpp memtest4.cpp memtest5.cpp pcprofile.cpp"
for _f in "$WIN_SRC"/Core/*.cpp "$WIN_SRC"/Core/*.h "$WIN_SRC"/Core/*.c; do
  [ -e "$_f" ] || continue
  _b="$(basename "$_f")"
  case " $DELIBERATELY_EXCLUDED " in *" $_b "*) continue ;; esac
  if [ ! -f "$OUT/Core/$_b" ]; then
    echo "!! stage-repo.sh: Core/$_b is neither staged nor deliberately excluded." >&2
    echo "   Add it to the Core allow-list, or to DELIBERATELY_EXCLUDED." >&2
    exit 1
  fi
done

# Swift app + the C ABI header
copy "Sources/CPokeCore"
copy "Sources/OpenGameAccess"
copy "Package.swift"
copy "xtool.yml"

# build + analysis scripts (the reverse-engineering instruments are the point)
copy "scripts"
copy "wsl.sh"

# experiment plans are documentation of how findings were made
copy "fe/plans"

# The host probes live directly in fe/ (they are instruments, not plans). Only
# fe/plans was staged, so every probe — including the ones that verified the Fire
# Emblem reader — was silently dropped from the published tree.
for f in dbz_probe.cpp; do
  copy "fe/$f"
done

# docs
copy "docs"

# The reverse-engineering toolbox and per-game machine-readable findings.
# ⛔ These MUST be in the allow-list or they are silently dropped from the publish tree:
# the staging script copies named paths only, so a new top-level directory that is not
# listed here never reaches GitHub even though the commit succeeds.
copy "tools"
copy "reverse-engineering"
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
