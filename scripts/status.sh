#!/usr/bin/env bash
# status.sh — report the true state of the iOS project + xtool stack.
set -uo pipefail
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
unset POKECORE_LIB

cd "$HOME/pokemon-access-ios" || exit 1

echo "=== xtool auth ==="
timeout 25 xtool auth status 2>&1 | head -5

echo
echo "=== xtool sdk ==="
timeout 25 xtool sdk status 2>&1 | head -10

echo
echo "=== usbmux socket ==="
ls -la /var/run/usbmuxd 2>&1
systemctl is-active usbmuxd 2>&1

echo
echo "=== device ==="
timeout 20 idevice_id -l 2>&1 | head -3

echo
echo "=== core archive ==="
ls -la Vendor/libpokecore.a 2>&1

echo
echo "=== swift build (clean env) ==="
export POKECORE_LIB="$HOME/pokemon-access-ios/Vendor/libpokecore.a"
timeout 280 swift build --triple arm64-apple-ios 2>&1 | tail -20
echo "SWIFT_BUILD_EXIT=$?"
