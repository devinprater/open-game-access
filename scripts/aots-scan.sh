#!/usr/bin/env bash
# aots-scan.sh — one clean look at the Dragon Ball Z: Attack of the Saiyans region.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
echo "shim present: $([ -f "$PA_SHIM" ] && echo yes || echo NO)"
echo
echo "== claimed money 020CC370 / bonus 020CC84C / AP 020CD4A4 =="
timeout 600 ./Vendor/ramscan "$HOME/roms/Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds" \
  6000 020CC000 2000 20 2>&1 | head -30
