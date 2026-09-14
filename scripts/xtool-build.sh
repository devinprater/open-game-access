#!/usr/bin/env bash
# xtool-build.sh — build the iOS app with xtool (build only; no device needed).
set -uo pipefail
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
unset POKECORE_LIB

cd "$HOME/pokemon-access-ios" || exit 1
export POKECORE_LIB="$HOME/pokemon-access-ios/Vendor/libpokecore.a"

echo "=== xtool dev build ==="
timeout 280 xtool dev build 2>&1 | tail -40
echo "EXIT=${PIPESTATUS[0]}"
echo
echo "=== artifacts ==="
find . -maxdepth 4 -name "*.app" -o -maxdepth 4 -name "*.ipa" 2>/dev/null | head
ls -la xtool/ 2>/dev/null | head
