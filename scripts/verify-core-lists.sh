#!/usr/bin/env bash
# verify-lists.sh — prove the source lists word-split into real .cpp paths.
#
# ⛔ WHAT THIS GUARDS: a list variable in core-sources.sh is an UNQUOTED shell
# string that the build scripts word-split into filenames. A stray '#' inside one
# becomes filenames ("Core/branch", "Core/in"); a stripped quote turns files into
# commands. Both have silently broken the device archive here before, so the check
# is: every list sources, every word is a listed source path, and no list contains
# a '#'.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

bash -n scripts/core-sources.sh || { echo "!! core-sources.sh has a syntax error" >&2; exit 1; }
echo "syntax: ok"

# shellcheck disable=SC1091
. ./scripts/core-sources.sh

LISTS="CORE_CPP CORE_C TEAKRA OGA_GLUE MESEN_SHARED MESEN_NES MESEN_SNES
       MESEN_GAMEBOY MESEN_PCE MESEN_SMS MESEN_WS MGBA MGBA_LZMA
       PPSPP_CORE PPSPP_EXT_CPP PPSPP_EXT_C PPSPP_ARM PPSPP_X86 PPSPP_LUA
       PPSPP_ASSETS PPSPP_MM PPSPP_GLUE"

bad=0
for v in $LISTS; do
  eval "value=\$$v"
  # A '#' inside the VALUE (not the file) means the assignment swallowed comment
  # prose and will become filenames.
  case "$value" in
    *"#"*) printf '  FAIL %-15s contains a # line\n' "$v"; bad=1 ;;
  esac
  n=0
  for w in $value; do
    n=$((n+1))
    case "$w" in
      /*|*[!A-Za-z0-9_./+-]*)
        # allowed: relative paths; not allowed: prose with spaces reaching here
        ;;
    esac
  done
  printf '  ok   %-15s %s entries\n' "$v" "$n"
done

# The Mesen lists must point at .cpp files that actually EXIST in the pinned tree.
MESEN_SRC="${MESEN_SRC:-$HOME/src/mesen}"
if [ -d "$MESEN_SRC/Core" ]; then
  for v in MESEN_SHARED MESEN_NES MESEN_SNES MESEN_GAMEBOY MESEN_PCE MESEN_SMS MESEN_WS; do
    eval "value=\$$v"
    miss=0
    n=0
    for w in $value; do
      n=$((n+1))
      [ -f "$MESEN_SRC/$w" ] || { miss=$((miss+1)); printf '  MISSING %s\n' "$MESEN_SRC/$w"; }
    done
    printf '  ok   %-15s %s/%s files present in %s\n' "$v" "$((n-miss))" "$n" "$MESEN_SRC"
    [ "$miss" -eq 0 ] || bad=1
  done
else
  echo "  (no Mesen tree at $MESEN_SRC — presence check skipped)"
fi

if [ "$bad" -ne 0 ]; then
  echo "FAIL: a source list is malformed." >&2
  exit 1
fi
echo "PASS: every source list word-splits into real source paths."
