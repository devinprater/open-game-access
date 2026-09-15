#!/usr/bin/env bash
# fe-probe.sh — run the Fire Emblem probe plan and convert screenshots to PNG.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
export PA_SHIM="$ROOT/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
mkdir -p "$HOME/fe/out"
bash "$ROOT/scripts/probe-build.sh" || exit 1
FRAMES="${FRAMES:-4200}"
plan="${1:-$ROOT/fe/plans/boot.txt}"
echo "== plan: $plan, $FRAMES frames"
timeout 900 ./Vendor/probe "$HOME/roms/Fire Emblem - Shadow Dragon (USA).nds" "$FRAMES" "$plan" 2>&1 | tail -40
echo
echo "== screenshots =="
ls -la /tmp/fe/*.ppm 2>/dev/null | head -30
