#!/usr/bin/env bash
# frameprobe.sh — is Black hung or just idle-waiting? Hash the frames.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/open-game-access/Core/frameprobe.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/frameprobe Core/frameprobe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/frameprobe ] && echo yes || echo NO)"
echo
echo "########## BLACK with firmware, 90000 frames ##########"
timeout 700 ./Vendor/frameprobe "$HOME/hosttest-data/black.nds" 1 90000 2>&1 | tail -30
