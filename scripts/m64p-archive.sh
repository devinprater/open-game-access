#!/usr/bin/env bash
# m64p-archive.sh — turn the Android build's Mupen64Plus-Next objects into one
# isolated static archive.
#
# WHY THIS EXISTS. The iOS builds compile the core into a flat object folder,
# name every object m64p_*.o and run scripts/m64p-isolate.sh over that folder.
# Android builds with CMake, which keeps the objects in its own tree under
# names like libretro.c.o — and several upstream files share a base name, so
# the objects cannot simply be unpacked from an archive into one folder (the
# second libretro.c.o would overwrite the first). This script gives the
# isolate pass the shape it already knows: each object is copied to a numbered
# m64p_NNNN.o in a fresh folder, the folder is renamed (oga_m64p_ prefix, the
# same pass and the same rules as iOS), and the renamed objects are packed
# into the archive the frontend links. The CMake subproject's POST step is
# the only caller (scripts/n64-android-cmake.py writes it).
#
# Usage: m64p-archive.sh <out.a> <ar> <nm> <objcopy> <m64p-isolate.sh> <obj>...
set -uo pipefail

OUT="${1:?usage: m64p-archive.sh <out.a> <ar> <nm> <objcopy> <isolate> <obj>...}"
AR="${2:?no ar}"; NM="${3:?no nm}"; OBJCOPY="${4:?no objcopy}"; ISOLATE="${5:?no isolate script}"
shift 5
[ "$#" -gt 0 ] || { echo "!! m64p-archive: no objects given" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/m64p-archive.XXXXXX")" || exit 1
trap 'rm -rf "$WORK"' EXIT

i=0
for o in "$@"; do
  [ -f "$o" ] || { echo "!! m64p-archive: missing object $o" >&2; exit 1; }
  cp "$o" "$WORK/$(printf 'm64p_%04d.o' "$i")" || exit 1
  i=$((i + 1))
done

bash "$ISOLATE" "$WORK" "$NM" "$OBJCOPY" || { echo "!! m64p-archive: the isolate pass failed" >&2; exit 1; }

rm -f "$OUT"
"$AR" rcs "$OUT" "$WORK"/m64p_*.o || { echo "!! m64p-archive: $AR could not write $OUT" >&2; exit 1; }
echo "m64p-archive: $i objects renamed (oga_m64p_) into $(basename "$OUT")"
