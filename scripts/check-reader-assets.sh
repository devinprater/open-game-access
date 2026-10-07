#!/usr/bin/env bash
# check-reader-assets.sh — fail if the reader set is duplicated or incomplete.
#
# Two defects this guards, both measured on the shipped v0.6.1 APK:
#  1. a hand-copied SECOND reader tree under app/src/main/assets/ drifted five
#     days behind the canonical one and shipped without oga_bootstrap.lua or
#     mgba_compat.lua, i.e. without the loader and the mGBA API shim;
#  2. the canonical tree was missing sounds/ entirely (33 positional-audio WAVs
#     the reader names with `scriptpath .. "sounds\..."`), so the iOS bundle
#     never carried them.
#
# Run from the repo root. Exits non-zero with a specific reason.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CANON="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
DUP="$ROOT/app/src/main/assets/lua/gb"

# Files a staged reader set cannot work without. oga_bootstrap.lua calls itself
# "the ONE file to load"; mgba_compat.lua is the mGBA<-BizHawk API shim.
REQUIRED="oga_bootstrap.lua mgba_compat.lua gb.lua gba.lua pokemon.lua"
# Positional-audio files the readers name. At least one per directory proves the
# tree is wired for sound at all.
REQUIRED_SOUNDS="sounds/gba/s_grass.wav sounds/common/s_water.wav sounds/gb/menusel.wav"
fail=0

echo "== reader assets: $CANON"
[ -d "$CANON" ] || { echo "!! the canonical reader tree is missing" >&2; exit 1; }

for f in $REQUIRED; do
  if [ -f "$CANON/$f" ]; then
    echo "  ok:      $f"
  else
    echo "  MISSING: $f" >&2
    fail=1
  fi
done

for f in $REQUIRED_SOUNDS; do
  if [ -f "$CANON/$f" ]; then
    echo "  ok:      $f"
  else
    echo "  MISSING: $f (the readers name it and nothing else ships it)" >&2
    fail=1
  fi
done

# ⛔ The duplicate tree is the actual bug. If it reappears, the reader set is
# being hand-copied again and will drift again.
if [ -d "$DUP" ]; then
  echo "!! a SECOND reader tree exists at app/src/main/assets/lua/gb/" >&2
  echo "   It is staged at build time from the canonical tree (see" >&2
  echo "   scripts/android-apply-overlay.sh, step 1b). A tracked copy here will" >&2
  echo "   drift and ship stale -- delete it and put changes in the canonical tree." >&2
  fail=1
else
  echo "  ok:      no duplicate reader tree (assets are staged at build time)"
fi

# The host dev tools exist to be run on a workstation; they must not be part of
# what a bundle ships.
for f in host-sim-rom.lua oga_capture.lua; do
  if [ -f "$CANON/$f" ]; then echo "  note:    $f is present (excluded from staging, correct)"; fi
done

[ "$fail" -eq 0 ] || { echo "!! reader assets FAILED" >&2; exit 1; }
echo "PASS: the reader set is single-sourced, complete, and sound-wired."
