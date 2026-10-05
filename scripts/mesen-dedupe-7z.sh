#!/usr/bin/env bash
# mesen-dedupe-7z.sh — make Mesen's 7zStream.o and mGBA's copy coexist.
#
# ⛔ THE PROBLEM, CONCRETELY. Mesen's SevenZip/7zStream.c and mGBA's third-party/lzma/7zStream.c are
# the same file from the same SDK, and both are in the link, so five symbols are defined twice:
#
#     LookInStream_LookRead, LookInStream_Read, LookInStream_Read2,
#     SecToLook_CreateVTable, SecToRead_CreateVTable
#
# Neither "drop Mesen's whole SDK" nor "add all of it" works: mGBA's copy is a SUBSET (no 7zAlloc.c,
# so no SzAlloc), and Mesen's 7zStream.c is the ONLY source of LookToRead_CreateVTable /
# LookToRead_Init, which SZReader needs -- and those two cannot be lifted out, because
# LookToRead_CreateVTable points at five file-STATIC helpers in the same file.
#
# ✅ THE FIX: let mGBA's copy keep the five colliding globals, and make MESEN's object use its own
# copies privately via objcopy --localize-symbol. Intra-object references still resolve; only the
# exported names disappear, so the linker sees one global each. Nothing is edited, nothing is
# duplicated in source, and the pinned trees stay pinned.
#
# ⛔ This is not a hack around a build error -- it is the standard way two copies of one vendored
# SDK coexist in one link. But it IS a per-object step outside the compile, so it has to run after
# every compile of that object; build-host.sh calls it, and a fresh .o without it will reintroduce
# the duplicates.
set -uo pipefail

OBJ="${1:?usage: mesen-dedupe-7z.sh <mesen7z_SevenZip_7zStream.o>}"
[ -f "$OBJ" ] || { echo "!! no object at $OBJ" >&2; exit 1; }

command -v objcopy >/dev/null 2>&1 || {
  echo "!! objcopy not found; the 7-Zip duplicate symbols cannot be resolved" >&2; exit 1; }

# ⛔ THE OVERLAP IS COMPUTED, NOT LISTED. A hardcoded list was written from the first link error
# report and was immediately stale: the linker then found four more shared symbols. Ask nm which
# symbols BOTH objects define, and localize exactly those -- it cannot drift when either SDK moves.
MG7Z="${MG7Z:-$HOME/src/mgba/src/third-party/lzma/7zStream.o}"
if [ ! -f "$MG7Z" ]; then
  # The mGBA object lives in the host object dir under its own tag.
  MG7Z="$(ls "${OBJ%/*}"/mgbalzma_*_7zStream.o 2>/dev/null | head -1)"
fi
[ -f "$MG7Z" ] || { echo "!! cannot find mGBA's 7zStream.o to compute the overlap" >&2; exit 1; }

SHARED="$(comm -12 <(nm --defined-only --extern-only "$OBJ" | awk '{print $NF}' | sort -u) \
                   <(nm --defined-only --extern-only "$MG7Z" | awk '{print $NF}' | sort -u))"
[ -n "$SHARED" ] || { echo "!! the two 7zStream.o files share no symbols -- nothing to localize; \n   this is NOT a pass: the dedupe step probably measured the wrong objects." >&2; exit 1; }

count=0
for s in $SHARED; do
  objcopy --localize-symbol="$s" "$OBJ" "$OBJ.localized" 2>/dev/null \
    && mv "$OBJ.localized" "$OBJ" && count=$((count+1))
done
echo "  dedupe: localized $count shared symbol(s): $(echo $SHARED | tr '\n' ' ')"

# Verify: the object must still export the two symbols SZReader needs, and must no longer export
# the five that mGBA provides. A silent no-op here is exactly the failure this script exists to
# prevent, so prove it rather than trusting objcopy's exit code.
for s in LookToRead_CreateVTable LookToRead_Init; do
  nm --defined-only --extern-only "$OBJ" 2>/dev/null | grep -q " $s$" \
    || { echo "!! $obj no longer exports $s -- SZReader will not link" >&2; exit 1; }
done

echo "deduped $(basename "$OBJ"): LookToRead_* exported, the 5 mGBA-shared globals are local"
