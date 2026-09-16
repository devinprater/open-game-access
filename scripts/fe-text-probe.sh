#!/usr/bin/env bash
# fe-text-probe.sh — where does FE11 dialogue actually live?
#
# The RAM scan found message IDs ("MTUTH_00", "MCT000") and asset filenames
# ("startup.cmb", "panel.cl") but NO narrative prose. That is a real finding, not a
# failed probe: it means the game reaches text through the message tables rather than
# holding dialogue in RAM, so the text has to be resolved from the ROM archive.
#
# This dumps the evidence needed to decide how to resolve it.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${ROM:-$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds}"

[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 2; }

echo "=== ROM ==="
ls -la "$ROM" | awk '{print "  size:", $5, "bytes"}'

echo
echo "=== do known FE11 strings appear in the ROM? ==="
for s in Marth Caeda Jagen Malledus Talys bmap001 MTUTH; do
  n=$(grep -aoc -- "$s" "$ROM" 2>/dev/null || true)
  printf '  %-10s %s\n' "$s" "${n:-0}"
done

echo
echo "=== longest ASCII runs in the ROM (dialogue-like, >=32 chars) ==="
strings -n 32 "$ROM" 2>/dev/null | head -20

echo
echo "=== count of ASCII runs >=32 chars ==="
strings -n 32 "$ROM" 2>/dev/null | wc -l

echo
echo "=== any ROM-offset message tables? look for the MCT/MTUTH id strings ==="
strings -n 5 "$ROM" 2>/dev/null | grep -E "^(MCT|MTUTH|gop|ill)_?[0-9]" | head -12

echo
echo "=== the .nds file table (FAT) at 0x40 ==="
xxd -s 0x40 -l 0x20 "$ROM" 2>/dev/null
