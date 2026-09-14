#!/usr/bin/env bash
# keymask.sh — DS keys are ACTIVE LOW. My harness passes mask=0 when nothing is
# pressed. If SetKeyMask does not invert that, the game sees EVERY BUTTON HELD
# from frame zero, which would derail a game's boot.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"

echo "=== NDS::SetKeyMask + TouchScreen / ReleaseScreen ==="
grep -n 'void NDS::SetKeyMask' -A 10 "$SRC/NDS.cpp"
grep -n 'void NDS::TouchScreen' -A 10 "$SRC/NDS.cpp"
grep -n 'void NDS::ReleaseScreen' -A 6 "$SRC/NDS.cpp"

echo
echo "=== KeyInput default / semantics ==="
grep -n 'KeyInput = \|KeyInput &\|u16 KeyInput' "$SRC/NDS.cpp" "$SRC/NDS.h" | head -12

echo
echo "=== what my core passes ==="
grep -n 'SetKeyMask\|buttonsDown' -B3 -A6 "$ROOT/Core/pokecore.cpp" | head -30
