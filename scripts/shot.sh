#!/usr/bin/env bash
# shot.sh — capture DS screens as PNGs so the rendering can actually be looked at.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/shot.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/shot Core/shot.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -6
echo "linked: $([ -x Vendor/shot ] && echo yes || echo NO)"

export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
timeout 300 ./Vendor/shot "$HOME/hosttest-data/black.nds" 6 3000 2>&1 | tail -10

echo
echo "=== convert to PNG ==="
for p in /home/devin/shot_top_*.ppm /home/devin/shot_bot_*.ppm; do
  [ -f "$p" ] || continue
  if command -v convert >/dev/null 2>&1; then convert "$p" "${p%.ppm}.png"; fi
done
ls -la /home/devin/shot_*.png 2>/dev/null | head
ls -la /home/devin/shot_*.ppm 2>/dev/null | head
