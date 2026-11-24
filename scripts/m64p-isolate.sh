#!/usr/bin/env bash
# m64p-isolate.sh — make Mupen64Plus-Next unable to collide with anything else
# in the app's one big link.
#
# WHY RENAME EVERYTHING, NOT JUST THE OVERLAP. The other cores' passes
# (mesen-localize-twins.sh, gpgx-localize-twins.sh) compute the symbols two
# trees BOTH define and localize one side. That works for a vendored twin
# (blip_buf is blip_buf). Mupen64Plus-Next is different in kind: it is a
# libretro core, so it defines hundreds of generic globals (main_run, g_dev,
# md5_init, the whole libretro-common set: path_*, string_*, filestream_*,
# retro_*), and the collision set would change with every other core's pin
# and with every future libretro core (PCSX ReARMed for PS1 is one). And
# localizing cannot work here at all: the core's own objects cross-reference
# those names, so hiding a definition would bind its callers to the OTHER
# tree's function of the same name (the gpgx `config` lesson).
#
# So the m64p side is RENAMED, wholesale: every strong global symbol defined by
# any m64p_*.o gets the prefix oga_m64p_, applied to the definition AND every
# reference in every m64p_*.o in one pass (objcopy --redefine-syms). The core
# then binds only to itself, nothing else can bind to it, and the glue
# (Core/n64_core.cpp, which is NOT an m64p_ object) calls it as
# oga_m64p_retro_*. Weak definitions (C++ inline/template instantiations from
# parallel_al.cpp) are left alone: duplicate weak symbols do not collide.
# Undefined symbols (libc, libc++, pthread) are left alone: they are not ours.
#
# ⛔ IT MUST RUN AFTER THE COMPILE LOOP, EVERY TIME. A fresh compile rewrites an
# object with the original names. Running it twice is harmless: the second pass
# finds every strong global already prefixed and changes nothing.
#
# Usage: m64p-isolate.sh <obj-dir> [nm] [objcopy]
#   <obj-dir>  directory holding the m64p_*.o objects
#   nm         optional; defaults to llvm-nm, else nm
#   objcopy    optional; defaults to llvm-objcopy, else objcopy
set -uo pipefail

OBJ="${1:?usage: m64p-isolate.sh <obj-dir> [nm] [objcopy]}"
[ -d "$OBJ" ] || { echo "!! no object directory at $OBJ" >&2; exit 1; }
NM="${2:-}"
[ -n "$NM" ] || NM="$(command -v llvm-nm || command -v nm || true)"
OBJCOPY="${3:-}"
[ -n "$OBJCOPY" ] || OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"
[ -n "$NM" ] || { echo "!! no nm: the core's symbols cannot be listed" >&2; exit 1; }
[ -n "$OBJCOPY" ] || { echo "!! no objcopy: the core's symbols cannot be renamed" >&2; exit 1; }

shopt -s nullglob
objs=("$OBJ"/m64p_*.o)
[ "${#objs[@]}" -gt 0 ] || { echo "!! no m64p_*.o objects in $OBJ" >&2; exit 1; }

PREFIX="oga_m64p_"
MAP="$OBJ/.m64p-rename"
trap 'rm -f "$MAP" "$MAP.syms"' EXIT

# Strong global definitions, as nm prints them: type letters T D B R S C (upper
# case = global). V/W (weak) and U (undefined) are excluded on purpose.
# ⛔ SPELLING: Mach-O stores C names with a leading underscore (_main_run) and
# ELF does not. The prefix goes AFTER that underscore so the C-level name the
# glue declares (oga_m64p_retro_init) is the same on both formats.
for o in "${objs[@]}"; do
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF>=3 && $2 ~ /^[TDBRSC]$/ {print $3}'
done | sort -u > "$MAP.syms"

# ⛔ THE MAP IS BUILT FROM THE BASE NAMES, prefixed or not. The app builds
# cache objects, so a rebuild can recompile ONE m64p object (fresh, old names)
# beside the rest (already renamed). The fresh object's references to symbols
# the others define must be renamed too, but those definitions now carry the
# prefix -- so a name seen as oga_m64p_X still maps X -> oga_m64p_X. Renaming
# a name an object does not contain is a no-op, so this is safe on every
# object, renamed or not.
: > "$MAP"
while IFS= read -r s; do
  [ -n "$s" ] || continue
  case "$s" in
    "_$PREFIX"*) s="_${s#_$PREFIX}" ;;    # Mach-O, already renamed
    "$PREFIX"*)  s="${s#$PREFIX}" ;;      # ELF, already renamed
  esac
  case "$s" in
    _*) printf '%s _%s%s\n' "$s" "$PREFIX" "${s#_}" ;;
    *)  printf '%s %s%s\n' "$s" "$PREFIX" "$s" ;;
  esac
done < "$MAP.syms" | sort -u > "$MAP"

n=$(wc -l < "$MAP")
if [ "$n" -gt 0 ]; then
  for o in "${objs[@]}"; do
    "$OBJCOPY" --redefine-syms="$MAP" "$o" || { echo "!! objcopy failed on $o" >&2; exit 1; }
  done
fi
echo "== m64p-isolate: $n core symbols mapped to $PREFIX* across ${#objs[@]} objects"

# ---- re-prove: the outcome, not the intent ----
# 1. No strong global defined by the core is left unprefixed.
left=$(for o in "${objs[@]}"; do
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF>=3 && $2 ~ /^[TDBRSC]$/ {print $3}'
done | sed 's/^_//' | grep -v "^$PREFIX" | sort -u)
if [ -n "$left" ]; then
  echo "!! m64p-isolate: core globals still unprefixed:" >&2
  echo "$left" | head -20 >&2
  exit 1
fi
# 2. The entry points the glue calls exist under their new names.
for want in retro_init retro_deinit retro_load_game retro_unload_game retro_run \
            retro_set_environment retro_set_video_refresh retro_set_audio_sample \
            retro_set_audio_sample_batch retro_set_input_poll retro_set_input_state \
            retro_get_memory_data retro_get_memory_size retro_serialize_size \
            retro_serialize retro_unserialize retro_get_system_info retro_get_system_av_info \
            retro_reset retro_savestate_result; do
  found=0
  for o in "${objs[@]}"; do
    if "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk '{print $3}' | sed 's/^_//' | grep -qx "$PREFIX$want"; then
      found=1; break
    fi
  done
  [ "$found" -eq 1 ] || { echo "!! m64p-isolate: $PREFIX$want is not defined by any m64p object" >&2; exit 1; }
done
echo "== m64p-isolate: every core global carries the $PREFIX prefix; glue entry points present"
