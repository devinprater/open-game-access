#!/usr/bin/env bash
# fe-terrain2-full.sh — run feterrain2 and save the FULL output to a file, then show
# the parts that matter. Piping through head/tail loses the middle of a long dump and
# made an earlier read look like the matrix was garbage when only rows 25+ were.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
FRAMES="${1:-6000}"
PLAN="${2:-tutorial2}"
OUT="$HOME/fe/out/terrain2-$FRAMES.txt"
mkdir -p "$HOME/fe/out"

bash "$ROOT/scripts/fe-terrain2.sh" "$FRAMES" "$PLAN" > "$OUT" 2>&1

echo "=== full output: $OUT ($(wc -l < "$OUT") lines) ==="
echo
echo "########## database header ##########"
sed -n '1,40p' "$OUT"
echo
echo "########## unk_28 header + INTERPRETATION A vs B ##########"
grep -n -A14 'unk_28 = TerrainCostData' "$OUT" | head -22
echo
echo "########## interpretation labels ##########"
grep -n 'INTERPRETATION' "$OUT"
