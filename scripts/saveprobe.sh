#!/usr/bin/env bash
# saveprobe.sh — is the cart's save memory backed?
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/saveprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/saveprobe Core/saveprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/saveprobe ] && echo yes || echo NO)"

# Android requires the .sav file to exist. Create a 512KB one for this test.
python3 -c "open('$HOME/roms/black.sav','wb').write(b'\xff'*524288)"
ls -la "$HOME/roms/black.sav"

echo
echo "########## BLACK, NO save path ##########"
timeout 150 ./Vendor/saveprobe "$HOME/hosttest-data/black.nds" "" 2>&1 | tail -8
echo
echo "########## BLACK, WITH a 512KB save file ##########"
timeout 150 ./Vendor/saveprobe "$HOME/hosttest-data/black.nds" "$HOME/roms/black.sav" 2>&1 | tail -8
