#!/usr/bin/env bash
# check-sim-scripts.sh — syntax-check and exercise the simulator packaging chain.
#
# This is the local proxy for what CI does, so a CI-only failure can be reproduced
# (and fixed) here in seconds rather than in a 7-minute remote run.
set -uo pipefail
cd "$HOME/pokemon-access-ios" || exit 1

echo "== syntax =="
bad=0
for f in scripts/lib-macho.sh scripts/package-sim-app.sh scripts/verify-sim-app.sh \
         scripts/build-sim.sh scripts/sim-uitest.sh scripts/build-sim-app.sh; do
  if bash -n "$f" 2>/tmp/e; then echo "  ok: $f"; else echo "  FAIL: $f"; head -3 /tmp/e; bad=1; fi
done
[ "$bad" = "0" ] || exit 1

echo
echo "== lib-macho detection on known binaries =="
# shellcheck disable=SC1091
. ./scripts/lib-macho.sh
echo "  tools present:$(macho_platform_tools)"
for b in \
  ".build/arm64-apple-ios-simulator/debug/PokemonAccess-App" \
  ".build/arm64-apple-ios/debug/PokemonAccess-App" \
  ".build/arm64-apple-ios-simulator/debug/PokemonAccess-App.dSYM/Contents/Resources/DWARF/PokemonAccess-App" \
  "xtool/PokemonAccess.app/PokemonAccess"
do
  if [ -f "$b" ]; then
    echo "  [$(macho_platform "$b")] $b"
  else
    echo "  [absent] $b"
  fi
done

echo
echo "== packaging the simulator app =="
bash scripts/package-sim-app.sh 2>&1 | tail -12

echo
echo "== verification =="
bash scripts/verify-sim-app.sh 2>&1 | tail -10
