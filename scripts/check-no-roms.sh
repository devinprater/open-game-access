#!/usr/bin/env bash
# check-no-roms.sh — refuse to publish game data.
#
# ⛔ THIS IS THE GUARD THAT MATTERS. ROMs, BIOS/firmware dumps and save files are
# copyrighted; this project's premise is that players supply their own. A single
# stray `git add .` in a directory that has a ROM two levels up would be a real
# legal problem, so the check looks for game data AND for the emulator build
# output that should never be committed, and exits non-zero on either.
#
# Run by CI before upload and by scripts/stage-repo.sh before staging.
set -uo pipefail
ROOT="${1:-.}"
cd "$ROOT" || exit 2

fail=0

echo "== scanning for game data and build output under $ROOT"

# --- game data: the hard stop ---
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
VENDOR=$(find . -maxdepth 3 -type d \( \
    -name 'melonds-lua' -o -name 'lua-5.4*' -o -name 'mgba' -o -name 'melonDS-android' \
    -o -name 'Vendor' \) -not -path './.git/*' 2>/dev/null)
if [ -n "$VENDOR" ]; then
  echo "!! VENDORED EMULATOR SOURCE / BUILD OUTPUT FOUND:"
  echo "$VENDOR"
  fail=1
else
  echo "   no vendored emulator source"
fi

# --- compiled objects / archives / packages ---
BUILD=$(find . -type f \( -iname '*.o' -o -iname '*.a' -o -iname '*.so' -o -iname '*.apk' \
    -o -iname '*.ipa' \) -not -path './.git/*' 2>/dev/null | head -20)
if [ -n "$BUILD" ]; then
  echo "!! BUILD ARTIFACTS FOUND (should be gitignored):"
  echo "$BUILD"
  fail=1
else
  echo "   no object/archive/package files"
fi

# --- large files: a proxy for "something got in that shouldn't have" ---
BIG=$(find . -type f -size +10M -not -path './.git/*' 2>/dev/null | head -10)
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
