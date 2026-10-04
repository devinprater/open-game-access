#!/usr/bin/env bash
# check-no-roms.sh — refuse to publish game data.
#
# ⛔ THIS IS THE GUARD THAT MATTERS. ROMs, BIOS/firmware dumps and save files are
# copyrighted; this project's premise is that players supply their own. A single
# stray `git add .` in a directory that has a ROM two levels up would be a real
# legal problem, so the check looks for game data AND for the emulator build
# output that should never be committed, and exits non-zero on either.
#
# Run by CI before upload. Commits are guarded by scripts/git-hooks/pre-commit.
#
# Usage: check-no-roms.sh [root] [--skip-build-output]
set -uo pipefail
ROOT="."
ROOT_SET=0
SKIP_BUILD_OUTPUT=0
for arg in "$@"; do
  case "$arg" in
    --skip-build-output) SKIP_BUILD_OUTPUT=1 ;;
    *) if [ "$ROOT_SET" -eq 0 ]; then ROOT="$arg"; ROOT_SET=1; fi ;;
  esac
done
cd "$ROOT" || exit 2

# --skip-build-output skips the artifact/size scans, which are only meaningful on a
# SOURCE tree. CI runs this AFTER building, so those scans otherwise find the APK
# they are about to upload and fail the step.
#
# ⛔ The whole `frontend/` tree is fetched and built, not our source: gradle output,
# app/build, app/.cxx (CMake/Ninja objects) AND its own nested .git with submodule
# packfiles over 10 MB. Excluding only app/build left three other ways for a
# SUCCESSFUL build to fail this guard, which is exactly what happened once.
# Vendor/ is likewise local build output (0 tracked files).
#
# ⛔ THE GAME-DATA SCAN BELOW IS NEVER PRUNED. A ROM must fail this even inside
# build output.
PRUNE=(-not -path './.git/*')
if [ "$SKIP_BUILD_OUTPUT" -eq 1 ]; then
  PRUNE=(
    -not -path './.git/*'
    -not -path './frontend/*'          # fetched shell + gradle/CMake output + nested .git
    -not -path './Vendor/*'            # host/device build output (0 tracked files)
    -not -path './.build/*'            # swift build output
    -not -path './build/*'
    -not -path '*/obj/*'               # per-TU objects from build-core.sh
    -not -path './xtool/*'             # iOS archive output
    -not -path './xtool-sim/*'         # iOS simulator app output
  )
fi

fail=0

echo "== scanning for game data and build output under $ROOT"

# --- game data: the hard stop (never pruned) ---
GAME=$(find . -type f \( \
    -iname '*.nds' -o -iname '*.dsi' -o -iname '*.gba' -o -iname '*.gbc' -o -iname '*.gb' \
    -o -iname '*.sav' -o -iname '*.srm' -o -iname '*.dsv' -o -iname '*.st[0-9]' \
    -o -iname '*.state' -o -iname '*.SaveRAM' \
    -o -iname 'bios7.bin' -o -iname 'bios9.bin' -o -iname 'firmware.bin' \
    -o -iname 'bios*.bin' -o -iname 'firmware*.bin' -o -iname '*.xip' \) \
    -not -path './.git/*' 2>/dev/null)
if [ -n "$GAME" ]; then
  echo "!! GAME DATA FOUND — this must not be published:"
  echo "$GAME"
  fail=1
else
  echo "   no ROMs / BIOS / firmware / saves"
fi

# --- emulator source that should have been fetched, not committed ---
# NOTE: -not -path prunes DESCENT, it does not filter the matched directory itself,
# so './Vendor/*' never matches './Vendor'. Exclude the build-output roots by name.
VENDOR=$(find . -maxdepth 3 -type d \( \
    -name 'melonds-lua' -o -name 'lua-5.4*' -o -name 'mgba' -o -name 'melonDS-android' \
    -o -name 'Vendor' \) \
    -not -path './.git' -not -path './.git/*' \
    -not -path './frontend' -not -path './frontend/*' \
    -not -path './app/mgba' -not -path './app/mgba/*' \
    -not -path './Vendor' -not -path './Vendor/*' \
    -not -path './.build' -not -path './.build/*' \
    -not -path './xtool' -not -path './xtool/*' \
    -not -path './xtool-sim' -not -path './xtool-sim/*' 2>/dev/null)
if [ -n "$VENDOR" ]; then
  echo "!! VENDORED EMULATOR SOURCE / BUILD OUTPUT FOUND:"
  echo "$VENDOR"
  fail=1
else
  echo "   no vendored emulator source"
fi

# --- compiled objects / archives / packages ---
BUILD=$(find . -type f \( -iname '*.o' -o -iname '*.a' -o -iname '*.so' -o -iname '*.apk' \
    -o -iname '*.ipa' \) "${PRUNE[@]}" 2>/dev/null | head -20)
if [ -n "$BUILD" ]; then
  echo "!! BUILD ARTIFACTS FOUND (should be gitignored):"
  echo "$BUILD"
  fail=1
else
  echo "   no object/archive/package files"
fi

# --- large files: a proxy for "something got in that shouldn't have" ---
BIG=$(find . -type f -size +10M "${PRUNE[@]}" 2>/dev/null | head -10)
if [ -n "$BIG" ]; then
  echo "!! FILES OVER 10 MB (verify each is meant to ship):"
  echo "$BIG"
  fail=1
else
  echo "   no files over 10 MB"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "PASS — nothing that looks like game data or emulator build output."
else
  echo "FAIL — remove the files above before publishing."
fi
exit "$fail"
