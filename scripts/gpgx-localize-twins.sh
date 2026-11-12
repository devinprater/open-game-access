#!/usr/bin/env bash
# gpgx-localize-twins.sh — make Genesis Plus GX coexist with the other cores
# in one link. TWO distinct collisions, two distinct tools:
#
# 1. VENDORED TWINS (localize). GPGX vendors blip_buf (core/sound/blip_buf.c,
# Blargg's buffer) — the same library melonDS vendors (src/blip-buf/) and
# Mesen vendors (Utilities/Audio/). All three export the same strong symbols
# and the linker dies on the duplicates. The fix is the Mesen one
# (scripts/mesen-localize-twins.sh is the model): compute the overlap from nm
# and localize it on the GPGX side (objects named gpgx*.o). GPGX's own entry
# points (load_rom, system_init, ...) have no twin and stay global.
#
# 2. MANDATED GENERIC NAMES (redefine). The core's frontend contract FIXES
# two symbol names the glue must provide: `config` (the t_config global every
# core TU reads) and `crc32` (two core TUs call it undeclared; osd.h promises
# it). Both names already exist in this link — PPSSPP has a `config` of its
# own (Core/HLE/sceUsbCam) and mGBA vendors a `crc32` (src/util/crc32.c).
# ⛔ LOCALIZING CANNOT FIX THESE: localization hides a definition from OTHER
# objects, but GPGX's own TUs cross-reference both symbols, so hiding the
# glue's definition would bind those references to PPSSPP's config (a
# different struct entirely) or mGBA's crc32. Instead the GPGX side is
# RENAMED — objcopy --redefine-sym rewrites the definition in genesis_core.o
# and every reference in the gpgx*.o objects in one pass, keeping the GPGX
# side bound to its own symbols under names nothing else uses:
# config -> oga_gpgx_config, crc32 -> oga_gpgx_crc32. The list is NAMED, not
# computed, because it is exactly the frontend contract's fixed names; a new
# mandated name would be a contract change, not a silent overlap.
#
# ⛔ IT MUST RUN AFTER THE COMPILE LOOP, EVERY TIME, AND AFTER THE MESEN PASS.
# A fresh compile rewrites the objects and undoes both steps. Each pass
# localizes its own side's symbols that some other object still defines
# globally at its turn: the Mesen pass localizes Mesen's blip_buf (melonDS
# and GPGX still define it), then this pass localizes GPGX's (melonDS still
# does) — so melonDS keeps the only global copy, every object binds to its
# own side's implementation, and the re-prove below requires the final state
# to have no gpgx global duplicated anywhere.
#
# Usage: gpgx-localize-twins.sh <obj-dir> [llvm-nm] [llvm-objcopy]
#   <obj-dir>      directory of .o files; GPGX objects are gpgx*.o plus the
#                  glue object genesis_core.o (the shim symbols live there)
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
[ -n "$OBJCOPY" ] || { echo "!! no objcopy: the GPGX symbols cannot be resolved" >&2; exit 1; }

SHARED="$OBJ/.gpgx-twin-shared"
MINE="$OBJ/.gpgx-twin-mine"
trap 'rm -f "$SHARED" "$MINE"' EXIT

# ---- step 2 first: rename the mandated generic names on the GPGX side ----
# Before the overlap computation, so the renamed symbols never enter it.
# ⛔ SPELLING: Mach-O stores C symbols with a leading underscore (_config)
# and ELF does not, and objcopy matches the stored spelling — so BOTH
# spellings are passed; the one the object format does not use simply
# matches nothing. (First device CI run of this script failed its own
# re-prove because nm on Mach-O prints _oga_gpgx_config: the checks below
# normalise one leading underscore away before comparing.)
norm() { sed 's/^_//' ; }
RENAMES="config crc32"
renamed=0
for o in "$OBJ"/gpgx*.o "$OBJ"/genesis_core.o; do
  [ -f "$o" ] || continue
  args=""
  for r in $RENAMES; do
    args="$args --redefine-sym $r=oga_gpgx_$r --redefine-sym _$r=_oga_gpgx_$r"
  done
  # objcopy errors on a redefine for a symbol the object does not mention at
  # all are NOT fatal to us (most TUs mention neither); the re-prove below
  # is what guarantees the outcome, so a silent pass here is acceptable.
  "$OBJCOPY" $args "$o" 2>/dev/null || true
  renamed=$((renamed + 1))
done
echo "== renamed the frontend-contract symbols (config, crc32) in $renamed GPGX-side object(s)"

# ---- step 1: localize the computed vendored-twin overlap ----
find "$OBJ" -maxdepth 1 -name '*.o' ! -name 'gpgx*' ! -name 'genesis_core.o' -print0 2>/dev/null \
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

# ⛔ PROVE IT (the Mesen rule): objcopy failures are silent, so recompute and
# require (a) the twin overlap empty, and (b) no GPGX-side object still
# exporting the generic names, while the glue exports the renamed ones.
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

for o in "$OBJ"/gpgx*.o "$OBJ"/genesis_core.o; do
  [ -f "$o" ] || continue
  if "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF {print $3}' | norm | grep -qx -e config -e crc32; then
    echo "!! $(basename "$o") still exports a generic frontend-contract name (config/crc32)" >&2
    exit 1
  fi
done
if [ -f "$OBJ/genesis_core.o" ]; then
  "$NM" --defined-only --extern-only "$OBJ/genesis_core.o" 2>/dev/null | awk 'NF {print $3}' | norm | grep -qx "oga_gpgx_config" \
    || { echo "!! genesis_core.o does not export oga_gpgx_config after the rename" >&2; exit 1; }
  "$NM" --defined-only --extern-only "$OBJ/genesis_core.o" 2>/dev/null | awk 'NF {print $3}' | norm | grep -qx "oga_gpgx_crc32" \
    || { echo "!! genesis_core.o does not export oga_gpgx_crc32 after the rename" >&2; exit 1; }
fi
echo "localized + renamed GPGX symbols in $(basename "$OBJ"): no global is defined twice"
