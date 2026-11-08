#!/usr/bin/env bash
# android-mesen-7z-dedupe.sh — make Mesen's 7zStream.o coexist with the
# other vendored 7-Zip copies in the Android link.
#
# The Android twin of scripts/mesen-dedupe-7z.sh (the iOS/host one);
# read that file's header for the whole problem. The differences are
# only about WHERE the objects live:
#
#   - iOS links ONE other copy (mGBA's). Android links TWO: mGBA's
#     vendored lzma (the oga-mgba subproject) and PPSSPP's vendored
#     lzma-sdk (the oga-ppsspp subproject). The overlap is therefore
#     computed against the UNION of every other 7z object in the build
#     tree, not against one named file.
#   - The objects are CMake's, found by glob under the build root
#     rather than by the host build's tag names.
#   - nm/objcopy come from the NDK toolchain (CMake hands them over
#     through OGA_NM / OGA_OBJCOPY; the llvm-* spellings are the
#     fallback).
#
# WHAT IT DOES, unchanged from the host script: compute the symbols
# BOTH Mesen's 7zStream object and some other 7z object define as
# extern, and localize exactly those in MESEN's copy — except
# LookToRead_CreateVTable / LookToRead_Init, which Mesen's copy is the
# only source of and which SZReader needs exported; those are verified
# still exported at the end. Idempotent: a re-run finds no extern
# overlap, verifies the LookToRead pair, and succeeds.
#
# Usage: android-mesen-7z-dedupe.sh <cmake-build-root>
# Env:   OGA_NM, OGA_OBJCOPY (tool paths; llvm-nm / llvm-objcopy fallback)
set -uo pipefail

BUILD_ROOT="${1:?usage: android-mesen-7z-dedupe.sh <cmake-build-root>}"
[ -d "$BUILD_ROOT" ] || { echo "!! no build root at $BUILD_ROOT" >&2; exit 1; }

NM="${OGA_NM:-llvm-nm}"
OBJCOPY="${OGA_OBJCOPY:-llvm-objcopy}"
command -v "$NM" >/dev/null 2>&1 || { echo "!! nm not found ($NM); the 7-Zip duplicates cannot be resolved" >&2; exit 1; }
command -v "$OBJCOPY" >/dev/null 2>&1 || { echo "!! objcopy not found ($OBJCOPY); the 7-Zip duplicates cannot be resolved" >&2; exit 1; }

MESEN_OBJ="$(find "$BUILD_ROOT" -path '*oga-mesen-sevenzip*' -name '7zStream.c.o' 2>/dev/null | head -1)"
[ -n "$MESEN_OBJ" ] && [ -f "$MESEN_OBJ" ] || {
  echo "!! Mesen's 7zStream object not found under $BUILD_ROOT (oga-mesen-sevenzip not built?)" >&2; exit 1; }

# Every OTHER 7z object in the build: mGBA's lzma set and PPSSPP's
# lzma-sdk set, whatever directories CMake put them in.
OTHER_SYMS_FILE="$(mktemp)"
trap 'rm -f "$OTHER_SYMS_FILE"' EXIT
find "$BUILD_ROOT" -name '7z*.o' 2>/dev/null | while IFS= read -r obj; do
  [ "$obj" = "$MESEN_OBJ" ] && continue
  "$NM" --defined-only --extern-only "$obj" 2>/dev/null | awk '{print $NF}'
done | sort -u > "$OTHER_SYMS_FILE"

MESEN_SYMS="$("$NM" --defined-only --extern-only "$MESEN_OBJ" 2>/dev/null | awk '{print $NF}' | sort -u)"

# The overlap, minus the pair Mesen must keep exporting.
SHARED="$(comm -12 <(printf '%s\n' "$MESEN_SYMS") "$OTHER_SYMS_FILE" \
  | grep -vx -e LookToRead_CreateVTable -e LookToRead_Init || true)"

verify_looktoread() {
  for s in LookToRead_CreateVTable LookToRead_Init; do
    "$NM" --defined-only --extern-only "$MESEN_OBJ" 2>/dev/null | grep -q " $s\$" \
      || { echo "!! $MESEN_OBJ no longer exports $s -- SZReader will not link" >&2; exit 1; }
  done
}

if [ -z "$SHARED" ]; then
  # Nothing to localize. That is only OK if the LookToRead pair is
  # still exported (the dedupe ran before, or there never was an
  # overlap) — verify rather than trust.
  verify_looktoread
  echo "  dedupe: no extern overlap to localize (already applied, or none exists)"
  exit 0
fi

count=0
for s in $SHARED; do
  if "$OBJCOPY" --localize-symbol="$s" "$MESEN_OBJ" "$MESEN_OBJ.localized" 2>/dev/null; then
    mv "$MESEN_OBJ.localized" "$MESEN_OBJ"; count=$((count+1))
  else
    rm -f "$MESEN_OBJ.localized"
    echo "  dedupe: could not localize $s (already local?) -- continuing" >&2
  fi
done

verify_looktoread
echo "deduped $(basename "$MESEN_OBJ"): localized $count shared symbol(s); LookToRead_* still exported"
