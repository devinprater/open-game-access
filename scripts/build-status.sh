#!/usr/bin/env bash
# build-status.sh — clean-env build of the iOS app with the Darwin SDK selected.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
unset POKECORE_LIB

cd "$ROOT" || exit 1
export POKECORE_LIB="$ROOT/Vendor/libpokecore.a"

echo "=== swift build with --swift-sdk darwin ==="
timeout 280 swift build --triple arm64-apple-ios --swift-sdk darwin 2>&1 | tail -25
echo "EXIT=${PIPESTATUS[0]}"
