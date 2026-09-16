#!/usr/bin/env bash
# fe-sweep-frames.sh — find the frame where the map overlay is mapped AND enemies exist.
#
# ⛔ WHY A SWEEP. Two different things must both be true:
#   * gMapStateManager (0x021E3328) lives in OVERLAY bss, so it is a valid pointer only
#     while ov000 is the loaded overlay. Read outside that window and every cursor
#     command correctly answers "Not on a map yet".
#   * the enemy force (gForces, arm9 bss) is populated as soon as the chapter loads.
# So there is a window where enemies exist but the cursor is unreadable, and reading at
# the wrong frame looks like a reader failure. This finds the frame where both hold.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

if [ -z "${SAVE:-}" ]; then
  for c in "$HOME/fe/saves/fe11-usa-finalboss.sav" "$ROOT/fe/saves/"*.sav; do
    [ -f "$c" ] && { export SAVE="$c"; break; }
  done
fi
echo "save: ${SAVE:-<none>}"
echo

for F in 3000 3600 4200 4600 5000 5600 6200 7000 8000; do
  OUT="$(timeout 900 ./Vendor/fe_access \
        "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$F" "$ROOT/fe/plans/units.txt" 2>&1)"
  CUR="$(printf '%s' "$OUT" | grep -oE 'Cursor [0-9]+, [0-9]+' | head -1)"
  TERR="$(printf '%s' "$OUT" | grep -oE 'Terrain category [0-9]+' | head -1)"
  ENEMY="$(printf '%s' "$OUT" | grep -oE 'Next enemy   -> .*' | head -1)"
  UNITS="$(printf '%s' "$OUT" | grep -oE 'live units    : [0-9]+' | head -1)"
  printf 'f=%-5s  %-18s %-22s %-30s %s\n' "$F" "${CUR:-no-cursor}" "${TERR:-no-terrain}" "${ENEMY:-<none>}" "${UNITS:-}"
done
