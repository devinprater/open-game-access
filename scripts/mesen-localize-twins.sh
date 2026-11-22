#!/usr/bin/env bash
# mesen-localize-twins.sh — make Mesen's vendored twin libraries coexist with
# melonDS's and PPSSPP's copies in one link.
#
# ⛔ THE PROBLEM. Mesen vendors SEVERAL of the same third-party libraries as melonDS
# and PPSSPP, and every one of them is a strong twin in the link:
#
#     SevenZip -- Mesen's SevenZip/        vs PPSSPP's ext/lzma-sdk/  (and mGBA's lzma)
#     xBRZ     -- Mesen's Utilities/xBRZ/  vs PPSSPP's ext/xbrz/
#     blip_buf -- Mesen's Utilities/Audio/ vs melonDS's src/blip-buf/
#
# ld64.lld / GNU ld both die on the strong twins ("multiple definition of blip_end_frame").
# Neither "drop Mesen's whole copy" nor "keep all of it" works, because each side is the only
# source of something the other needs. THE TWINS ARE ALSO NOT IDENTICAL SOURCES (measured:
# Mesen's Utilities/xBRZ/xbrz.cpp differs from PPSSPP's by ~1800 diff lines, the 7-Zip pair by
# 218), so "pick a winner" is not a fix either.
#
# ✅ THE FIX: keep every other object bound to its own copy, and make the MESEN side use its
# copies privately via objcopy --localize-symbol. Intra-object references still resolve; only
# the exported names disappear, so the linker sees one global each. Nothing is edited, nothing
# is duplicated in source, and the pinned trees stay pinned.
#
# ⛔ THE OVERLAP IS COMPUTED, NEVER LISTED. A hand-written symbol list was written from the
# first link error and was immediately stale: naming 7-Zip but not blip_buf is exactly how the
# "duplicate global text symbols" gate in verify-sim-app.sh came to fail on _blip_new and
# xbrz::scale. Ask the objects which symbols BOTH sides define, so a new vendored twin is
# covered the moment it lands.
#
# ⛔ THIS RAN IN build-sim.sh AND NOT HERE, WHICH IS WHY THE HOST HARNESS COULD NOT LINK. The
# host build called a 7-Zip-ONLY dedupe (mesen-dedupe-7z.sh) while the sim build had grown the
# generalised form; Mesen's blip_buf vs melonDS's blip_buf was therefore an unresolved strong
# twin in Vendor/hostobj, and every harness died at the LINK. One definition, called by both.
#
# ⛔ IT MUST RUN AFTER THE COMPILE LOOP, EVERY TIME. A fresh compile rewrites the objects and
# undoes the localization, so it belongs where the objects are produced -- not in a one-off
# fixup someone has to remember. Re-running on already-localized objects is a no-op: the
# computed overlap is empty, so objcopy is never invoked.
#
# ⛔ TWO SYMBOL CLASSES IN THE OVERLAP ARE NOT TWINS AND MUST NOT BE LOCALIZED.
# The computed overlap also catches C++ vague-linkage entities that BOTH sides emit
# from the same headers (libstdc++ shared_ptr internals, typeinfos, vtables), and
# the linker already coalesces those itself:
#
#   * STB_GNU_UNIQUE definitions (e.g. _Sp_make_shared_tag's __tag, which g++
#     emits for header-defined statics): GNU objcopy CANNOT localize them at
#     all -- --localize-symbol exits 0 and changes nothing, so the prove-it
#     gate below failed a from-scratch host build on exactly the 17 objects
#     carrying __tag. It does not matter: STB_GNU_UNIQUE exists precisely so
#     that every reference in a link binds to one instance, duplicates
#     included (measured: two objects defining it link cleanly).
#   * Definitions living in COMDAT group sections (typeinfos like
#     _ZTI13ISerializable, _Sp_counted_base's cold release path): group dedup
#     keeps exactly one copy at link time, so they need no localization
#     either -- and localizing one is what BREAKS the link. A localized group
#     member can no longer be redirected to the kept copy, so the object's
#     own references die with "defined in discarded section" (measured on
#     the SNES proof link, SmsCart/ArmV3Cpu/DummySnesCpu/PceTurboTap).
#
# So the localize list and the gate both subtract, per object, the symbols
# that object defines with UNIQUE binding or inside a group section.
# readelf reports both; where readelf is absent or the objects are not ELF
# (the Mach-O sim build, which has neither class), the subtraction is empty
# and this script behaves exactly as before.
#
# Usage: mesen-localize-twins.sh <obj-dir> [llvm-nm] [llvm-objcopy]
#   <obj-dir>      directory of .o files; Mesen objects are the ones named mesen*.o
#   llvm-nm        optional; defaults to llvm-nm, else nm
#   llvm-objcopy   optional; defaults to llvm-objcopy, else objcopy
set -uo pipefail

OBJ="${1:?usage: mesen-localize-twins.sh <obj-dir> [llvm-nm] [llvm-objcopy]}"
[ -d "$OBJ" ] || { echo "!! no object directory at $OBJ" >&2; exit 1; }

# ⛔ LLVM BINUTILS IF AVAILABLE, BUT NOT REQUIRED FOR GNU. build-sim.sh passes the LLVM pair
# because GNU nm cannot read Mach-O; the host build has ELF objects, where GNU nm/objcopy are
# correct and always present. Do not silently fall back to nothing on a host that has neither.
NM="${2:-}"
[ -n "$NM" ] || NM="$(command -v llvm-nm || command -v nm || true)"
OBJCOPY="${3:-}"
[ -n "$OBJCOPY" ] || OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"

[ -n "$NM" ] || { echo "!! no nm: the twin overlap cannot be computed" >&2; exit 1; }
[ -n "$OBJCOPY" ] || { echo "!! no objcopy: the Mesen twin symbols cannot be resolved" >&2; exit 1; }

# readelf only recognises the linker-coalesced symbol classes (see
# coalesced_symbols below). It ships in the same binutils package as nm and
# objcopy; its absence means no exclusions, never a silent pass -- the gate
# below would fail on the un-localizable symbols exactly as it did before.
READELF="$(command -v readelf || true)"

# ⛔ PERFORMANCE. A `grep -qx` PER SYMBOL PER OBJECT is ~700 Mesen objects x dozens of symbols
# and grinds for 15+ minutes. Build the shared set ONCE, intersect with `comm -12` (one process
# per object), and hand objcopy the WHOLE collide list at once with --localize-symbols=<file>
# instead of one flag per symbol.
SHARED="$OBJ/.twin-shared"
MINE="$OBJ/.twin-mine"
TOLOC="$OBJ/.twin-toloc"
EXCLONE="$OBJ/.twin-excl-one"
# ⛔ THE COALESCED-SYMBOL CACHE IS PER-RUN, NEVER KEPT. It describes the objects
# as compiled; a later run over recompiled objects must recompute it, so it is
# created empty here and removed by the same trap as the other scratch files.
EXCLDIR="$OBJ/.twin-excl"
rm -rf "$EXCLDIR"
mkdir -p "$EXCLDIR"
trap 'rm -f "$SHARED" "$MINE" "$TOLOC" "$EXCLONE"; rm -rf "$EXCLDIR"' EXIT

# coalesced_symbols <object> — print the symbols <object> defines that the
# linker coalesces on its own (see the header): STB_GNU_UNIQUE definitions
# and definitions in COMDAT group sections. Computed once per object per
# run and cached; empty for non-ELF objects or when readelf is unavailable.
coalesced_symbols() {
  local o="$1" cache="$EXCLDIR/$(basename "$1")"
  if [ -f "$cache" ]; then cat "$cache"; return 0; fi
  : > "$cache"
  [ -n "$READELF" ] || return 0
  "$READELF" -h "$o" >/dev/null 2>&1 || return 0
  local grp="$cache.grp" idx="$cache.idx"
  "$READELF" -gW "$o" 2>/dev/null | awk '
    /COMDAT group section/ { g=1; next }
    /^$/ { g=0 }
    g && /\[ *[0-9]+\]/ { print $NF }' | sort -u > "$grp"
  "$READELF" -SW "$o" 2>/dev/null \
    | sed -n 's/^ *\[ *\([0-9][0-9]*\)\] *\([^ ]*\).*/\1 \2/p' > "$idx"
  # The group/section maps go through FILES, not awk -v: a large object
  # carries more section names than a single argument may hold (measured:
  # "Argument list too long" on the PCE objects).
  "$READELF" -sW "$o" 2>/dev/null | awk -v idxf="$idx" -v grpf="$grp" '
    BEGIN { while ((getline l < idxf) > 0) { split(l, p, " "); sec[p[1]] = p[2] }
            while ((getline l < grpf) > 0) inseg[l] = 1 }
    $4 == "SECTION" || $4 == "FILE" { next }
    $5 == "UNIQUE" { n = $8; sub(/@.*/, "", n); print n; next }
    $7 ~ /^[0-9]+$/ && (sec[$7] in inseg) { n = $8; sub(/@.*/, "", n); print n }
  ' | sort -u > "$cache.tmp"
  mv "$cache.tmp" "$cache"
  rm -f "$grp" "$idx"
  cat "$cache"
}

find "$OBJ" -maxdepth 1 -name '*.o' ! -name 'mesen*' -print0 2>/dev/null \
  | xargs -0 -r "$NM" --defined-only --extern-only -A 2>/dev/null \
  | awk 'NF {print $NF}' | sort -u > "$SHARED"
echo "== symbols defined by non-Mesen objects: $(wc -l < "$SHARED")"

count=0
skipped=0
for o in "$OBJ"/mesen*.o; do
  [ -f "$o" ] || continue
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF {print $3}' \
    | sort -u | comm -12 - "$SHARED" > "$MINE"
  coalesced_symbols "$o" > "$EXCLONE"
  skipped=$((skipped + $(comm -12 "$MINE" "$EXCLONE" | wc -l)))
  comm -23 "$MINE" "$EXCLONE" > "$TOLOC"
  if [ -s "$TOLOC" ]; then
    "$OBJCOPY" --localize-symbols="$TOLOC" "$o" 2>/dev/null \
      || { echo "!! could not localize twins in $(basename "$o")" >&2; exit 1; }
    count=$((count + $(wc -l < "$TOLOC")))
  fi
done
echo "== localised $count Mesen twin symbol(s) shared with melonDS/PPSSPP"
echo "== left $skipped linker-coalesced symbol(s) global (GNU_UNIQUE / COMDAT group members)"

# ⛔ PROVE IT. objcopy --localize-symbol is a SILENT no-op for a symbol it cannot find, so a
# green exit code means nothing. Recompute the overlap and require it to be empty -- the link
# would fail otherwise, and a build that only reports success is how this class of bug ships.
# The recomputation subtracts the same linker-coalesced classes as the pass above (they stay
# global on purpose); what remains must be empty.
left=0
for o in "$OBJ"/mesen*.o; do
  [ -f "$o" ] || continue
  "$NM" --defined-only --extern-only "$o" 2>/dev/null | awk 'NF {print $3}' \
    | sort -u | comm -12 - "$SHARED" > "$MINE"
  coalesced_symbols "$o" > "$EXCLONE"
  comm -23 "$MINE" "$EXCLONE" > "$TOLOC"
  if [ -s "$TOLOC" ]; then
    while read -r s; do echo "!! still duplicated: $s in $(basename "$o")" >&2; done < "$TOLOC"
    left=$((left + $(wc -l < "$TOLOC")))
  fi
done
[ "$left" -eq 0 ] || { echo "!! $left twin symbol(s) still duplicated; the link would fail" >&2; exit 1; }
echo "localized Mesen twins in $(basename "$OBJ"): no global the linker cannot coalesce is defined twice"
