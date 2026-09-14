#!/usr/bin/env bash
# probe-build.sh — build the reverse-engineering probe.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1
bash "$ROOT/scripts/build-host.sh" || exit 1
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/probe Core/probe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -10
[ -x Vendor/probe ] && echo "probe: linked" || { echo "!! probe did not link"; exit 1; }
