#!/usr/bin/env bash
# gpgx-localize-twins.sh — make Genesis Plus GX's vendored twin library coexist
# with melonDS's and Mesen's copies in one link.
#
# ⛔ THE PROBLEM. Genesis Plus GX vendors blip_buf (core/sound/blip_buf.c,
# Blargg's buffer) — the same library melonDS vendors (src/blip-buf/) and
# Mesen vendors (Utilities/Audio/). All three export the same strong symbols
# (blip_new, blip_end_frame, ...), and the linker dies on the duplicates.
# Genesis Plus GX's own entry points (load_rom, system_init, ...) have no
# twin anywhere and are NOT touched: only symbols some OTHER object also
# defines are localized, and only in the gpgx*.o objects.
#
# ✅ THE FIX AND ITS RULES ARE THE MESEN ONE'S, VERBATIM IN SHAPE: keep every
# object bound to its own copy by localizing the overlap on ONE side
# (scripts/mesen-localize-twins.sh is the model; this is its sibling, not a
# copy, because the object glob and the vendored set differ). The overlap is
# COMPUTED from nm, never listed — a hand-written list goes stale within one
# upstream bump.
#
# ⛔ IT MUST RUN AFTER THE COMPILE LOOP, EVERY TIME, AND AFTER THE MESEN PASS.
# A fresh compile rewrites the objects and undoes the localization. Each pass
# localizes its own side's symbols that some other object still defines
# globally at its turn: the Mesen pass localizes Mesen's blip_buf (melonDS
# and GPGX still define it), then this pass localizes GPGX's (melonDS still
# does) — so melonDS keeps the only global copy, every object binds to its
# own side's implementation, and the re-prove below requires the final state
# to have no gpgx global duplicated anywhere.
#
# Usage: gpgx-localize-twins.sh <obj-dir> [llvm-nm] [llvm-objcopy]
#   <obj-dir>      directory of .o files; GPGX objects are the ones named gpgx*.o
#   llvm-nm        optional; defaults to llvm-nm, else nm
#   llvm-objcopy   optional; defaults to llvm-objcopy, else objcopy
set -uo pipefail

OBJ="${1:?usage: gpgx-localize-twins.sh <obj-dir> [llvm-nm] [llvm-objcopy]}"
[ -d "$OBJ" ] || { echo "!! no object directory at $OBJ" >&2; exit 1; }

NM="${2:-}"
[ -n "$NM" ] || NM="$(command -v llvm-nm || command -v nm || true)"
OBJCOPY="${3:-}"
[ -n "$OBJCOPY" ] || OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"

[ -n "$NM" ] || { echo "!! no nm: the twin overlap cannot be computed" >&2; exit 1; }
[ -n "$OBJCOPY" ] || { echo "!! no objcopy: the GPGX twin symbols cannot be resolved" >&2; exit 1; }

SHARED="$OBJ/.gpgx-twin-shared"
MINE="$OBJ/.gpgx-twin-mine"
trap 'rm -f "$SHARED" "$MINE"' EXIT

find "$OBJ" -maxdepth 1 -name '*.o' ! -name 'gpgx*' -print0 2>/dev/null \
  | xargs -0 -r "$NM" --defined-only --extern-only -A 2>/dev/null \
  | awk 'NF {print $NF}' | sort -u > "$SHARED"
echo "== symbols defined by non-GPGX objects: $(wc -l < "$SHARED")"

count=0
for o in "$OBJ"/gpgx*.o; do
  [ -f "$o" ] || continue
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF {print $3}' \
    | sort -u | comm -12 - "$SHARED" > "$MINE"
  if [ -s "$MINE" ]; then
    "$OBJCOPY" --localize-symbols="$MINE" "$o" 2>/dev/null \
      || { echo "!! could not localize twins in $(basename "$o")" >&2; exit 1; }
    count=$((count + $(wc -l < "$MINE")))
  fi
done
echo "== localised $count GPGX twin symbol(s) shared with melonDS/Mesen/PPSSPP"

# ⛔ PROVE IT (the Mesen rule): objcopy failures are silent, so recompute the
# overlap and require it empty.
left=0
for o in "$OBJ"/gpgx*.o; do
  [ -f "$o" ] || continue
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF {print $3}' \
    | sort -u | comm -12 - "$SHARED" > "$MINE"
  if [ -s "$MINE" ]; then
    while read -r s; do echo "!! still duplicated: $s in $(basename "$o")" >&2; done < "$MINE"
    left=$((left + $(wc -l < "$MINE")))
  fi
done
[ "$left" -eq 0 ] || { echo "!! $left twin symbol(s) still duplicated; the link would fail" >&2; exit 1; }
echo "localized GPGX twins in $(basename "$OBJ"): no global is defined twice"
